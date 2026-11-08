#include <core/rendering/resources/mesh_page_reader.h>
#include <windows.h>
#include <algorithm>
#include <thread>

namespace lumen {

static HANDLE _open_shared(const std::filesystem::path& p_path)
{
    return CreateFileW(p_path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
}

static bool _read_at(HANDLE p_file, uint64_t p_offset, void* r_data, uint32_t p_size)
{
    OVERLAPPED ov{};
    ov.Offset = (DWORD)(p_offset & 0xFFFFFFFFull);
    ov.OffsetHigh = (DWORD)(p_offset >> 32);
    DWORD read = 0;
    return ReadFile(p_file, r_data, p_size, &read, &ov) && read == p_size;
}

void MeshPageReader::start()
{
    if (!workers.empty()) return;
    quit = false;
    const uint32_t hw = std::max(1u, std::thread::hardware_concurrency());
    const uint32_t count = std::clamp(hw / 4u, 2u, MAX_WORKERS);
    for (uint32_t i = 0; i < count; i++) workers.emplace_back([this] { _run(); });
}

void MeshPageReader::stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        quit = true;
        pending.clear();
    }
    wake.notify_all();
    for (std::thread& w : workers) {
        if (w.joinable()) w.join();
    }
    workers.clear();
    std::lock_guard<std::mutex> lock(mutex);
    finished.clear();
    in_flight = 0;
}

void MeshPageReader::submit(std::vector<Request>& r_requests)
{
    if (r_requests.empty()) return;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (Request& r : r_requests) pending.push_back(std::move(r));
        in_flight += (uint32_t)r_requests.size();
    }
    r_requests.clear();
    wake.notify_all();
}

uint64_t MeshPageReader::cancel(uint32_t p_mesh)
{
    std::lock_guard<std::mutex> lock(mutex);
    uint64_t bytes = 0;
    const size_t before = pending.size();
    pending.erase(std::remove_if(pending.begin(), pending.end(), [&](const Request& r) {
        if (r.mesh != p_mesh) return false;
        bytes += r.bytes;
        return true;
    }), pending.end());
    in_flight -= (uint32_t)(before - pending.size());
    return bytes;
}

void MeshPageReader::drain(std::vector<Result>& r_out)
{
    std::lock_guard<std::mutex> lock(mutex);
    for (Result& r : finished) r_out.push_back(std::move(r));
    in_flight -= (uint32_t)finished.size();
    finished.clear();
}

void MeshPageReader::_gather(std::vector<Request>& r_batch)
{
    const Request& first = r_batch.front();
    const LMeshData& tables = *first.source->tables;
    uint64_t start = tables.pages[first.page].offset;
    uint64_t end = start + tables.pages[first.page].size;
    bool grew = true;
    while (grew && r_batch.size() < COALESCE_MAX_PAGES) {
        grew = false;
        uint32_t scanned = 0;
        for (auto it = pending.begin(); it != pending.end() && scanned < COALESCE_SCAN && r_batch.size() < COALESCE_MAX_PAGES; scanned++) {
            if (it->source != first.source) { ++it; continue; }
            const LMeshPage& info = tables.pages[it->page];
            const bool after = info.offset == end;
            const bool before = info.offset + info.size == start;
            if ((!after && !before) || (end - start) + info.size > COALESCE_MAX_BYTES) { ++it; continue; }
            if (after) end += info.size;
            else start = info.offset;
            r_batch.push_back(std::move(*it));
            it = pending.erase(it);
            grew = true;
        }
    }
}

void MeshPageReader::_run()
{
    std::vector<uint8_t> blob;
    std::vector<Request> batch;
    HANDLE file = INVALID_HANDLE_VALUE;
    std::shared_ptr<const Source> open_source;

    for (;;) {
        batch.clear();
        {
            std::unique_lock<std::mutex> lock(mutex);
            if (pending.empty() && file != INVALID_HANDLE_VALUE) {
                lock.unlock();
                CloseHandle(file);
                file = INVALID_HANDLE_VALUE;
                open_source.reset();
                lock.lock();
            }
            wake.wait(lock, [this] { return quit || !pending.empty(); });
            if (quit) break;
            batch.push_back(std::move(pending.front()));
            pending.pop_front();
            _gather(batch);
        }

        const std::shared_ptr<const Source> source = batch.front().source;
        const Source& src = *source;
        uint64_t start = UINT64_MAX, end = 0;
        for (const Request& r : batch) {
            const LMeshPage& info = src.tables->pages[r.page];
            start = std::min(start, info.offset);
            end = std::max(end, info.offset + info.size);
        }

        if (open_source != source) {
            if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
            file = _open_shared(src.path);
            open_source = file != INVALID_HANDLE_VALUE ? source : nullptr;
        }
        const char* read_error = nullptr;
        if (file == INVALID_HANDLE_VALUE) {
            read_error = "cannot open";
        } else {
            blob.resize((size_t)(end - start));
            if (!_read_at(file, start, blob.data(), (uint32_t)(end - start))) read_error = "read failed";
        }

        std::vector<Result> results(batch.size());
        for (size_t i = 0; i < batch.size(); i++) {
            const Request& r = batch[i];
            Result& res = results[i];
            res.mesh = r.mesh;
            res.generation = r.generation;
            res.page = r.page;
            res.bytes = r.bytes;
            res.error = read_error;
            if (res.error) continue;
            const LMeshPage& info = src.tables->pages[r.page];
            res.error = lmesh_decode_page(blob.data() + (info.offset - start), info.size, info, src.flags, res.data);
            if (!res.error) res.error = lmesh_validate_page(*src.tables, r.page, res.data);
        }

        std::lock_guard<std::mutex> lock(mutex);
        for (Result& res : results) finished.push_back(std::move(res));
    }

    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
}

}

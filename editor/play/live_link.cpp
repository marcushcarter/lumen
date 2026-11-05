#include <editor/play/live_link.h>
#include <core/world/world.h>
#include <core/world/world_file.h>
#include <windows.h>
#include <string>
#include <vector>

namespace lumen {

Error LiveLink::open()
{
    using enum Error;
    close();
    name = std::wstring(LIVE_PIPE_PREFIX) + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(++_sequence);
    HANDLE h = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, LIVE_PIPE_BUFFER, 0, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        log_write("Live Link: CreateNamedPipe failed (%lu).", GetLastError());
        return FAILED;
    }
    OVERLAPPED* ov = new OVERLAPPED{};
    ov->hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    pipe = h;
    overlapped = ov;
    connecting = false;
    connected = false;
    _poll_connect();
    return OK;
}

void LiveLink::close()
{
    if (pipe) {
        CancelIo((HANDLE)pipe);
        CloseHandle((HANDLE)pipe);
        pipe = nullptr;
    }
    if (overlapped) {
        OVERLAPPED* ov = (OVERLAPPED*)overlapped;
        if (ov->hEvent) CloseHandle(ov->hEvent);
        delete ov;
        overlapped = nullptr;
    }
    connecting = false;
    connected = false;
    world = Guid{};
    sent.clear();
    sent_environment = 0;
}

void LiveLink::_poll_connect()
{
    if (!pipe || connected) return;
    OVERLAPPED* ov = (OVERLAPPED*)overlapped;
    if (!connecting) {
        ResetEvent(ov->hEvent);
        if (ConnectNamedPipe((HANDLE)pipe, ov)) {
            connected = true;
            return;
        }
        const DWORD err = GetLastError();
        if (err == ERROR_PIPE_CONNECTED) connected = true;
        else if (err == ERROR_IO_PENDING) connecting = true;
        else log_write("Live Link: ConnectNamedPipe failed (%lu).", err);
        return;
    }
    DWORD unused = 0;
    if (GetOverlappedResult((HANDLE)pipe, ov, &unused, FALSE)) {
        connecting = false;
        connected = true;
    } else if (GetLastError() != ERROR_IO_INCOMPLETE) {
        connecting = false;
    }
}

bool LiveLink::_send(LiveMessage p_type, const void* p_data, size_t p_size)
{
    if (!connected || p_size > LIVE_MESSAGE_MAX) return false;
    std::vector<uint8_t> bytes(sizeof(LiveHeader) + p_size);
    LiveHeader header;
    header.size = (uint32_t)p_size;
    header.type = p_type;
    std::memcpy(bytes.data(), &header, sizeof(header));
    if (p_size) std::memcpy(bytes.data() + sizeof(header), p_data, p_size);

    OVERLAPPED* ov = (OVERLAPPED*)overlapped;
    ResetEvent(ov->hEvent);
    DWORD written = 0;
    if (!WriteFile((HANDLE)pipe, bytes.data(), (DWORD)bytes.size(), &written, ov) && GetLastError() != ERROR_IO_PENDING) {
        log_write("Live Link: the game disconnected.");
        connected = false;
        return false;
    }
    if (!GetOverlappedResult((HANDLE)pipe, ov, &written, TRUE) || written != bytes.size()) {
        connected = false;
        return false;
    }
    return true;
}

void LiveLink::_snapshot(World& r_world, std::unordered_map<uint64_t, uint64_t>& r_hashes)
{
    r_hashes.clear();
    std::vector<Entity> entities;
    r_world.view<EntityIdComponent>([&](Entity p_entity, const EntityIdComponent&) { entities.push_back(p_entity); });
    for (Entity e : entities) r_hashes[r_world.try_get<EntityIdComponent>(e)->guid.value] = WorldFile::entity_hash(r_world, e);
}

void LiveLink::baseline(World& r_world, Guid p_world)
{
    world = p_world;
    _snapshot(r_world, sent);
    sent_environment = WorldFile::environment_hash(r_world);
}

void LiveLink::tick(World& r_world, Guid p_world, double p_now)
{
    if (!pipe) return;
    _poll_connect();
    if (!connected || p_now - poll < POLL_S) return;
    poll = p_now;

    if (p_world != world) {
        WorldFile file;
        file.guid = p_world;
        const std::string text = WorldFile::write_text(r_world, file);
        if (_send(LiveMessage::WORLD, text.data(), text.size())) baseline(r_world, p_world);
        return;
    }

    _snapshot(r_world, _current);
    for (const auto& [guid, hash] : _current) {
        auto it = sent.find(guid);
        if (it != sent.end() && it->second == hash) continue;
        const std::string text = WorldFile::entity_text(r_world, r_world.find(Guid{ guid }));
        if (!_send(LiveMessage::ENTITY, text.data(), text.size())) return;
        sent[guid] = hash;
    }
    for (auto it = sent.begin(); it != sent.end();) {
        if (_current.contains(it->first)) {
            ++it;
            continue;
        }
        const uint64_t guid = it->first;
        if (!_send(LiveMessage::DESTROY, &guid, sizeof(guid))) return;
        it = sent.erase(it);
    }
    if (const uint64_t env = WorldFile::environment_hash(r_world); env != sent_environment) {
        const std::string text = WorldFile::environment_text(r_world);
        if (_send(LiveMessage::ENVIRONMENT, text.data(), text.size())) sent_environment = env;
    }
}

}

#include <game/application/live_client.h>
#include <core/application/live_protocol.h>
#include <core/world/world.h>
#include <core/world/world_file.h>
#include <windows.h>
#include <cstring>
#include <string_view>

namespace lumen {

Error LiveClient::connect(const std::wstring& p_name)
{
    using enum Error;
    close();
    HANDLE h = CreateFileW(p_name.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        log_write("Live Client: couldn't connect to the editor (%lu), live edits are off.", GetLastError());
        return FAILED;
    }
    pipe = h;
    log_write("Live Client: connected to the editor.");
    return OK;
}

void LiveClient::close()
{
    if (pipe) CloseHandle((HANDLE)pipe);
    pipe = nullptr;
    buffer.clear();
}

void LiveClient::tick(World& r_world)
{
    if (!pipe) return;
    DWORD available = 0;
    if (!PeekNamedPipe((HANDLE)pipe, nullptr, 0, nullptr, &available, nullptr)) {
        log_write("Live Client: the editor disconnected.");
        close();
        return;
    }
    if (available) {
        const size_t start = buffer.size();
        buffer.resize(start + available);
        DWORD read = 0;
        if (!ReadFile((HANDLE)pipe, buffer.data() + start, available, &read, nullptr)) {
            close();
            return;
        }
        buffer.resize(start + read);
    }

    size_t offset = 0;
    while (buffer.size() - offset >= sizeof(LiveHeader)) {
        LiveHeader header;
        std::memcpy(&header, buffer.data() + offset, sizeof(header));
        if (header.size > LIVE_MESSAGE_MAX) {
            log_write("Live Client: bad message from the editor, live edits are off.");
            close();
            return;
        }
        if (buffer.size() - offset - sizeof(header) < header.size) break;
        _apply(r_world, (uint8_t)header.type, buffer.data() + offset + sizeof(header), header.size);
        offset += sizeof(header) + header.size;
    }
    buffer.erase(buffer.begin(), buffer.begin() + (ptrdiff_t)offset);
}

void LiveClient::_apply(World& r_world, uint8_t p_type, const uint8_t* p_data, uint32_t p_size)
{
    const std::string_view text((const char*)p_data, p_size);
    switch ((LiveMessage)p_type) {
        case LiveMessage::WORLD: {
            r_world.unload();
            WorldFile file;
            WorldFile::read_text(text, r_world, false, file);
        } break;
        case LiveMessage::ENTITY:
            WorldFile::apply_entity_text(text, r_world, false);
            break;
        case LiveMessage::DESTROY: {
            if (p_size != sizeof(uint64_t)) break;
            Guid guid;
            std::memcpy(&guid.value, p_data, sizeof(uint64_t));
            if (const Entity e = r_world.find(guid); e != ENTITY_NULL) r_world.destroy(e);
        } break;
        case LiveMessage::ENVIRONMENT:
            WorldFile::apply_environment_text(text, r_world);
            break;
    }
}

}

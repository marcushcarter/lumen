#pragma once
#include <filesystem>
#include <fstream>
#include <string_view>

namespace lumen {

inline bool write_file_atomic(const std::filesystem::path& p_path, const void* p_data, size_t p_size)
{
    std::filesystem::path tmp = p_path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(static_cast<const char*>(p_data), static_cast<std::streamsize>(p_size));
        if (!out) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, p_path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

inline bool write_file_atomic(const std::filesystem::path& p_path, std::string_view p_text)
{
    return write_file_atomic(p_path, p_text.data(), p_text.size());
}

}

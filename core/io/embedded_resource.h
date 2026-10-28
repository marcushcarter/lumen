#pragma once
#include <windows.h>

namespace lumen {

struct EmbeddedResource
{
    struct Blob {
        const void* data = nullptr;
        size_t size = 0;
        explicit operator bool() const { return data != nullptr; }
    };

    static Blob load(const wchar_t* p_resource_name);
    static HICON load_icon(const wchar_t* p_resource_name);
};

}

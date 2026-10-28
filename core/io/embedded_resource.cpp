#include <core/io/embedded_resource.h>

namespace lumen {

EmbeddedResource::Blob EmbeddedResource::load(const wchar_t* p_resource_name)
{
    HMODULE mod = GetModuleHandleW(nullptr);
    HRSRC res = FindResourceW(mod, p_resource_name, RT_RCDATA);
    if (!res) return {};
    HGLOBAL mem = LoadResource(mod, res);
    if (!mem) return {};
    return { LockResource(mem), SizeofResource(mod, res) };
}

HICON EmbeddedResource::load_icon(const wchar_t* p_resource_name)
{
    return LoadIconW(GetModuleHandleW(nullptr), p_resource_name);
}

}

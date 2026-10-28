#pragma once
#include <cstdint>
#include <string>

namespace lumen {

template<typename T, int Channels>
struct ImageData
{
    T* pixels = nullptr;
    int width = 0;
    int height = 0;
    int source_channels = 0;

    [[nodiscard]] bool valid() const { return pixels != nullptr && width > 0 && height > 0; }
};

struct ImageIO
{
    static void _write_callback(void* p_context, void* p_data, int p_size);

    template<typename T, int Channels>
    static ImageData<T, Channels> load_from_resource(const std::wstring& p_resource_name);

    template<typename T, int Channels>
    static ImageData<T, Channels> load_from_file(const std::wstring& p_path);

    template<typename T, int Channels>
    static void free_image(ImageData<T, Channels>& p_image);

    template<typename T, int Channels>
    static bool save_png(const std::wstring& p_path, const ImageData<T, Channels>& p_image);
};

}

#include <core/io/image_io.inl>

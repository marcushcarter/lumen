#include <core/io/embedded_resource.h>
#include <stb_image.h>
#include <stb_image_write.h>
#include <cstdio>
#include <cstdlib>

namespace lumen {

inline void ImageIO::_write_callback(void* p_context, void* p_data, int p_size)
{
    std::fwrite(p_data, 1, static_cast<size_t>(p_size), static_cast<FILE*>(p_context));
}

template<typename T, int Channels>
ImageData<T, Channels> ImageIO::load_from_file(const std::wstring& p_path)
{
    ImageData<T, Channels> image;
    FILE* file = nullptr;
    if (_wfopen_s(&file, p_path.c_str(), L"rb") != 0 || !file) return image;
    image.pixels = reinterpret_cast<T*>(stbi_load_from_file(file, &image.width, &image.height, &image.source_channels, Channels));
    fclose(file);
    return image;
}

template<typename T, int Channels>
ImageData<T, Channels> ImageIO::load_from_resource(const std::wstring& p_resource_name)
{
    ImageData<T, Channels> image;
    const EmbeddedResource::Blob blob = EmbeddedResource::load(p_resource_name.c_str());
    if (!blob || blob.size == 0) return image;
    image.pixels = reinterpret_cast<T*>(stbi_load_from_memory(static_cast<const unsigned char*>(blob.data), static_cast<int>(blob.size), &image.width, &image.height, &image.source_channels, Channels));
    return image;
}

template<typename T, int Channels>
void ImageIO::free_image(ImageData<T, Channels>& p_image)
{
    if (!p_image.pixels) return;
    stbi_image_free(p_image.pixels);
    p_image.pixels = nullptr;
}

template<typename T, int Channels>
bool ImageIO::save_png(const std::wstring& p_path, const ImageData<T, Channels>& p_image)
{
    if (!p_image.valid()) return false;
    FILE* file = nullptr;
    if (_wfopen_s(&file, p_path.c_str(), L"wb") != 0 || !file) return false;
    const int stride = p_image.width * Channels;
    const int result = stbi_write_png_to_func(_write_callback, file, p_image.width, p_image.height, Channels, p_image.pixels, stride);
    fclose(file);
    return result != 0;
}

}

#include <editor/editor_resources.h>
#include <core/io/image_io.h>
#include <core/io/embedded_resource.h>

namespace lumen {

Error EditorResources::initialize(drivers::DeviceDriverVulkan& r_dd)
{
    using enum Error;

    dd = &r_dd;
    
    ImageData<uint8_t, 4> icon_data = ImageIO::load_from_resource<uint8_t, 4>(L"LOGOS_MINIMAL_PNG");
    if (icon_data.valid()) icon_image = dd->image_create_texture(icon_data.pixels, static_cast<uint32_t>(icon_data.width), static_cast<uint32_t>(icon_data.height), "editor_logo");
    ImageIO::free_image(icon_data);

    ImageData<uint8_t, 4> logo_data = ImageIO::load_from_resource<uint8_t, 4>(L"LOGOS_LOGO_PNG");
    if (logo_data.valid()) logo_image = dd->image_create_texture(logo_data.pixels, static_cast<uint32_t>(logo_data.width), static_cast<uint32_t>(logo_data.height), "editor_logo");
    ImageIO::free_image(logo_data);

    ImageData<uint8_t, 4> test_data = ImageIO::load_from_resource<uint8_t, 4>(L"LOGOS_LOGO_PNG");
    if (test_data.valid()) test_thumbnail = dd->image_create_texture(test_data.pixels, static_cast<uint32_t>(test_data.width), static_cast<uint32_t>(test_data.height), "editor_logo");
    ImageIO::free_image(test_data);

    {
        EmbeddedResource::Blob blob = EmbeddedResource::load(L"LICENSE");
        license_text.assign((const char*)blob.data, blob.size);
        license_text.erase(std::remove(license_text.begin(), license_text.end(), '\r'), license_text.end());
    }

    {
        EmbeddedResource::Blob blob = EmbeddedResource::load(L"EULA_MD");
        eula_text.assign((const char*)blob.data, blob.size);
        eula_text.erase(std::remove(eula_text.begin(), eula_text.end(), '\r'), eula_text.end());
    }

    return OK;
}

void EditorResources::shutdown()
{
    dd->device_wait_idle();

    dd->image_free(icon_image);
    dd->image_free(logo_image);
    dd->image_free(test_thumbnail);
}

}
#pragma once
#include <drivers/vulkan/device_driver_vulkan.h>
#include <core/base/error.h>
#include <string>

namespace lumen {

struct EditorResources
{
    drivers::DeviceDriverVulkan* dd = nullptr;
    drivers::DeviceDriverVulkan::Image icon_image;
    drivers::DeviceDriverVulkan::Image logo_image;
    std::string license_text;
    std::string eula_text;

    Error initialize(drivers::DeviceDriverVulkan& r_dd);
    void shutdown();
};

}

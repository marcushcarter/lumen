#pragma once
#include <drivers/vulkan/lumen_vulkan.h>
#include <core/base/error.h>
#include <core/base/vsync_mode.h>
#include <windows.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

namespace lumen::drivers {

struct DriverDevice {
    std::wstring name;
    uint32_t vendor_id = 0;
    VkPhysicalDeviceType device_type = VK_PHYSICAL_DEVICE_TYPE_OTHER;
};

struct ContextDriverVulkan
{
    struct Functions {
        // Debug Messenger
        PFN_vkDebugUtilsMessengerCallbackEXT DebugUtilsMessengerCallbackEXT = nullptr;
        PFN_vkDestroyDebugUtilsMessengerEXT DestroyDebugUtilsMessengerEXT = nullptr;
    };

    struct DeviceQueueFamilies {
        std::vector<VkQueueFamilyProperties> properties;
    };

    VkInstance instance = VK_NULL_HANDLE;
    uint32_t instance_api_version = VK_API_VERSION_1_0;
    std::unordered_map<std::string, bool> requested_instance_extensions;
    std::unordered_set<std::string> enabled_instance_extension_names;
    std::vector<const char*> enabled_validation_layer_names;
    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
    std::vector<VkPhysicalDevice> physical_devices;
    std::vector<DriverDevice> driver_devices;
    std::vector<DeviceQueueFamilies> device_queue_families;
    Functions functions;

    Error _initialize_vulkan_version();
    void _register_requested_instance_extension(const std::string& p_extension_name, bool p_required);
    Error _initialize_instance_extensions();
    Error _find_validation_layers();
    Error _initialize_instance();
    Error _initialize_devices();

    Error initialize();
    Error full_initialize_windows(HWND p_hwnd);
    void shutdown();

    struct Surface {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        uint32_t width = 0;
        uint32_t height = 0;
        VsyncMode vsync_mode = VsyncMode::OFF;
        VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
        bool needs_resize = false;
    };

    Surface surface;

    Error surface_create(HWND p_hwnd);
    void surface_set_size(uint32_t p_width, uint32_t p_height);
    void surface_set_vsync_mode(VsyncMode p_mode);

    int optimal_device_index = -1;
    uint32_t graphics_queue_family = 0;
    uint32_t present_queue_family = 0;
    uint32_t transfer_queue_family = 0;
    uint32_t compute_queue_family = 0;
    
    Error physical_device_select(int p_override_index = -1);

    VkInstance instance_get() const;
    const DriverDevice& device_get(uint32_t p_device_index) const;
    uint32_t device_get_count();
	VkPhysicalDevice physical_device_get(uint32_t p_device_index) const;
    VkQueueFamilyProperties queue_family_get(uint32_t p_device_index, uint32_t p_queue_family_index) const;
    uint32_t queue_family_get_count(uint32_t p_device_index) const;
    const Functions& functions_get() const;

    static VKAPI_ATTR VkBool32 VKAPI_CALL _debug_messenger_callback(VkDebugUtilsMessageSeverityFlagBitsEXT p_message_severity, VkDebugUtilsMessageTypeFlagsEXT p_message_type, const VkDebugUtilsMessengerCallbackDataEXT *p_callback_data, void *p_user_data);
};

}
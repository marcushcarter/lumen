#include <drivers/vulkan/context_driver_vulkan.h>
#include <core/version.h>
#include <windows.h>
#include <iostream>

namespace lumen::drivers {

Error ContextDriverVulkan::_initialize_vulkan_version()
{
    using enum Error;

    typedef VkResult(VKAPI_PTR* PFN_vkEnumerateInstanceVersion)(uint32_t*);
    auto enumerate_instance_version = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion")
    );

    if (enumerate_instance_version) {
        uint32_t api_version = 0;
        VkResult err = enumerate_instance_version(&api_version);
        LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, FAILED, "vkEnumerateInstanceVersion failed unexpectedly.");
        instance_api_version = api_version;
    } else {
        instance_api_version = VK_API_VERSION_1_0;
    }

    LUMEN_ERR_FAIL_COND_V_MSG(instance_api_version < VK_API_VERSION_1_2, FAILED,
        "Your graphics driver only supports an older version of Vulkan than this engine requires (1.2 minimum)."
        "Please update your GPU drvier.");

    return OK;
}

void ContextDriverVulkan::_register_requested_instance_extension(const std::string& p_extension_name, bool p_required) {
    requested_instance_extensions[p_extension_name] = p_required;
}

Error ContextDriverVulkan::_initialize_instance_extensions()
{
    using enum Error;

    enabled_instance_extension_names.clear();

    _register_requested_instance_extension(VK_KHR_SURFACE_EXTENSION_NAME, true);
    _register_requested_instance_extension(VK_KHR_WIN32_SURFACE_EXTENSION_NAME, true);
    
    _register_requested_instance_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, true);
    // _register_requested_instance_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, false);
    _register_requested_instance_extension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, false);

    uint32_t instance_extension_count = 0;
    VkResult err = vkEnumerateInstanceExtensionProperties(nullptr, &instance_extension_count, nullptr);

    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS && err != VK_INCOMPLETE, FAILED,
        "vkEnumerateInstanceExtensionProperties (count query) failed.");
    LUMEN_ERR_FAIL_COND_V_MSG(instance_extension_count == 0, FAILED,
        "No Vulkan instance extensions were found.");

    std::vector<VkExtensionProperties> instance_extensions(instance_extension_count);
    err = vkEnumerateInstanceExtensionProperties(nullptr, &instance_extension_count, instance_extensions.data());

    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS && err != VK_INCOMPLETE, FAILED,
        "vkEnumerateInstanceExtensionProperties (fetch) failed.");

    for (const auto& extension : instance_extensions) {
        std::string extension_name(extension.extensionName);
        if (requested_instance_extensions.contains(extension_name)) {
            enabled_instance_extension_names.insert(extension_name);
        }
    }

    for (const auto& [name, is_required] : requested_instance_extensions) {
        if (!enabled_instance_extension_names.contains(name)) {
            LUMEN_ERR_FAIL_COND_V_MSG(is_required, FAILED,
                ("Required Vulkan instance extension " + name + " was not found.").c_str());
        }
    }
        
    return OK;
}

Error ContextDriverVulkan::_find_validation_layers()
{
    using enum Error;

    enabled_validation_layer_names.clear();

    uint32_t instance_layer_count = 0;
    VkResult err = vkEnumerateInstanceLayerProperties(&instance_layer_count, nullptr);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, FAILED,
        "vkEnumerateInstanceLayerProperties (count query) failed.");

    if (instance_layer_count == 0) {
        return OK;
    }

    std::vector<VkLayerProperties> layer_properties(instance_layer_count);
    err = vkEnumerateInstanceLayerProperties(&instance_layer_count, layer_properties.data());
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, FAILED,
        "vkEnumerateInstanceLayerProperties (fetch) failed.");

    for (const auto& properties : layer_properties) {
        if (strcmp(properties.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
            enabled_validation_layer_names.push_back("VK_LAYER_KHRONOS_validation");
            break;
        }
    }

    if (enabled_validation_layer_names.empty()) {
        fprintf(stderr, "[Lumen] Warning: VK_LAYER_KHRONOS_validation not found. Running without validation layers.\n");
    }

    return OK;
}

VKAPI_ATTR VkBool32 VKAPI_CALL ContextDriverVulkan::_debug_messenger_callback(VkDebugUtilsMessageSeverityFlagBitsEXT p_message_severity, VkDebugUtilsMessageTypeFlagsEXT p_message_type, const VkDebugUtilsMessengerCallbackDataEXT *p_callback_data, void *p_user_data)
{
    (void)p_message_type;
    auto* self = reinterpret_cast<ContextDriverVulkan*>(p_user_data);
    (void)self;
    const char* level = (p_message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "ERROR" : (p_message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) ? "WARN" : "INFO";
    log_write("\n[VULKAN][%s]\nID: %s\n%s\n", level, p_callback_data->pMessageIdName ? p_callback_data->pMessageIdName : "unknown", p_callback_data->pMessage);
    return VK_FALSE;
}

Error ContextDriverVulkan::_initialize_instance()
{
    using enum Error;

    // if (p_validation) {
        Error err = _find_validation_layers();
        LUMEN_ERR_FAIL_COND_V(err != OK, err);
    // }

    // Instance
    VkApplicationInfo app_info{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app_info.pApplicationName = LUMEN_VERSION_NAME;
    app_info.applicationVersion = VK_MAKE_VERSION(LUMEN_VERSION_MAJOR, LUMEN_VERSION_MINOR, LUMEN_VERSION_PATCH);
    app_info.pEngineName = LUMEN_VERSION_NAME;
    app_info.engineVersion = VK_MAKE_VERSION(LUMEN_VERSION_MAJOR, LUMEN_VERSION_MINOR, LUMEN_VERSION_PATCH);
    app_info.apiVersion = instance_api_version;

    std::vector<const char*> extension_name_ptrs;
    for (const auto& name : enabled_instance_extension_names) {
        extension_name_ptrs.push_back(name.c_str());
    }
    bool validation_enabled = !enabled_validation_layer_names.empty();

    VkValidationFeatureEnableEXT enabled_validation_features[] = {
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
    };

    VkValidationFeaturesEXT validation_features{ VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT };
    validation_features.enabledValidationFeatureCount = static_cast<uint32_t>(std::size(enabled_validation_features));
    validation_features.pEnabledValidationFeatures = enabled_validation_features;

    VkInstanceCreateInfo instance_ci{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    instance_ci.pApplicationInfo = &app_info;
    instance_ci.enabledExtensionCount = static_cast<uint32_t>(extension_name_ptrs.size());
    instance_ci.ppEnabledExtensionNames = extension_name_ptrs.data();
    instance_ci.enabledLayerCount = static_cast<uint32_t>(enabled_validation_layer_names.size());
    instance_ci.ppEnabledLayerNames = enabled_validation_layer_names.empty() ? nullptr : enabled_validation_layer_names.data();

    if (validation_enabled) {
        extension_name_ptrs.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
        instance_ci.enabledExtensionCount = static_cast<uint32_t>(extension_name_ptrs.size());
        instance_ci.ppEnabledExtensionNames = extension_name_ptrs.data();
        instance_ci.pNext = &validation_features;
    }

    VkResult result = vkCreateInstance(&instance_ci, nullptr, &instance);
    LUMEN_ERR_FAIL_COND_V_MSG(result != VK_SUCCESS, FAILED,
        "vkCreateInstance failed. Do you have a compatible Vulkan driver installed?");

    // Debug Messenger
    if (validation_enabled) {
        functions.DebugUtilsMessengerCallbackEXT = _debug_messenger_callback;

        VkDebugUtilsMessengerCreateInfoEXT debug_messenger_ci{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };

        debug_messenger_ci.messageSeverity =
            // VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            // VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

        debug_messenger_ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debug_messenger_ci.pfnUserCallback = functions.DebugUtilsMessengerCallbackEXT;
        debug_messenger_ci.pUserData = this;

        auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
        LUMEN_ERR_FAIL_COND_V_MSG(!func, FAILED, "vkCreateDebugUtilsMessengerEXT not present.");

        VkResult debug_result = func(instance, &debug_messenger_ci, nullptr, &debug_messenger);
        LUMEN_ERR_FAIL_COND_V_MSG(debug_result != VK_SUCCESS, FAILED, "Failed to create Vulkan debug messenger.");
        
        functions.DestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        LUMEN_ERR_FAIL_COND_V_MSG(!functions.DestroyDebugUtilsMessengerEXT, FAILED, "vkDestroyDebugUtilsMessengerEXT not present.");
    }

    // fill functions

    return OK;
}

Error ContextDriverVulkan::_initialize_devices()
{
    using enum Error;

    physical_devices.clear();

    uint32_t physical_device_count = 0;
    VkResult err = vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, FAILED, "vkEnumeratePhysicalDevices (count query) failed.");
    LUMEN_ERR_FAIL_COND_V_MSG(physical_device_count == 0, FAILED, "No Vulkan-capable GPUs were found on this system.");

    driver_devices.resize(physical_device_count);
    physical_devices.resize(physical_device_count);
    device_queue_families.resize(physical_device_count);
    err = vkEnumeratePhysicalDevices(instance, &physical_device_count, physical_devices.data());
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, FAILED, "vkEnumeratePhysicalDevices (fetch) failed.");

	for (uint32_t i = 0; i < physical_devices.size(); i++) {
		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties(physical_devices[i], &props);

		DriverDevice& driver_device = driver_devices[i];
        driver_device.name = std::wstring(props.deviceName, props.deviceName + strlen(props.deviceName));
        driver_device.vendor_id = props.vendorID;
        driver_device.device_type = props.deviceType;

		uint32_t queue_family_properties_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &queue_family_properties_count, nullptr);

		if (queue_family_properties_count > 0) {
			device_queue_families[i].properties.resize(queue_family_properties_count);
			vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &queue_family_properties_count, device_queue_families[i].properties.data());
		}
	}

    return OK;
}

Error ContextDriverVulkan::initialize()
{
    using enum Error;
    Error err;
    
    err = _initialize_vulkan_version();
	LUMEN_ERR_FAIL_COND_V(err != OK, err);

    err = _initialize_instance_extensions();
	LUMEN_ERR_FAIL_COND_V(err != OK, err);

    err = _initialize_instance();
	LUMEN_ERR_FAIL_COND_V(err != OK, err);

    err = _initialize_devices();
	LUMEN_ERR_FAIL_COND_V(err != OK, err);

    return OK;
}

Error ContextDriverVulkan::full_initialize_windows(HWND p_hwnd)
{
    using enum Error;

    Error err = initialize();
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    // can be called for other backends
    err = surface_create(p_hwnd);
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    // can be manually selected
    err = physical_device_select();
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    return OK;
}

void ContextDriverVulkan::shutdown()
{
    if (debug_messenger) {
        functions.DestroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
        debug_messenger = VK_NULL_HANDLE;
    }

    if (surface.surface) {
        vkDestroySurfaceKHR(instance, surface.surface, nullptr);
        surface.surface = VK_NULL_HANDLE;
    }

    if (instance) {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
}

Error ContextDriverVulkan::surface_create(HWND p_hwnd)
{
    using enum Error;

    VkWin32SurfaceCreateInfoKHR surface_ci{ VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
    surface_ci.hinstance = GetModuleHandleW(nullptr);
    surface_ci.hwnd = p_hwnd;

    VkResult err = vkCreateWin32SurfaceKHR(instance, &surface_ci, nullptr, &surface.surface);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, FAILED,
        "vkCreateWin32SurfaceKHR failed.");
        
    surface.needs_resize = true;

    return OK;
}

void ContextDriverVulkan::surface_set_size(uint32_t p_width, uint32_t p_height)
{
    if (p_width == 0 || p_height == 0) return;
    if (p_width == surface.width && p_height == surface.height) return;

	surface.width = p_width;
	surface.height = p_height;
	surface.needs_resize = true;
}

void ContextDriverVulkan::surface_set_vsync_mode(VsyncMode p_mode)
{
    if (surface.vsync_mode == p_mode) return;
    surface.vsync_mode = p_mode;
    surface.needs_resize = true;
}

static void resolve_queue_families(VkPhysicalDevice p_device, VkSurfaceKHR p_surface, const std::vector<VkQueueFamilyProperties>& p_queue_properties, int& r_graphics, int& r_present, int& r_transfer, int& r_compute)
{
    r_graphics = -1;
    r_present = -1;
    r_transfer = -1;
    r_compute = -1;

    for (uint32_t q = 0; q < p_queue_properties.size(); ++q) {
        if (r_graphics < 0 && (p_queue_properties[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            r_graphics = q;
        }

        VkBool32 can_present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(p_device, q, p_surface, &can_present);
        if (r_present < 0 && can_present) {
            r_present = q;
        }

        if (r_transfer < 0 && (p_queue_properties[q].queueFlags & VK_QUEUE_TRANSFER_BIT)
            && !(p_queue_properties[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            r_transfer = q;
        }
        if (r_compute < 0 && (p_queue_properties[q].queueFlags & VK_QUEUE_COMPUTE_BIT)
            && !(p_queue_properties[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            r_compute = q;
        }
    }

    if (r_transfer < 0) r_transfer = r_graphics;
    if (r_compute < 0) r_compute = r_graphics;
}

static bool device_has_required_extensions(VkPhysicalDevice p_device, const std::vector<const char*>& p_required)
{
    uint32_t ext_count = 0;
    vkEnumerateDeviceExtensionProperties(p_device, nullptr, &ext_count, nullptr);
    std::vector<VkExtensionProperties> extensions(ext_count);
    vkEnumerateDeviceExtensionProperties(p_device, nullptr, &ext_count, extensions.data());

    for (const char* required : p_required) {
        bool found = false;
        for (const auto& ext : extensions) {
            if (strcmp(required, ext.extensionName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

Error ContextDriverVulkan::physical_device_select(int p_override_index)
{
    using enum Error;

    LUMEN_ERR_FAIL_COND_V_MSG(surface.surface == VK_NULL_HANDLE, FAILED,
        "select_physical_device() requires create_surface() to have already succeeded.");

    static const std::vector<const char*> required_device_extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    if (p_override_index >= 0) {
        LUMEN_ERR_FAIL_COND_V_MSG(static_cast<size_t>(p_override_index) >= physical_devices.size(), FAILED,
            "Requested physical device index is out of range.");

        VkPhysicalDevice candidate = physical_devices[p_override_index];

        LUMEN_ERR_FAIL_COND_V_MSG(!device_has_required_extensions(candidate, required_device_extensions), FAILED,
            "Requested physical device does not support required extensions.");

        int graphics, present, transfer, compute;
        resolve_queue_families(candidate, surface.surface, device_queue_families[p_override_index].properties,
            graphics, present, transfer, compute);

        LUMEN_ERR_FAIL_COND_V_MSG(graphics < 0 || present < 0, FAILED,
            "Requested physical device lacks required queue support.");

        optimal_device_index = p_override_index;
        graphics_queue_family = static_cast<uint32_t>(graphics);
        present_queue_family = static_cast<uint32_t>(present);
        transfer_queue_family = static_cast<uint32_t>(transfer);
        compute_queue_family = static_cast<uint32_t>(compute);

        return OK;
    }

    int highest_score = -1;

    for (uint32_t i = 0; i < physical_devices.size(); ++i) {
        VkPhysicalDevice candidate = physical_devices[i];

        if (!device_has_required_extensions(candidate, required_device_extensions)) {
            continue;
        }

        int graphics, present, transfer, compute;
        resolve_queue_families(candidate, surface.surface, device_queue_families[i].properties,
            graphics, present, transfer, compute);

        if (graphics < 0 || present < 0) {
            continue;
        }

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);

        int score = 0;
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score += 1000;
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score += 500;
        score += static_cast<int>(properties.limits.maxImageDimension2D);

        if (score > highest_score) {
            highest_score = score;
            optimal_device_index = i;
            graphics_queue_family = static_cast<uint32_t>(graphics);
            present_queue_family = static_cast<uint32_t>(present);
            transfer_queue_family = static_cast<uint32_t>(transfer);
            compute_queue_family = static_cast<uint32_t>(compute);
        }
    }

    LUMEN_ERR_FAIL_COND_V_MSG(optimal_device_index == -1, FAILED,
        "No suitable GPU found (missing required extensions or queue support).");

    return OK;
}

VkInstance ContextDriverVulkan::instance_get() const {
    return instance;
}

const DriverDevice& ContextDriverVulkan::device_get(uint32_t p_device_index) const {
    return driver_devices[p_device_index];
}

uint32_t ContextDriverVulkan::device_get_count() {
    return static_cast<uint32_t>(driver_devices.size());
}

VkPhysicalDevice ContextDriverVulkan::physical_device_get(uint32_t p_device_index) const {
    return physical_devices[p_device_index];
}

VkQueueFamilyProperties ContextDriverVulkan::queue_family_get(uint32_t p_device_index, uint32_t p_queue_family_index) const {
    return device_queue_families[p_device_index].properties[p_queue_family_index];
}

uint32_t ContextDriverVulkan::queue_family_get_count(uint32_t p_device_index) const {
    return static_cast<uint32_t>(device_queue_families[p_device_index].properties.size());
}

const ContextDriverVulkan::Functions& ContextDriverVulkan::functions_get() const {
    return functions;
}

}
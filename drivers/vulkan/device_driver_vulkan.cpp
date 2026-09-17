#include <drivers/vulkan/device_driver_vulkan.h>
#include <core/io/embedded_resource.h>
#include <core/io/path.h>
#include <vulkan/vulkan.hpp>
#include <iostream>
#include <algorithm>
#include <mutex>
#include <fstream>
#include <filesystem>
#include <memory>
#include <cctype>
#include <cstring>

namespace lumen::drivers {

/***************/
/**** SETUP ****/
/***************/

bool DeviceDriverVulkan::memory_budget_enabled() const {
    return enabled_device_extension_names.contains(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
}

void DeviceDriverVulkan::_register_requested_device_extension(const std::string& p_extension_name, bool p_required) {
    requested_device_extensions[p_extension_name] = p_required;
}

Error DeviceDriverVulkan::_initialize_device_extensions()
{
    using enum Error;

    requested_device_extensions.clear();
    enabled_device_extension_names.clear();

    _register_requested_device_extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME, true);
    _register_requested_device_extension(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME, true);
    _register_requested_device_extension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME, true);

    _register_requested_device_extension(VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME, false);
    _register_requested_device_extension(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME, false);
    _register_requested_device_extension(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME, false);
    _register_requested_device_extension(VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME, false);

#ifdef LUMEN_EDITOR
    _register_requested_device_extension(VK_EXT_DEBUG_MARKER_EXTENSION_NAME, false);
    _register_requested_device_extension(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME, false);
    _register_requested_device_extension(VK_EXT_DEVICE_MEMORY_REPORT_EXTENSION_NAME, false);
    _register_requested_device_extension(VK_EXT_DEVICE_FAULT_EXTENSION_NAME, false);
    _register_requested_device_extension(VK_NV_RAY_TRACING_VALIDATION_EXTENSION_NAME, false);
#endif

    // if (features.variable_rate_shading) {
    //     _register_requested_device_extension(VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME, false);
    // }

    // if (features.subgroup_size_control) {
    //     _register_requested_device_extension(VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME, false);
    // }

    // if (features.ray_tracing) {
    //     _register_requested_device_extension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, true);
    //     _register_requested_device_extension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, true);
    //     _register_requested_device_extension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, true);
    // }

    uint32_t device_extension_count = 0;
    VkResult err = vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &device_extension_count, nullptr);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "vkEnumerateDeviceExtensionProperties (count query) failed.");
    LUMEN_ERR_FAIL_COND_V_MSG(device_extension_count == 0, Failed, "Couldn't find any Vulkan device extensions. Do you have a compatible Vulkan installable client driver (ICD) installed?");

	std::vector<VkExtensionProperties> device_extensions;
    device_extensions.resize(device_extension_count);
    err = vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &device_extension_count, device_extensions.data());
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't get Vulkan device extension properties.");
    
    for (uint32_t i = 0; i < device_extension_count; i++) {
        std::string extension_name(device_extensions[i].extensionName);
        if (requested_device_extensions.contains(extension_name)) {
            enabled_device_extension_names.insert(extension_name);
        }
    }

    for (const auto& [name, is_required] : requested_device_extensions) {
        if (!enabled_device_extension_names.contains(name)) {
            LUMEN_ERR_FAIL_COND_V_MSG(is_required, Failed, ("Required Vulkan device extension " + name + " was not found.").c_str());
        }
    }

    return Ok;
}

void DeviceDriverVulkan::_get_device_properties()
{
    vkGetPhysicalDeviceProperties(physical_device, &physical_device_properties);

    std::string device_type = vk::to_string(vk::PhysicalDeviceType(physical_device_properties.deviceType));

    log_write("Vulkan %u.%u.%u - Driver %u.%u.%u - Using Device #%d: %s (%s)",
        VK_API_VERSION_MAJOR(physical_device_properties.apiVersion),
        VK_API_VERSION_MINOR(physical_device_properties.apiVersion),
        VK_API_VERSION_PATCH(physical_device_properties.apiVersion),
        VK_API_VERSION_MAJOR(physical_device_properties.driverVersion),
        VK_API_VERSION_MINOR(physical_device_properties.driverVersion),
        VK_API_VERSION_PATCH(physical_device_properties.driverVersion),
        cd->optimal_device_index,
        physical_device_properties.deviceName,
        device_type.c_str());
}

Error DeviceDriverVulkan::_check_device_features()
{
    using enum Error;

    vkGetPhysicalDeviceFeatures(physical_device, &physical_device_features);

    if (!physical_device_features.imageCubeArray || !physical_device_features.independentBlend) {
        std::string error_string = "Your GPU does not support required Vulkan features:\n";
        if (!physical_device_features.imageCubeArray) error_string += "- No support for image cube arrays.\n";
        if (!physical_device_features.independentBlend) error_string += "- No support for independentBlend.\n";
        error_string += "This is usually a hardware limitation; updating drivers won't help.";
        LUMEN_ERR_FAIL_COND_V_MSG(true, Failed, error_string.c_str());
    }

#define VK_DEVICEFEATURE_ENABLE_IF(x) \
    if (physical_device_features.x) { \
        requested_device_features.x = physical_device_features.x; \
    } else \
        ((void)0)
        
    requested_device_features = {};
    VK_DEVICEFEATURE_ENABLE_IF(fullDrawIndexUint32);
    VK_DEVICEFEATURE_ENABLE_IF(imageCubeArray);
    VK_DEVICEFEATURE_ENABLE_IF(independentBlend);
    VK_DEVICEFEATURE_ENABLE_IF(geometryShader);
    VK_DEVICEFEATURE_ENABLE_IF(tessellationShader);
    VK_DEVICEFEATURE_ENABLE_IF(sampleRateShading);
    VK_DEVICEFEATURE_ENABLE_IF(dualSrcBlend);
    VK_DEVICEFEATURE_ENABLE_IF(logicOp);
    VK_DEVICEFEATURE_ENABLE_IF(multiDrawIndirect);
    VK_DEVICEFEATURE_ENABLE_IF(drawIndirectFirstInstance);
    VK_DEVICEFEATURE_ENABLE_IF(depthClamp);
    VK_DEVICEFEATURE_ENABLE_IF(depthBiasClamp);
    VK_DEVICEFEATURE_ENABLE_IF(fillModeNonSolid);
    VK_DEVICEFEATURE_ENABLE_IF(depthBounds);
    VK_DEVICEFEATURE_ENABLE_IF(wideLines);
    VK_DEVICEFEATURE_ENABLE_IF(largePoints);
    VK_DEVICEFEATURE_ENABLE_IF(samplerAnisotropy);
    VK_DEVICEFEATURE_ENABLE_IF(textureCompressionBC);
    VK_DEVICEFEATURE_ENABLE_IF(vertexPipelineStoresAndAtomics);
    VK_DEVICEFEATURE_ENABLE_IF(fragmentStoresAndAtomics);
    VK_DEVICEFEATURE_ENABLE_IF(shaderClipDistance);
    VK_DEVICEFEATURE_ENABLE_IF(shaderCullDistance);
    VK_DEVICEFEATURE_ENABLE_IF(shaderInt64);
    VK_DEVICEFEATURE_ENABLE_IF(shaderInt16);
    VK_DEVICEFEATURE_ENABLE_IF(pipelineStatisticsQuery);
    VK_DEVICEFEATURE_ENABLE_IF(occlusionQueryPrecise);
    VK_DEVICEFEATURE_ENABLE_IF(shaderStorageImageExtendedFormats);

#undef VK_DEVICEFEATURE_ENABLE_IF

    return Ok;
}

void DeviceDriverVulkan::_check_subgroup_capabilities()
{
    VkPhysicalDeviceSubgroupProperties subgroup_properties{};
    subgroup_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;

    VkPhysicalDeviceProperties2 properties2{};
    properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties2.pNext = &subgroup_properties;

    vkGetPhysicalDeviceProperties2(physical_device, &properties2);

    subgroup_capabilities.size = subgroup_properties.subgroupSize;
    subgroup_capabilities.supported_stages = subgroup_properties.supportedStages;
    subgroup_capabilities.supported_operations = subgroup_properties.supportedOperations;
}

Error DeviceDriverVulkan::_check_device_capabilities()
{
    using enum Error;

    _check_subgroup_capabilities();

    return Ok;
}

Error DeviceDriverVulkan::_add_queue_create_info(std::vector<VkDeviceQueueCreateInfo> &r_queue_create_info)
{
    using enum Error;

    static const float queue_priority = 1.0f;

    std::vector<uint32_t> distinct_families = {
        cd->graphics_queue_family,
        cd->present_queue_family,
        cd->transfer_queue_family,
        cd->compute_queue_family
    };

    std::sort(distinct_families.begin(), distinct_families.end());
    distinct_families.erase(std::unique(distinct_families.begin(), distinct_families.end()), distinct_families.end());

    queue_families.resize(cd->queue_family_get_count(device_index));

    for (uint32_t family_index : distinct_families) {
        VkDeviceQueueCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        create_info.queueFamilyIndex = family_index;
        create_info.queueCount = 1;
        create_info.pQueuePriorities = &queue_priority;
        r_queue_create_info.push_back(create_info);
        queue_families[family_index].resize(1);
    }

    return Ok;
}

Error DeviceDriverVulkan::_initialize_device(const std::vector<VkDeviceQueueCreateInfo> &p_queue_create_info)
{
    using enum Error;

    std::vector<const char*> enabled_extension_names;
    enabled_extension_names.reserve(enabled_device_extension_names.size());
    for (const std::string& extension_name : enabled_device_extension_names) {
        enabled_extension_names.push_back(extension_name.c_str());
    }

    VkPhysicalDeviceFeatures2 supported_features2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    VkPhysicalDeviceVulkan12Features supported_1_2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    VkPhysicalDeviceVulkan11Features supported_1_1{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
    supported_features2.pNext = &supported_1_2;
    supported_1_2.pNext = &supported_1_1;
    vkGetPhysicalDeviceFeatures2(physical_device, &supported_features2);
    
    LUMEN_ERR_FAIL_COND_V_MSG(!physical_device_features.shaderStorageImageExtendedFormats, Failed, "GPU lacks shaderStorageImageExtendedFormats, required for r16f Hi-Z and rg16 G-buffer storage.");

    LUMEN_ERR_FAIL_COND_V_MSG(!supported_1_1.storageBuffer16BitAccess, Failed, "GPU lacks storageBuffer16BitAccess, required for 16-bit vertex data.");
    LUMEN_ERR_FAIL_COND_V_MSG(!supported_1_1.shaderDrawParameters, Failed, "GPU lacks shaderDrawParameters, required for gl_DrawID in indirect draws.");

    LUMEN_ERR_FAIL_COND_V_MSG(!supported_1_2.descriptorIndexing, Failed, "GPU lacks descriptorIndexing, required for bindless rendering.");
    LUMEN_ERR_FAIL_COND_V_MSG(!supported_1_2.runtimeDescriptorArray, Failed, "GPU lacks runtimeDescriptorArray, required for bindless rendering.");
    LUMEN_ERR_FAIL_COND_V_MSG(!supported_1_2.descriptorBindingPartiallyBound, Failed, "GPU lacks descriptorBindingPartiallyBound.");
    LUMEN_ERR_FAIL_COND_V_MSG(!supported_1_2.descriptorBindingSampledImageUpdateAfterBind, Failed, "GPU lacks descriptorBindingSampledImageUpdateAfterBind.");
    LUMEN_ERR_FAIL_COND_V_MSG(!supported_1_2.descriptorBindingStorageImageUpdateAfterBind, Failed, "GPU lacks descriptorBindingStorageImageUpdateAfterBind.");
    LUMEN_ERR_FAIL_COND_V_MSG(!supported_1_2.descriptorBindingVariableDescriptorCount, Failed, "GPU lacks descriptorBindingVariableDescriptorCount.");
    LUMEN_ERR_FAIL_COND_V_MSG(!supported_1_2.shaderSampledImageArrayNonUniformIndexing, Failed, "GPU lacks shaderSampledImageArrayNonUniformIndexing.");
    LUMEN_ERR_FAIL_COND_V_MSG(!supported_1_2.shaderStorageImageArrayNonUniformIndexing, Failed, "GPU lacks shaderStorageImageArrayNonUniformIndexing.");
    LUMEN_ERR_FAIL_COND_V_MSG(!supported_1_2.scalarBlockLayout, Failed, "GPU lacks scalarBlockLayout, required for POD SSBO struct layout.");
    LUMEN_ERR_FAIL_COND_V_MSG(!supported_1_2.storageBuffer8BitAccess, Failed, "GPU lacks storageBuffer8BitAccess, required for 8-bit skin data.");
    LUMEN_ERR_FAIL_COND_V_MSG(!supported_1_2.drawIndirectCount, Failed, "GPU lacks drawIndirectCount, required for GPU-driven indirect-count draws.");
    LUMEN_ERR_FAIL_COND_V_MSG(!supported_1_2.vulkanMemoryModel, Failed, "GPU lacks vulkanMemoryModel, required for single-pass Hi-Z.");
    
    void* create_info_next = nullptr;

    VkPhysicalDeviceVulkan13Features features_1_3{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    features_1_3.dynamicRendering = VK_TRUE;
    features_1_3.synchronization2 = VK_TRUE;
    features_1_3.pNext = create_info_next;
    create_info_next = &features_1_3;

    VkPhysicalDeviceVulkan12Features features_1_2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    features_1_2.descriptorIndexing = VK_TRUE;
    features_1_2.runtimeDescriptorArray = VK_TRUE;
    features_1_2.bufferDeviceAddress = VK_TRUE;
    features_1_2.descriptorBindingPartiallyBound = VK_TRUE;
    features_1_2.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    features_1_2.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
    features_1_2.descriptorBindingVariableDescriptorCount = VK_TRUE;
    features_1_2.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    features_1_2.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
    features_1_2.separateDepthStencilLayouts = VK_TRUE;
    features_1_2.scalarBlockLayout = VK_TRUE;
    features_1_2.storageBuffer8BitAccess = VK_TRUE;
    features_1_2.drawIndirectCount = VK_TRUE;
    features_1_2.vulkanMemoryModel = VK_TRUE;
    features_1_2.vulkanMemoryModelDeviceScope = VK_TRUE;
    features_1_2.pNext = create_info_next;
    create_info_next = &features_1_2;

    VkPhysicalDeviceVulkan11Features features_1_1{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
    features_1_1.storageBuffer16BitAccess = VK_TRUE;
    features_1_1.shaderDrawParameters = VK_TRUE;
    features_1_1.pNext = create_info_next;
    create_info_next = &features_1_1;

    // VkPhysicalDeviceShaderFloat16Int8FeaturesKHR shader_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES_KHR };
	// shader_features.shaderFloat16 = shader_capabilities.shader_float16_is_supported;
	// shader_features.shaderInt8 = shader_capabilities.shader_int8_is_supported;
	// shader_features.pNext = create_info_next;
	// create_info_next = &shader_features;

// 	VkPhysicalDeviceBufferDeviceAddressFeaturesKHR buffer_device_address_features = {};
// 	if (buffer_device_address_support) {
// 		buffer_device_address_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR;
// 		buffer_device_address_features.pNext = create_info_next;
// 		buffer_device_address_features.bufferDeviceAddress = buffer_device_address_support;
// 		create_info_next = &buffer_device_address_features;
// 	}

// 	VkPhysicalDeviceVulkanMemoryModelFeaturesKHR vulkan_memory_model_features = {};
// 	if (vulkan_memory_model_support && vulkan_memory_model_device_scope_support) {
// 		vulkan_memory_model_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES_KHR;
// 		vulkan_memory_model_features.pNext = create_info_next;
// 		vulkan_memory_model_features.vulkanMemoryModel = vulkan_memory_model_support;
// 		vulkan_memory_model_features.vulkanMemoryModelDeviceScope = vulkan_memory_model_device_scope_support;
// 		create_info_next = &vulkan_memory_model_features;
// 	}

// 	VkPhysicalDeviceFragmentShadingRateFeaturesKHR fsr_features = {};
// 	if (fsr_capabilities.pipeline_supported || fsr_capabilities.primitive_supported || fsr_capabilities.attachment_supported) {
// 		fsr_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR;
// 		fsr_features.pNext = create_info_next;
// 		fsr_features.pipelineFragmentShadingRate = fsr_capabilities.pipeline_supported;
// 		fsr_features.primitiveFragmentShadingRate = fsr_capabilities.primitive_supported;
// 		fsr_features.attachmentFragmentShadingRate = fsr_capabilities.attachment_supported;
// 		create_info_next = &fsr_features;
// 	}

// 	VkPhysicalDeviceFragmentDensityMapFeaturesEXT fdm_features = {};
// 	if (fdm_capabilities.attachment_supported || fdm_capabilities.dynamic_attachment_supported || fdm_capabilities.non_subsampled_images_supported) {
// 		fdm_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT;
// 		fdm_features.pNext = create_info_next;
// 		fdm_features.fragmentDensityMap = fdm_capabilities.attachment_supported;
// 		fdm_features.fragmentDensityMapDynamic = fdm_capabilities.dynamic_attachment_supported;
// 		fdm_features.fragmentDensityMapNonSubsampledImages = fdm_capabilities.non_subsampled_images_supported;
// 		create_info_next = &fdm_features;
// 	}

// 	VkPhysicalDeviceFragmentDensityMapOffsetFeaturesQCOM fdm_offset_features = {};
// 	if (fdm_capabilities.offset_supported) {
// 		fdm_offset_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_OFFSET_FEATURES_QCOM;
// 		fdm_offset_features.pNext = create_info_next;
// 		fdm_offset_features.fragmentDensityMapOffset = VK_TRUE;
// 		create_info_next = &fdm_offset_features;
// 	}

// 	VkPhysicalDevicePipelineCreationCacheControlFeatures pipeline_cache_control_features = {};
// 	if (pipeline_cache_control_support) {
// 		pipeline_cache_control_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_CREATION_CACHE_CONTROL_FEATURES;
// 		pipeline_cache_control_features.pNext = create_info_next;
// 		pipeline_cache_control_features.pipelineCreationCacheControl = pipeline_cache_control_support;
// 		create_info_next = &pipeline_cache_control_features;
// 	}

// 	VkPhysicalDeviceFaultFeaturesEXT device_fault_features = {};
// 	if (device_fault_support) {
// 		device_fault_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT;
// 		device_fault_features.pNext = create_info_next;
// 		create_info_next = &device_fault_features;
// 	}

// #if defined(VK_TRACK_DEVICE_MEMORY)
// 	VkDeviceDeviceMemoryReportCreateInfoEXT memory_report_info = {};
// 	if (device_memory_report_support) {
// 		memory_report_info.sType = VK_STRUCTURE_TYPE_DEVICE_DEVICE_MEMORY_REPORT_CREATE_INFO_EXT;
// 		memory_report_info.pfnUserCallback = ContextDriverVulkan::memory_report_callback;
// 		memory_report_info.pNext = create_info_next;
// 		memory_report_info.flags = 0;
// 		memory_report_info.pUserData = this;

// 		create_info_next = &memory_report_info;
// 	}
// #endif

// 	VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure_features = {};
// 	if (acceleration_structure_capabilities.acceleration_structure_support) {
// 		acceleration_structure_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
// 		acceleration_structure_features.pNext = create_info_next;
// 		acceleration_structure_features.accelerationStructure = acceleration_structure_capabilities.acceleration_structure_support;
// 		create_info_next = &acceleration_structure_features;
// 	}

// 	VkPhysicalDeviceRayTracingPipelineFeaturesKHR raytracing_pipeline_features = {};
// 	if (raytracing_capabilities.raytracing_pipeline_support) {
// 		raytracing_pipeline_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
// 		raytracing_pipeline_features.pNext = create_info_next;
// 		raytracing_pipeline_features.rayTracingPipeline = raytracing_capabilities.raytracing_pipeline_support;
// 		create_info_next = &raytracing_pipeline_features;
// 	}

// 	VkPhysicalDeviceRayTracingValidationFeaturesNV raytracing_validation_features = {};
// 	if (raytracing_capabilities.validation) {
// 		raytracing_validation_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_VALIDATION_FEATURES_NV;
// 		raytracing_validation_features.pNext = create_info_next;
// 		raytracing_validation_features.rayTracingValidation = raytracing_capabilities.validation;
// 		create_info_next = &raytracing_validation_features;
// 	}

// 	VkPhysicalDeviceVulkan11Features vulkan_1_1_features = {};
// 	VkPhysicalDevice16BitStorageFeaturesKHR storage_features = {};
// 	VkPhysicalDeviceMultiviewFeatures multiview_features = {};
// 	const bool enable_1_2_features = physical_device_properties.apiVersion >= VK_API_VERSION_1_2;
// 	if (enable_1_2_features) {
// 		// In Vulkan 1.2 and newer we use a newer struct to enable various features.
// 		vulkan_1_1_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
// 		vulkan_1_1_features.pNext = create_info_next;
// 		vulkan_1_1_features.storageBuffer16BitAccess = storage_buffer_capabilities.storage_buffer_16_bit_access_is_supported;
// 		vulkan_1_1_features.uniformAndStorageBuffer16BitAccess = storage_buffer_capabilities.uniform_and_storage_buffer_16_bit_access_is_supported;
// 		vulkan_1_1_features.storagePushConstant16 = storage_buffer_capabilities.storage_push_constant_16_is_supported;
// 		vulkan_1_1_features.storageInputOutput16 = storage_buffer_capabilities.storage_input_output_16;
// 		vulkan_1_1_features.multiview = multiview_capabilities.is_supported;
// 		vulkan_1_1_features.multiviewGeometryShader = multiview_capabilities.geometry_shader_is_supported;
// 		vulkan_1_1_features.multiviewTessellationShader = multiview_capabilities.tessellation_shader_is_supported;
// 		vulkan_1_1_features.variablePointersStorageBuffer = 0;
// 		vulkan_1_1_features.variablePointers = 0;
// 		vulkan_1_1_features.protectedMemory = 0;
// 		vulkan_1_1_features.samplerYcbcrConversion = 0;
// 		vulkan_1_1_features.shaderDrawParameters = 0;
// 		create_info_next = &vulkan_1_1_features;
// 	} else {
// 		// On Vulkan 1.0 and 1.1 we use our older structs to initialize these features.
// 		storage_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES_KHR;
// 		storage_features.pNext = create_info_next;
// 		storage_features.storageBuffer16BitAccess = storage_buffer_capabilities.storage_buffer_16_bit_access_is_supported;
// 		storage_features.uniformAndStorageBuffer16BitAccess = storage_buffer_capabilities.uniform_and_storage_buffer_16_bit_access_is_supported;
// 		storage_features.storagePushConstant16 = storage_buffer_capabilities.storage_push_constant_16_is_supported;
// 		storage_features.storageInputOutput16 = storage_buffer_capabilities.storage_input_output_16;
// 		create_info_next = &storage_features;

// 		const bool enable_1_1_features = physical_device_properties.apiVersion >= VK_API_VERSION_1_1;
// 		if (enable_1_1_features) {
// 			multiview_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES;
// 			multiview_features.pNext = create_info_next;
// 			multiview_features.multiview = multiview_capabilities.is_supported;
// 			multiview_features.multiviewGeometryShader = multiview_capabilities.geometry_shader_is_supported;
// 			multiview_features.multiviewTessellationShader = multiview_capabilities.tessellation_shader_is_supported;
// 			create_info_next = &multiview_features;
// 		}
// 	}

    VkDeviceCreateInfo device_ci{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    device_ci.pNext = create_info_next;
    device_ci.queueCreateInfoCount = static_cast<uint32_t>(p_queue_create_info.size());
    device_ci.pQueueCreateInfos = p_queue_create_info.data();
    device_ci.enabledExtensionCount = static_cast<uint32_t>(enabled_extension_names.size());
    device_ci.ppEnabledExtensionNames = enabled_extension_names.data();
    device_ci.pEnabledFeatures = &requested_device_features;
   
    VkResult err = vkCreateDevice(physical_device, &device_ci, nullptr, &device);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed,
        "vkCreateDevice failed. Do you have a compatible Vulkan driver installed?");

	for (uint32_t i = 0; i < queue_families.size(); i++) {
		for (uint32_t j = 0; j < queue_families[i].size(); j++) {
			vkGetDeviceQueue(device, i, j, &queue_families[i][j].queue);
		}
	}

    // Set PFN_Functions

    return Ok;
}

Error DeviceDriverVulkan::initialize(ContextDriverVulkan& r_cd, uint32_t p_device_index, uint32_t p_frame_count)
{
    using enum Error;

    cd = &r_cd;

    device_index = p_device_index;
    driver_device = cd->device_get(device_index);
    physical_device = cd->physical_device_get(device_index);
	frame_count = p_frame_count;

    shader_cache_dir = Paths::shader_cache().string();
    shader_compiler = std::make_unique<shaderc::Compiler>();
    
    Error err = _initialize_device_extensions();
	LUMEN_ERR_FAIL_COND_V(err != Ok, err);
    
    _get_device_properties();
    
    err = _check_device_features();
	LUMEN_ERR_FAIL_COND_V(err != Ok, err);
    
    err = _check_device_capabilities();
	LUMEN_ERR_FAIL_COND_V(err != Ok, err);
    
    std::vector<VkDeviceQueueCreateInfo> queue_create_info;
    err = _add_queue_create_info(queue_create_info);
	LUMEN_ERR_FAIL_COND_V(err != Ok, err);
    
    err = _initialize_device(queue_create_info);
	LUMEN_ERR_FAIL_COND_V(err != Ok, err);
    
    err = allocator_create();
	LUMEN_ERR_FAIL_COND_V(err != Ok, err);

    err = pipeline_cache_create();
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);
    
    err = swapchain_create(&cd->surface);
	LUMEN_ERR_FAIL_COND_V(err != Ok, err);

    err = swapchain_resize(frame_count);
	LUMEN_ERR_FAIL_COND_V(err != Ok, err);

    err = bindless_heap_create(16384, 4096, 256);
	LUMEN_ERR_FAIL_COND_V(err != Ok, err);

    SamplerCreateInfo sampler_ci{};
    sampler_ci.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_ci.address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.name = "default_sampler";
    default_sampler = sampler_create(sampler_ci);

    return Ok;
}

void DeviceDriverVulkan::shutdown()
{
    device_wait_idle();

    sampler_free(default_sampler);
    
    bindless_heap_free();
    swapchain_free();
    pipeline_cache_free();
    shader_compiler.reset();
    allocator_free();

    if (device) {
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }
}

Error DeviceDriverVulkan::device_wait_idle()
{    
    using enum Error;
    VkResult err = vkDeviceWaitIdle(device);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't wait idle for Vulkan device.");
    return Ok;
}

/****************/
/**** MEMORY ****/
/****************/

uint32_t DeviceDriverVulkan::_pool_memory_type(VmaPool p_pool) const
{
    if (!p_pool) return UINT32_MAX;
    if (p_pool == image_transient_pool || p_pool == image_persistent_pool || p_pool == image_texture_pool) return image_device_type_index;
    if (p_pool == buffer_geometry_pool || p_pool == buffer_device_pool) return buffer_device_type_index;
    if (p_pool == buffer_bar_pool) return buffer_bar_type_index;
    if (p_pool == upload_pool) return upload_type_index;
    if (p_pool == readback_pool) return readback_type_index;
    return UINT32_MAX;
}

uint32_t DeviceDriverVulkan::_find_memory_type(VkMemoryPropertyFlags p_properties)
{
    VkPhysicalDeviceMemoryProperties memory_properties{};
    vkGetPhysicalDeviceMemoryProperties(
        physical_device,
        &memory_properties
    );

    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++)
    {
        VkMemoryPropertyFlags flags =
            memory_properties.memoryTypes[i].propertyFlags;

        if ((flags & p_properties) == p_properties)
            return i;
    }

    return UINT32_MAX;
}

VkDeviceSize DeviceDriverVulkan::_heap_size_for_type(uint32_t p_type_index) const
{
    VkPhysicalDeviceMemoryProperties mem{};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem);
    if (p_type_index >= mem.memoryTypeCount) return 0;
    return mem.memoryHeaps[mem.memoryTypes[p_type_index].heapIndex].size;
}

Error DeviceDriverVulkan::_allocator_pools_create()
{
    using enum Error;

    auto make_pool = [&](uint32_t type_index, const char* name, VmaPool* out) -> VkResult {
        VmaPoolCreateInfo ci{};
        ci.memoryTypeIndex = type_index;
        ci.blockSize = 0;
        ci.minBlockCount = 0;
        ci.maxBlockCount = 0;
        VkResult e = vmaCreatePool(allocator, &ci, out);
        if (e == VK_SUCCESS) vmaSetPoolName(allocator, *out, name);
        return e;
    };

    // ---- device-local images ----
    VkImageCreateInfo image_probe{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    image_probe.imageType = VK_IMAGE_TYPE_2D;
    image_probe.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_probe.extent = { 1920, 1080, 1 };
    image_probe.mipLevels = 1;
    image_probe.arrayLayers = 1;
    image_probe.samples = VK_SAMPLE_COUNT_1_BIT;
    image_probe.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_probe.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image_probe.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_probe.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo image_alloc{};
    image_alloc.usage = VMA_MEMORY_USAGE_AUTO;
    image_alloc.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VkResult err = vmaFindMemoryTypeIndexForImageInfo(allocator, &image_probe, &image_alloc, &image_device_type_index);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't resolve memory type for image pools.");

    err = make_pool(image_device_type_index, "image_transient", &image_transient_pool);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't create transient image pool.");
    err = make_pool(image_device_type_index, "image_persistent", &image_persistent_pool);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't create persistent image pool.");
    err = make_pool(image_device_type_index, "image_texture", &image_texture_pool);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't create texture image pool.");

    // ---- device-local buffers ----
    VkBufferCreateInfo buffer_probe{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    buffer_probe.size = 1024 * 1024;
    buffer_probe.usage =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    buffer_probe.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo device_buf_alloc{};
    device_buf_alloc.usage = VMA_MEMORY_USAGE_AUTO;
    device_buf_alloc.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    err = vmaFindMemoryTypeIndexForBufferInfo(allocator, &buffer_probe, &device_buf_alloc, &buffer_device_type_index);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't resolve memory type for device buffer pools.");

    err = make_pool(buffer_device_type_index, "buffer_geometry", &buffer_geometry_pool);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't create geometry buffer pool.");
    err = make_pool(buffer_device_type_index, "buffer_device", &buffer_device_pool);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't create device buffer pool.");

    // ---- bar buffers ----
    VmaAllocationCreateInfo bar_alloc{};
    bar_alloc.usage = VMA_MEMORY_USAGE_AUTO;
    bar_alloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    bar_alloc.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

    if (vmaFindMemoryTypeIndexForBufferInfo(allocator, &buffer_probe, &bar_alloc, &buffer_bar_type_index) == VK_SUCCESS) {
        err = make_pool(buffer_bar_type_index, "buffer_bar", &buffer_bar_pool);
        LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't create BAR buffer pool.");
        bar_available = true;
    } else {
        buffer_bar_type_index = UINT32_MAX;
        bar_available = false;
        log_write("No DEVICE_LOCAL|HOST_VISIBLE memory type; BAR buffers fall back to upload pool.");
    }

    // ---- upload buffers ----
    VkBufferCreateInfo upload_probe = buffer_probe;
    upload_probe.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    VmaAllocationCreateInfo upload_alloc{};
    upload_alloc.usage = VMA_MEMORY_USAGE_AUTO;
    upload_alloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    upload_alloc.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    err = vmaFindMemoryTypeIndexForBufferInfo(allocator, &upload_probe, &upload_alloc, &upload_type_index);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't resolve memory type for upload pool.");
    err = make_pool(upload_type_index, "upload", &upload_pool);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't create upload pool.");

    // ---- readback buffers ----
    VkBufferCreateInfo readback_probe = buffer_probe;
    readback_probe.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    VmaAllocationCreateInfo readback_alloc{};
    readback_alloc.usage = VMA_MEMORY_USAGE_AUTO;
    readback_alloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    readback_alloc.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

    if (vmaFindMemoryTypeIndexForBufferInfo(allocator, &readback_probe, &readback_alloc, &readback_type_index) == VK_SUCCESS) {
        err = make_pool(readback_type_index, "readback", &readback_pool);
        LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't create readback pool.");
    } else {
        readback_type_index = UINT32_MAX;
        log_write("No HOST_VISIBLE|HOST_CACHED memory type; readback pool unavailable.");
    }

    return Ok;
}

void DeviceDriverVulkan::_allocator_pools_free()
{
    auto kill = [&](VmaPool& p) { if (p) { vmaDestroyPool(allocator, p); p = nullptr; } };
    kill(image_transient_pool);
    kill(image_persistent_pool);
    kill(image_texture_pool);
    kill(buffer_geometry_pool);
    kill(buffer_device_pool);
    kill(buffer_bar_pool);
    kill(upload_pool);
    kill(readback_pool);

    image_device_type_index = UINT32_MAX;
    buffer_device_type_index = UINT32_MAX;
    buffer_bar_type_index = UINT32_MAX;
    upload_type_index = UINT32_MAX;
    readback_type_index = UINT32_MAX;
    bar_available = false;
}

Error DeviceDriverVulkan::allocator_create()
{
    using enum Error;

    VmaAllocatorCreateInfo allocator_ci{};
    allocator_ci.physicalDevice = physical_device;
    allocator_ci.device = device;
    allocator_ci.instance = cd->instance_get();
    allocator_ci.vulkanApiVersion = physical_device_properties.apiVersion;
    const bool use_1_3_features = physical_device_properties.apiVersion >= VK_API_VERSION_1_3;
    if (use_1_3_features) {
        allocator_ci.flags |= VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE5_BIT;
    }
	if (/*buffer_device_address_support*/ true) {
		allocator_ci.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	}
    if (enabled_device_extension_names.contains(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME)) {
        allocator_ci.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    }

    VkResult err = vmaCreateAllocator(&allocator_ci, &allocator);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't create Vulkan memory allocator.");

    return _allocator_pools_create();
}

void DeviceDriverVulkan::allocator_free()
{
    if (allocator) {
        _allocator_pools_free();
        vmaDestroyAllocator(allocator);
        allocator = nullptr;
    }
}

/****************/
/**** IMAGES ****/
/****************/

DeviceDriverVulkan::Image DeviceDriverVulkan::_image_create(const ImageCreateInfo& p_ci, VkExtent2D p_extent)
{
    using enum Error;
    
    Image image;
    image.extent = p_extent;
    image.format = p_ci.format;
    image.aspect= p_ci.aspect;
    image.mip_levels = p_ci.mip_levels;
    image.layers = p_ci.layers;
    
    VkImageCreateInfo image_ci{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    image_ci.flags = 0;
    image_ci.imageType = VK_IMAGE_TYPE_2D;
    image_ci.format = p_ci.format;
    image_ci.extent = { p_extent.width, p_extent.height, 1 };
    image_ci.mipLevels = p_ci.mip_levels;
    image_ci.arrayLayers = p_ci.layers;
    image_ci.samples = p_ci.samples;
    image_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_ci.usage = p_ci.usage;
    image_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    VkResult err = vkCreateImage(device, &image_ci, nullptr, &image.image);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, {}, "Couldn't create Vulkan image.");

    VkImageMemoryRequirementsInfo2 req_info{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2 };
    req_info.image = image.image;
    VkMemoryDedicatedRequirements ded{ VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS };
    VkMemoryRequirements2 req2{ VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
    req2.pNext = &ded;
    vkGetImageMemoryRequirements2(device, &req_info, &req2);

    image.mem_req = req2.memoryRequirements;
    image.requires_dedicated = ded.requiresDedicatedAllocation == VK_TRUE;
    image.prefers_dedicated = ded.prefersDedicatedAllocation == VK_TRUE;

    set_object_name(VK_OBJECT_TYPE_IMAGE, (uint64_t)image.image, p_ci.name);
    return image;
}

Error DeviceDriverVulkan::_image_bind(Image& r_image, VmaAllocation p_allocation)
{
    using enum Error;

    VkResult err = vmaBindImageMemory(allocator, p_allocation, r_image.image);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't bind VMA image memory.");

    r_image.allocation = p_allocation;
    return Ok;
}

Error DeviceDriverVulkan::_image_create_view(Image& r_image)
{
    using enum Error;

    VkImageViewCreateInfo view_ci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    view_ci.image = r_image.image;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = r_image.format;
    view_ci.subresourceRange.aspectMask = r_image.aspect;
    view_ci.subresourceRange.baseMipLevel = 0;
    view_ci.subresourceRange.levelCount = r_image.mip_levels;
    view_ci.subresourceRange.baseArrayLayer = 0;
    view_ci.subresourceRange.layerCount = r_image.layers;

    VkResult err = vkCreateImageView(device, &view_ci, nullptr, &r_image.image_view);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't create Vulkan image view.");

    return Ok;
}

VkImageView DeviceDriverVulkan::_image_create_mip_view(const Image& p_image, uint32_t p_mip)
{
    VkImageViewCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    ci.image = p_image.image;
    ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ci.format = p_image.format;
    ci.subresourceRange = { (VkImageAspectFlags)p_image.aspect, p_mip, 1, 0, 1 };
    VkImageView view = VK_NULL_HANDLE;
    vkCreateImageView(device, &ci, nullptr, &view);
    return view;
}

DeviceDriverVulkan::Image DeviceDriverVulkan::image_create_dedicated(const ImageCreateInfo& p_ci, VkExtent2D p_extent)
{
    using enum Error;

    Image image = _image_create(p_ci, p_extent);
    LUMEN_ERR_FAIL_COND_V(image.image == VK_NULL_HANDLE, {});

    VmaAllocationCreateInfo alloc_ci{};
    alloc_ci.usage = VMA_MEMORY_USAGE_UNKNOWN;
    alloc_ci.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    const uint32_t pool_type = _pool_memory_type(p_ci.pool);
    const bool type_ok = pool_type != UINT32_MAX && (image.mem_req.memoryTypeBits & (1u << pool_type)) != 0;
    bool pooled = p_ci.pool != nullptr && type_ok && !image.requires_dedicated && !image.prefers_dedicated;

    if (pooled) alloc_ci.pool = p_ci.pool;
    else alloc_ci.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    VmaAllocation allocation = nullptr;
    VkResult err = vmaAllocateMemoryForImage(allocator, image.image, &alloc_ci, &allocation, nullptr);
    if (err != VK_SUCCESS && pooled) {
        log_write("Transient image pool exhausted for '%s', falling back to dedicated.", p_ci.name ? p_ci.name : "<unnamed>");
        alloc_ci.pool = nullptr;
        alloc_ci.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        err = vmaAllocateMemoryForImage(allocator, image.image, &alloc_ci, &allocation, nullptr);
    }
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, {}, "Couldn't allocate image memory.");

    Error e = _image_bind(image, allocation);
    LUMEN_ERR_FAIL_COND_V(e != Ok, {});

    e = _image_create_view(image);
    LUMEN_ERR_FAIL_COND_V(e != Ok, {});

    if (p_ci.usage & VK_IMAGE_USAGE_SAMPLED_BIT) image.bindless_sampled = bindless_heap_alloc_sampled(image.image_view);

    if (p_ci.usage & VK_IMAGE_USAGE_STORAGE_BIT) {
        image.mip_views.resize(image.mip_levels);
        image.mip_storage_slots.resize(image.mip_levels);
        for (uint32_t mm = 0; mm < image.mip_levels; mm++) {
            image.mip_views[mm] = _image_create_mip_view(image, mm);
            image.mip_storage_slots[mm] = bindless_heap_alloc_storage(image.mip_views[mm]);
        }
        image.bindless_storage = image.mip_storage_slots[0];
    }

    return image;
}

void DeviceDriverVulkan::image_free(Image& r_image)
{
    if (r_image.image_view) {
        vkDestroyImageView(device, r_image.image_view, nullptr);
        r_image.image_view = VK_NULL_HANDLE;
    }
    if (r_image.allocation) {
        vmaDestroyImage(allocator, r_image.image, r_image.allocation);
        r_image.image= VK_NULL_HANDLE;
        r_image.allocation = nullptr;
    }
    if (r_image.bindless_sampled != UINT32_MAX) {
        bindless_heap_free_sampled(r_image.bindless_sampled);
        r_image.bindless_sampled = UINT32_MAX;
    }
    if (r_image.bindless_storage != UINT32_MAX && r_image.mip_storage_slots.empty()) bindless_heap_free_storage(r_image.bindless_storage);
    r_image.bindless_storage = UINT32_MAX;
    for (uint32_t m = 0; m < (uint32_t)r_image.mip_views.size(); m++) {
        if (m < r_image.mip_storage_slots.size() && r_image.mip_storage_slots[m] != UINT32_MAX) bindless_heap_free_storage(r_image.mip_storage_slots[m]);
        if (r_image.mip_views[m]) vkDestroyImageView(device, r_image.mip_views[m], nullptr);
    }
    r_image.mip_views.clear();
    r_image.mip_storage_slots.clear();
    r_image.state = {};
}

void DeviceDriverVulkan::image_clear(Image& r_image, const VkClearColorValue& p_color)
{
    CommandPool pool = command_pool_create(cd->graphics_queue_family, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    VkCommandBuffer cmd = command_buffer_create(pool);
    command_buffer_begin(cmd, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VkImageSubresourceRange range{};
    range.aspectMask = (VkImageAspectFlags)r_image.aspect;
    range.baseMipLevel = 0;
    range.levelCount = r_image.mip_levels;
    range.baseArrayLayer = 0;
    range.layerCount = r_image.layers;

    VkImageMemoryBarrier2 to_dst{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    to_dst.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    to_dst.srcAccessMask = 0;
    to_dst.dstStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT;
    to_dst.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    to_dst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_dst.image = r_image.image;
    to_dst.subresourceRange = range;

    VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &to_dst;
    vkCmdPipelineBarrier2(cmd, &dep);

    vkCmdClearColorImage(cmd, r_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &p_color, 1, &range);

    command_buffer_end(cmd);

    VkCommandBufferSubmitInfo cmd_si{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
    cmd_si.commandBuffer = cmd;
    VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmd_si;

    VkFence fence = fence_create(false);
    vkQueueSubmit2(queue_families[cd->graphics_queue_family][0].queue, 1, &submit, fence);
    fence_wait(fence, UINT64_MAX);
    fence_free(fence);
    command_pool_free(pool);

    r_image.state.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    r_image.state.stage = VK_PIPELINE_STAGE_2_CLEAR_BIT;
    r_image.state.access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
}

DeviceDriverVulkan::Image DeviceDriverVulkan::image_create_texture(const void* p_rgba, uint32_t p_width, uint32_t p_height, const char* p_name)
{
    using enum Error;

    ImageCreateInfo ci{};
    ci.format = VK_FORMAT_R8G8B8A8_UNORM;
    ci.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    ci.mip_levels = 1;
    ci.layers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.pool = image_texture_pool;
    ci.name = p_name;

    Image image = image_create_dedicated(ci, { p_width, p_height });
    LUMEN_ERR_FAIL_COND_V(image.image == VK_NULL_HANDLE, {});

    const VkDeviceSize bytes = (VkDeviceSize)p_width * p_height * 4;

    BufferCreateInfo staging_ci{};
    staging_ci.size = bytes;
    staging_ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    staging_ci.host_visible = true;
    staging_ci.pool = upload_pool;
    staging_ci.name = "texture_staging";
    Buffer staging = buffer_create(staging_ci);
    LUMEN_ERR_FAIL_COND_V(!staging.buffer, {});
    buffer_update(staging, p_rgba, bytes, 0);
    buffer_flush(staging, 0, bytes);

    CommandPool pool = command_pool_create(cd->graphics_queue_family, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    VkCommandBuffer cmd = command_buffer_create(pool);
    command_buffer_begin(cmd, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    auto barrier = [&](VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 ss, VkAccessFlags2 sa, VkPipelineStageFlags2 ds, VkAccessFlags2 da) {
        VkImageMemoryBarrier2 b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        b.srcStageMask = ss; b.srcAccessMask = sa;
        b.dstStageMask = ds; b.dstAccessMask = da;
        b.oldLayout = from; b.newLayout = to;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image.image;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        vkCmdPipelineBarrier2(cmd, &dep);
    };

    barrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent = { p_width, p_height, 1 };
    vkCmdCopyBufferToImage(cmd, staging.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    command_buffer_end(cmd);
    VkCommandBufferSubmitInfo cmd_si{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
    cmd_si.commandBuffer = cmd;
    VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmd_si;

    VkFence fence = fence_create(false);
    vkQueueSubmit2(queue_families[cd->graphics_queue_family][0].queue, 1, &submit, fence);
    fence_wait(fence, UINT64_MAX);

    image.state.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image.state.stage  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    image.state.access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;

    fence_free(fence);
    command_pool_free(pool);
    buffer_free(staging);
    
    return image;
}

static uint32_t _bc_block_bytes(VkFormat p_format)
{
    switch (p_format) {
        case VK_FORMAT_BC4_UNORM_BLOCK: return 8;
        case VK_FORMAT_BC5_UNORM_BLOCK: return 16;
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:  return 16;
        default:                        return 16;
    }
}

DeviceDriverVulkan::Image DeviceDriverVulkan::image_create_texture_compressed(VkFormat p_format, uint32_t p_width, uint32_t p_height, uint32_t p_mip_count, const void* p_blocks, VkDeviceSize p_blocks_size, const char* p_name)
{
    using enum Error;

    const uint32_t mip_count = p_mip_count ? p_mip_count : 1;
    const uint32_t block_bytes = _bc_block_bytes(p_format);

    ImageCreateInfo ci{};
    ci.format = p_format;
    ci.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    ci.mip_levels = mip_count;
    ci.layers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.pool = image_texture_pool;
    ci.name = p_name;

    Image image = image_create_dedicated(ci, { p_width, p_height }); // auto-allocs bindless_sampled
    LUMEN_ERR_FAIL_COND_V(image.image == VK_NULL_HANDLE, {});

    BufferCreateInfo staging_ci{};
    staging_ci.size = p_blocks_size;
    staging_ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    staging_ci.host_visible = true;
    staging_ci.pool = upload_pool;
    staging_ci.name = "texture_bc_staging";
    Buffer staging = buffer_create(staging_ci);
    LUMEN_ERR_FAIL_COND_V(!staging.buffer, {});
    buffer_update(staging, p_blocks, p_blocks_size, 0);
    buffer_flush(staging, 0, p_blocks_size);

    // One copy region per mip. Mips are concatenated tightly in blob order, no
    // offset table — derive each mip's byte size from format + halved extent.
    // The halving MUST match the cooker's chain (max(1, dim>>1)).
    std::vector<VkBufferImageCopy> regions(mip_count);
    VkDeviceSize offset = 0;
    uint32_t mw = p_width, mh = p_height;
    for (uint32_t m = 0; m < mip_count; ++m) {
        const uint32_t bx = (mw + 3) / 4;
        const uint32_t by = (mh + 3) / 4;

        VkBufferImageCopy& r = regions[m];
        r = {};
        r.bufferOffset = offset;
        r.bufferRowLength = 0;    // tightly packed
        r.bufferImageHeight = 0;
        r.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, m, 0, 1 };
        r.imageOffset = { 0, 0, 0 };
        r.imageExtent = { mw, mh, 1 };

        offset += (VkDeviceSize)bx * by * block_bytes;
        mw = mw > 1 ? mw >> 1 : 1;
        mh = mh > 1 ? mh >> 1 : 1;
    }

    if (offset != p_blocks_size)
        log_write("image_create_texture_compressed: derived %llu != blob %llu for '%s'",
                  (unsigned long long)offset, (unsigned long long)p_blocks_size, p_name ? p_name : "<unnamed>");

    CommandPool pool = command_pool_create(cd->graphics_queue_family, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    VkCommandBuffer cmd = command_buffer_create(pool);
    command_buffer_begin(cmd, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    auto barrier = [&](VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 ss, VkAccessFlags2 sa, VkPipelineStageFlags2 ds, VkAccessFlags2 da) {
        VkImageMemoryBarrier2 b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        b.srcStageMask = ss; b.srcAccessMask = sa;
        b.dstStageMask = ds; b.dstAccessMask = da;
        b.oldLayout = from; b.newLayout = to;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image.image;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_count, 0, 1 };
        VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        vkCmdPipelineBarrier2(cmd, &dep);
    };

    barrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    vkCmdCopyBufferToImage(cmd, staging.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mip_count, regions.data());
    barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    command_buffer_end(cmd);
    VkCommandBufferSubmitInfo cmd_si{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
    cmd_si.commandBuffer = cmd;
    VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmd_si;

    VkFence fence = fence_create(false);
    vkQueueSubmit2(queue_families[cd->graphics_queue_family][0].queue, 1, &submit, fence);
    fence_wait(fence, UINT64_MAX);

    image.state.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image.state.stage  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    image.state.access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;

    fence_free(fence);
    command_pool_free(pool);
    buffer_free(staging);

    return image;
}

/*****************/
/**** BUFFERS ****/
/*****************/

VkDeviceSize DeviceDriverVulkan::_next_power_of_2(VkDeviceSize v)
{
    if (v <= 1) return 1;
    --v;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return ++v;
}

DeviceDriverVulkan::Buffer DeviceDriverVulkan::buffer_create(const BufferCreateInfo& p_ci)
{
    LUMEN_ERR_FAIL_COND_V_MSG(!p_ci.device_local && !p_ci.host_visible, {}, "Buffer must be device_local, host_visible, or both.");
    LUMEN_ERR_FAIL_COND_V_MSG(p_ci.cpu_read && !p_ci.host_visible, {}, "cpu_read requires host_visible.");

    using enum Error;

    Buffer buffer;
    buffer.name = p_ci.name;

    buffer.device_local = p_ci.device_local;
    buffer.host_visible = p_ci.host_visible;
    buffer.cpu_read = p_ci.cpu_read;
    buffer.pool = p_ci.pool;

    VkBufferUsageFlags usage = p_ci.usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    buffer.usage = usage;

    VkDeviceSize alloc_size = p_ci.size ? p_ci.size : 1;

    VkBufferCreateInfo buffer_ci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    buffer_ci.size = alloc_size;
    buffer_ci.usage = usage;
    buffer_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_ci{};
    alloc_ci.usage = VMA_MEMORY_USAGE_UNKNOWN;
    if (p_ci.device_local) alloc_ci.requiredFlags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    if (p_ci.host_visible) {
        alloc_ci.requiredFlags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        alloc_ci.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
        if (p_ci.cpu_read) {
            alloc_ci.requiredFlags |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
            alloc_ci.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        } else {
            alloc_ci.preferredFlags |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            alloc_ci.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        }
    }

    bool pooled = false;
    const uint32_t pool_type = _pool_memory_type(p_ci.pool);
    if (p_ci.pool != nullptr && pool_type != UINT32_MAX) {
        VkPhysicalDeviceMemoryProperties mem_props{};
        vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);
        const VkMemoryPropertyFlags pool_flags = mem_props.memoryTypes[pool_type].propertyFlags;

        VkDeviceBufferMemoryRequirements dev_req{ VK_STRUCTURE_TYPE_DEVICE_BUFFER_MEMORY_REQUIREMENTS };
        dev_req.pCreateInfo = &buffer_ci;
        VkMemoryRequirements2 req2{ VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
        vkGetDeviceBufferMemoryRequirements(device, &dev_req, &req2);

        const bool bits_ok = (req2.memoryRequirements.memoryTypeBits & (1u << pool_type)) != 0;
        const bool props_ok = (pool_flags & alloc_ci.requiredFlags) == alloc_ci.requiredFlags;
        pooled = bits_ok && props_ok;
    }
    if (pooled) alloc_ci.pool = p_ci.pool;

    VmaAllocationInfo alloc_info{};
    VkResult err = vmaCreateBuffer(allocator, &buffer_ci, &alloc_ci, &buffer.buffer, &buffer.allocation, &alloc_info);
    if (err != VK_SUCCESS && pooled) {
        log_write("Device buffer pool exhausted for '%s', falling back to default allocator.", p_ci.name ? p_ci.name : "<unnamed>");
        alloc_ci.pool = nullptr;
        err = vmaCreateBuffer(allocator, &buffer_ci, &alloc_ci, &buffer.buffer, &buffer.allocation, &alloc_info);
    }
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, {}, "Couldn't create Vulkan buffer.");

    buffer.capacity = alloc_size;
    buffer.size = p_ci.size;
    buffer.mapped = alloc_info.pMappedData;
    
    VkMemoryPropertyFlags mem_props = 0;
    vmaGetAllocationMemoryProperties(allocator, buffer.allocation, &mem_props);
    buffer.coherent = (mem_props & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;

    VkBufferDeviceAddressInfo addr_info{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
    addr_info.buffer = buffer.buffer;
    buffer.device_address = vkGetBufferDeviceAddress(device, &addr_info);

    set_object_name(VK_OBJECT_TYPE_BUFFER, (uint64_t)buffer.buffer, p_ci.name);
    return buffer;
}

void DeviceDriverVulkan::buffer_free(Buffer& r_buffer)
{
    if (r_buffer.buffer) {
        vmaDestroyBuffer(allocator, r_buffer.buffer, r_buffer.allocation);
        r_buffer.buffer = VK_NULL_HANDLE;
        r_buffer.allocation = nullptr;
    }
    r_buffer.mapped = nullptr;
    r_buffer.device_address = 0;
    r_buffer.state = {};
}

Error DeviceDriverVulkan::buffer_ensure_capacity(Buffer& r_buffer, VkDeviceSize p_size)
{
    using enum Error;

    if (r_buffer.capacity >= p_size) {
        r_buffer.size = p_size;
        return Ok;
    }

    VkDeviceSize new_cap = _next_power_of_2(p_size);

    BufferCreateInfo buffer_ci{};
    buffer_ci.size = new_cap;
    buffer_ci.usage = r_buffer.usage;
    buffer_ci.device_local = r_buffer.device_local;
    buffer_ci.host_visible = r_buffer.host_visible;
    buffer_ci.cpu_read = r_buffer.cpu_read;
    buffer_ci.pool = r_buffer.pool;
    buffer_ci.name = r_buffer.name;

    Buffer fresh = buffer_create(buffer_ci);
    LUMEN_ERR_FAIL_COND_V_MSG(!fresh.buffer, Failed, "Buffer ensure capacity reallocation failed.");
    fresh.size = p_size;

    buffer_free(r_buffer);
    r_buffer = fresh;
    return Ok;
}

Error DeviceDriverVulkan::buffer_update(Buffer& r_buffer, const void* p_data, VkDeviceSize p_size, VkDeviceSize p_offset)
{
    using enum Error;
    LUMEN_ERR_FAIL_COND_V_MSG(!r_buffer.mapped, Failed, "Cannot update a non-host-visible buffer.");
    LUMEN_ERR_FAIL_COND_V_MSG(p_offset + p_size > r_buffer.capacity, Failed, "Buffer update of of range.");
    memcpy(static_cast<uint8_t*>(r_buffer.mapped) + p_offset, p_data, p_size);
    return Ok;
}

Error DeviceDriverVulkan::buffer_flush(Buffer& r_buffer, VkDeviceSize p_offset, VkDeviceSize p_size)
{
    using enum Error;
    if (r_buffer.coherent || !r_buffer.allocation) return Ok;
    VkResult err = vmaFlushAllocation(allocator, r_buffer.allocation, p_offset, p_size);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't flush buffer allocation.");
    return Ok;
}

Error DeviceDriverVulkan::buffer_invalidate(Buffer& r_buffer, VkDeviceSize p_offset, VkDeviceSize p_size)
{
    using enum Error;
    if (r_buffer.coherent || !r_buffer.allocation) return Ok;
    VkResult err = vmaInvalidateAllocation(allocator, r_buffer.allocation, p_offset, p_size);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't invalidate buffer allocation.");
    return Ok;
}

void DeviceDriverVulkan::command_bind_index_buffer(VkCommandBuffer p_cmd, VkBuffer p_buffer, VkDeviceSize p_offset, VkIndexType p_index_type)
{
    vkCmdBindIndexBuffer(p_cmd, p_buffer, p_offset, p_index_type);
}

void DeviceDriverVulkan::command_copy_buffer(VkCommandBuffer p_cmd, const Buffer& p_src, const Buffer& p_dst, VkDeviceSize p_size, VkDeviceSize p_src_offset, VkDeviceSize p_dst_offset)
{
    VkBufferCopy region{};
    region.srcOffset = p_src_offset;
    region.dstOffset = p_dst_offset;
    region.size = p_size;
    vkCmdCopyBuffer(p_cmd, p_src.buffer, p_dst.buffer, 1, &region);
}

void DeviceDriverVulkan::command_fill_buffer(VkCommandBuffer p_cmd, const Buffer& p_buffer, uint32_t p_value, VkDeviceSize p_offset, VkDeviceSize p_size)
{
    vkCmdFillBuffer(p_cmd, p_buffer.buffer, p_offset, p_size, p_value);
}

void DeviceDriverVulkan::command_update_buffer(VkCommandBuffer p_cmd, const Buffer& p_buffer, const void* p_data, VkDeviceSize p_size, VkDeviceSize p_offset)
{
    vkCmdUpdateBuffer(p_cmd, p_buffer.buffer, p_offset, p_size, p_data);
}

Error DeviceDriverVulkan::buffer_upload_batch(const BufferUpload* p_uploads, uint32_t p_count)
{
    using enum Error;
    if (p_count == 0) return Ok;

    VkDeviceSize total = 0;
    for (uint32_t i = 0; i < p_count; i++) total += p_uploads[i].size;
    if (total == 0) return Ok;

    BufferCreateInfo staging_ci{};
    staging_ci.size = total;
    staging_ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    staging_ci.host_visible = true;
    staging_ci.pool = upload_pool;
    staging_ci.name = "buffer_upload_staging";
    Buffer staging = buffer_create(staging_ci);
    LUMEN_ERR_FAIL_COND_V_MSG(!staging.buffer, Failed, "Couldn't create upload staging buffer.");

    std::vector<VkDeviceSize> src_offsets(p_count);
    VkDeviceSize src_off = 0;
    for (uint32_t i = 0; i < p_count; i++) {
        src_offsets[i] = src_off;
        buffer_update(staging, p_uploads[i].data, p_uploads[i].size, src_off);
        src_off += p_uploads[i].size;
    }
    buffer_flush(staging, 0, total);

    CommandPool pool = command_pool_create(cd->graphics_queue_family, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    VkCommandBuffer cmd = command_buffer_create(pool);
    command_buffer_begin(cmd, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    for (uint32_t i = 0; i < p_count; i++) command_copy_buffer(cmd, staging, *p_uploads[i].dst, p_uploads[i].size, src_offsets[i], p_uploads[i].offset);

    // Make the transfer writes available/visible to later shader reads on this queue.
    VkMemoryBarrier2 mb{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
    mb.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    mb.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(cmd, &dep);

    command_buffer_end(cmd);

    VkCommandBufferSubmitInfo cmd_si{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
    cmd_si.commandBuffer = cmd;
    VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmd_si;

    VkFence fence = fence_create(false);
    vkQueueSubmit2(queue_families[cd->graphics_queue_family][0].queue, 1, &submit, fence);
    fence_wait(fence, UINT64_MAX);

    fence_free(fence);
    command_pool_free(pool);
    buffer_free(staging);

    for (uint32_t i = 0; i < p_count; i++) {
        p_uploads[i].dst->state.stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        p_uploads[i].dst->state.access = VK_ACCESS_2_SHADER_READ_BIT;
    }
    return Ok;
}

Error DeviceDriverVulkan::buffer_copy_batch(const BufferCopy* p_copies, uint32_t p_count)
{
    using enum Error;
    if (p_count == 0) return Ok;

    CommandPool pool = command_pool_create(cd->graphics_queue_family, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    VkCommandBuffer cmd = command_buffer_create(pool);
    command_buffer_begin(cmd, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    for (uint32_t i = 0; i < p_count; i++)
        command_copy_buffer(cmd, *p_copies[i].src, *p_copies[i].dst, p_copies[i].size, p_copies[i].src_offset, p_copies[i].dst_offset);

    VkMemoryBarrier2 mb{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
    mb.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    mb.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
    VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(cmd, &dep);

    command_buffer_end(cmd);

    VkCommandBufferSubmitInfo cmd_si{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
    cmd_si.commandBuffer = cmd;
    VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmd_si;

    VkFence fence = fence_create(false);
    vkQueueSubmit2(queue_families[cd->graphics_queue_family][0].queue, 1, &submit, fence);
    fence_wait(fence, UINT64_MAX);

    fence_free(fence);
    command_pool_free(pool);

    for (uint32_t i = 0; i < p_count; i++) p_copies[i].dst->state.stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    return Ok;
}

void DeviceDriverVulkan::command_copy_image_to_buffer(VkCommandBuffer p_cmd, const Image& p_image, const Buffer& p_buffer, VkExtent2D p_extent)
{
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { p_extent.width, p_extent.height, 1 };
    vkCmdCopyImageToBuffer(p_cmd, p_image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, p_buffer.buffer, 1, &region);
}

DeviceDriverVulkan::BufferRing DeviceDriverVulkan::buffer_ring_create(const BufferCreateInfo& p_ci, uint32_t p_frame_count)
{
    using enum Error;
    BufferRing ring;
    for (uint32_t i = 0; i < p_frame_count; i++) {
        buffer_create(p_ci);
    }
    return ring;
}

void DeviceDriverVulkan::buffer_ring_free(BufferRing& r_buffer_ring)
{
    for (Buffer b : r_buffer_ring.buffers) {
        buffer_free(b);
    }
}

/*****************/
/**** SAMPLER ****/
/*****************/

DeviceDriverVulkan::Sampler DeviceDriverVulkan::sampler_create(const SamplerCreateInfo& p_ci)
{
    using enum Error;

    VkSamplerCreateInfo sampler_ci{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sampler_ci.magFilter = p_ci.filter;
    sampler_ci.minFilter = p_ci.filter;
    sampler_ci.addressModeU = p_ci.address_mode;
    sampler_ci.addressModeV = p_ci.address_mode;
    sampler_ci.addressModeW = p_ci.address_mode;
    sampler_ci.anisotropyEnable = p_ci.anisotropy > 1.0f;
    sampler_ci.maxAnisotropy = p_ci.anisotropy;
    sampler_ci.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_ci.unnormalizedCoordinates = false;
    sampler_ci.compareEnable = p_ci.compare;
    sampler_ci.compareOp = p_ci.compare_op;
    sampler_ci.mipmapMode = p_ci.mipmap_mode;
    sampler_ci.mipLodBias = 0.0f;
    sampler_ci.minLod = 0.0f;
    sampler_ci.maxLod = VK_LOD_CLAMP_NONE;

    Sampler sampler;
    VkResult err = vkCreateSampler(device, &sampler_ci, nullptr, &sampler.sampler);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, {}, "Couldn't create Vulkan sampler.");

    sampler.bindless_sampler = bindless_heap_alloc_sampler(sampler.sampler);

    set_object_name(VK_OBJECT_TYPE_SAMPLER, (uint64_t)sampler.sampler, p_ci.name);
    return sampler;
}

void DeviceDriverVulkan::sampler_free(Sampler& r_sampler)
{
    if (r_sampler.sampler) {
        vkDestroySampler(device, r_sampler.sampler, nullptr);
        r_sampler.sampler = VK_NULL_HANDLE;
    }
    if (r_sampler.bindless_sampler != UINT32_MAX) {
        bindless_heap_free_sampler(r_sampler.bindless_sampler);
        r_sampler.bindless_sampler = UINT32_MAX;
    }
}

/****************/
/**** FENCES ****/
/****************/

VkFence DeviceDriverVulkan::fence_create(bool p_signaled)
{
    VkFenceCreateInfo fence_ci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fence_ci.flags = p_signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;
    
    VkFence fence;
    VkResult err = vkCreateFence(device, &fence_ci, nullptr, &fence);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, VK_NULL_HANDLE, "Couldn't create Vulkan fence.");
    
    return fence;
}

void DeviceDriverVulkan::fence_free(VkFence& r_fence)
{
    if (r_fence) {
        vkDestroyFence(device, r_fence, nullptr);
        r_fence = VK_NULL_HANDLE;
    }
}

Error DeviceDriverVulkan::fence_wait(VkFence p_fence, uint64_t p_timeout)
{
    using enum Error;
    VkResult err = vkWaitForFences(device, 1, &p_fence, VK_TRUE, p_timeout);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't wait for Vulkan fence.");
    return Ok;
}

Error DeviceDriverVulkan::fence_reset(VkFence p_fence)
{
    using enum Error;
    VkResult err = vkResetFences(device, 1, &p_fence);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't reset Vulkan fence.");
    return Ok;
}

/********************/
/**** SEMAPHORES ****/
/********************/

VkSemaphore DeviceDriverVulkan::semaphore_create()
{
    VkSemaphoreCreateInfo semaphore_ci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    semaphore_ci.pNext = nullptr;
    semaphore_ci.flags = 0;
    
    VkSemaphore semaphore;
    VkResult err = vkCreateSemaphore(device, &semaphore_ci, nullptr, &semaphore);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, VK_NULL_HANDLE, "Couldn't create Vulkan semaphore.");
    
    return semaphore;
}

void DeviceDriverVulkan::semaphore_free(VkSemaphore& r_semaphore)
{
    if (r_semaphore) {
        vkDestroySemaphore(device, r_semaphore, nullptr);
        r_semaphore = VK_NULL_HANDLE;
    }
}

/***************/
/**** QUERY ****/
/***************/

uint32_t DeviceDriverVulkan::timestamp_valid_bits(uint32_t p_queue_family_index)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
    if (p_queue_family_index >= count) return 0;

    std::vector<VkQueueFamilyProperties> props(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, props.data());
    return props[p_queue_family_index].timestampValidBits;
}

DeviceDriverVulkan::QueryPool DeviceDriverVulkan::query_pool_create_timestamp(uint32_t p_query_count)
{
    VkQueryPoolCreateInfo query_pool_ci{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
    query_pool_ci.queryType = VK_QUERY_TYPE_TIMESTAMP;
    query_pool_ci.queryCount = p_query_count;
    query_pool_ci.pipelineStatistics = 0;

    QueryPool query_pool;
    query_pool.capacity = p_query_count;
    VkResult err = vkCreateQueryPool(device, &query_pool_ci, nullptr, &query_pool.pool);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, {}, "Couldn't create Vulkan timestamp query pool.");

    return query_pool;
}

DeviceDriverVulkan::QueryPool DeviceDriverVulkan::query_pool_create_occlusion(uint32_t p_query_count)
{
    VkQueryPoolCreateInfo query_pool_ci{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
    query_pool_ci.queryType = VK_QUERY_TYPE_OCCLUSION;
    query_pool_ci.queryCount = p_query_count;

    QueryPool query_pool;
    query_pool.capacity = p_query_count;
    VkResult err = vkCreateQueryPool(device, &query_pool_ci, nullptr, &query_pool.pool);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, {}, "Couldn't create Vulkan occlusion query pool.");
    return query_pool;
}

DeviceDriverVulkan::QueryPool DeviceDriverVulkan::query_pool_create_pipeline_statistics(uint32_t p_query_count, VkQueryPipelineStatisticFlags p_stats)
{
    VkQueryPoolCreateInfo query_pool_ci{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
    query_pool_ci.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
    query_pool_ci.queryCount = p_query_count;
    query_pool_ci.pipelineStatistics = p_stats;

    QueryPool query_pool;
    query_pool.capacity = p_query_count;
    VkResult err = vkCreateQueryPool(device, &query_pool_ci, nullptr, &query_pool.pool);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, {}, "Couldn't create Vulkan pipeline-statistics query query_pool.");
    return query_pool;
}

void DeviceDriverVulkan::query_pool_free(QueryPool& r_query_pool)
{
    if (r_query_pool.pool) {
        vkDestroyQueryPool(device, r_query_pool.pool, nullptr);
        r_query_pool.pool = VK_NULL_HANDLE;
    }
}

Error DeviceDriverVulkan::query_pool_get_results(const QueryPool& p_query_pool, uint32_t p_first, uint32_t p_count, uint64_t* r_results, uint32_t p_stride_u64)
{
    using enum Error;
    if (p_count == 0) return Ok;
    const VkDeviceSize stride = (VkDeviceSize)p_stride_u64 * sizeof(uint64_t);
    VkResult err = vkGetQueryPoolResults(device, p_query_pool.pool, p_first, p_count, (size_t)p_count * stride, r_results, stride, VK_QUERY_RESULT_64_BIT);
    if (err == VK_NOT_READY) return Failed;
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't get Vulkan query pool results.");
    return Ok;
}

void DeviceDriverVulkan::command_reset_query_pool(VkCommandBuffer p_cmd, const QueryPool& p_query_pool, uint32_t p_first, uint32_t p_count)
{
    vkCmdResetQueryPool(p_cmd, p_query_pool.pool, p_first, p_count);
}

void DeviceDriverVulkan::command_write_timestamp(VkCommandBuffer p_cmd, const QueryPool& p_query_pool, VkPipelineStageFlags2 p_stage, uint32_t p_index)
{
    vkCmdWriteTimestamp2(p_cmd, p_stage, p_query_pool.pool, p_index);
}

void DeviceDriverVulkan::command_begin_query(VkCommandBuffer p_cmd, const QueryPool& p_query_pool, uint32_t p_index, VkQueryControlFlags p_flags)
{
    vkCmdBeginQuery(p_cmd, p_query_pool.pool, p_index, p_flags);
}

void DeviceDriverVulkan::command_end_query(VkCommandBuffer p_cmd, const QueryPool& p_query_pool, uint32_t p_index)
{
    vkCmdEndQuery(p_cmd, p_query_pool.pool, p_index);
}

/******************/
/**** COMMANDS ****/
/******************/

DeviceDriverVulkan::CommandPool DeviceDriverVulkan::command_pool_create(uint32_t p_queue_family_index, VkCommandBufferLevel p_buffer_level)
{
    VkCommandPoolCreateInfo cmd_pool_ci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    cmd_pool_ci.queueFamilyIndex = p_queue_family_index;
    cmd_pool_ci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    
    CommandPool cmd_pool;
    cmd_pool.buffer_level = p_buffer_level;
    VkResult err = vkCreateCommandPool(device, &cmd_pool_ci, nullptr, &cmd_pool.command_pool);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, {}, "Couldn't create Vulkan command pool.");

    return cmd_pool;
}

void DeviceDriverVulkan::command_pool_free(CommandPool& r_cmd_pool)
{
    if (r_cmd_pool.command_pool) {
        vkDestroyCommandPool(device, r_cmd_pool.command_pool, nullptr);
        r_cmd_pool.command_pool = VK_NULL_HANDLE;
    }
}

Error DeviceDriverVulkan::command_pool_reset(CommandPool& r_cmd_pool)
{
    using enum Error;
    VkResult err = vkResetCommandPool(device, r_cmd_pool.command_pool, 0);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't reset Vulkan command pool.");
    return Ok;
}

VkCommandBuffer DeviceDriverVulkan::command_buffer_create(CommandPool& p_cmd_pool)
{
    VkCommandBufferAllocateInfo cmd_buffer_ci{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cmd_buffer_ci.commandPool = p_cmd_pool.command_pool;
    cmd_buffer_ci.level = p_cmd_pool.buffer_level;
    cmd_buffer_ci.commandBufferCount = 1;

    VkCommandBuffer cmd_buffer;
    VkResult err = vkAllocateCommandBuffers(device, &cmd_buffer_ci, &cmd_buffer);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, VK_NULL_HANDLE, "Couldn't create Vulkan command buffer.");
    
    return cmd_buffer;
}

Error DeviceDriverVulkan::command_buffer_begin(VkCommandBuffer p_cmd_buffer, VkCommandBufferUsageFlags p_flags)
{
    using enum Error;
    VkCommandBufferBeginInfo begin_info{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    begin_info.flags = p_flags;
    VkResult err = vkBeginCommandBuffer(p_cmd_buffer, &begin_info);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't begin Vulkan command buffer.");
    return Ok;
}

Error DeviceDriverVulkan::command_buffer_end(VkCommandBuffer p_cmd_buffer)
{
    using enum Error;
    VkResult err = vkEndCommandBuffer(p_cmd_buffer);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't end Vulkan command buffer.");
    return Ok;
}

void DeviceDriverVulkan::command_render_set_viewport(VkCommandBuffer p_cmd, const std::vector<VkRect2D>& p_viewports)
{
    std::vector<VkViewport> viewports(p_viewports.size());
    for (uint32_t i = 0; i < p_viewports.size(); i++) {
        viewports[i] = {};
        viewports[i].x = (float)p_viewports[i].offset.x;
        viewports[i].y = (float)p_viewports[i].offset.y;
        viewports[i].width = (float)p_viewports[i].extent.width;
        viewports[i].height = (float)p_viewports[i].extent.height;
        viewports[i].minDepth = 0.0f;
        viewports[i].maxDepth = 1.0f;
    }
    vkCmdSetViewport(p_cmd, 0, (uint32_t)viewports.size(), viewports.data());
}

void DeviceDriverVulkan::command_render_set_scissor(VkCommandBuffer p_cmd, const std::vector<VkRect2D>& p_scissors)
{
    vkCmdSetScissor(p_cmd, 0, (uint32_t)p_scissors.size(), p_scissors.data());
}

void DeviceDriverVulkan::command_bind_push_constants(const VkCommandBuffer& p_cmd, uint32_t p_size, void* r_data, uint32_t p_offset)
{
    vkCmdPushConstants(p_cmd, bindless_heap.pipeline_layout, VK_SHADER_STAGE_ALL, p_offset, p_size, r_data);
}

void DeviceDriverVulkan::command_render_draw(VkCommandBuffer p_cmd, uint32_t p_vertex_count, uint32_t p_instance_count, uint32_t p_base_vertex, uint32_t p_first_instance)
{
    vkCmdDraw(p_cmd, p_vertex_count, p_instance_count, p_base_vertex, p_first_instance);
}

void DeviceDriverVulkan::command_render_draw_indexed(VkCommandBuffer p_cmd, uint32_t p_index_count, uint32_t p_instance_count, uint32_t p_first_index, int32_t p_vertex_offset, uint32_t p_first_instance)
{
    vkCmdDrawIndexed(p_cmd, p_index_count, p_instance_count, p_first_index, p_vertex_offset, p_first_instance);
}

void DeviceDriverVulkan::command_render_draw_indexed_indirect(VkCommandBuffer p_cmd, const Buffer& p_indirect_buffer, uint64_t p_offset, uint32_t p_draw_count, uint32_t p_stride)
{
    vkCmdDrawIndexedIndirect(p_cmd, p_indirect_buffer.buffer, p_offset, p_draw_count, p_stride);
}

void DeviceDriverVulkan::command_render_draw_indexed_indirect_count(VkCommandBuffer p_cmd, const Buffer& p_indirect_buffer, uint64_t p_offset, const Buffer& p_count_buffer, uint64_t p_count_buffer_offset, uint32_t p_max_draw_count, uint32_t p_stride)
{
    vkCmdDrawIndexedIndirectCount(p_cmd, p_indirect_buffer.buffer, p_offset, p_count_buffer.buffer, p_count_buffer_offset, p_max_draw_count, p_stride);
}

void DeviceDriverVulkan::command_render_draw_indirect(VkCommandBuffer p_cmd, const Buffer& p_indirect_buffer, uint64_t p_offset, uint32_t p_draw_count, uint32_t p_stride)
{
    vkCmdDrawIndirect(p_cmd, p_indirect_buffer.buffer, p_offset, p_draw_count, p_stride);
}

void DeviceDriverVulkan::command_render_draw_indirect_count(VkCommandBuffer p_cmd, const Buffer& p_indirect_buffer, uint64_t p_offset, const Buffer& p_count_buffer, uint64_t p_count_buffer_offset, uint32_t p_max_draw_count, uint32_t p_stride)
{
    vkCmdDrawIndirectCount(p_cmd, p_indirect_buffer.buffer, p_offset, p_count_buffer.buffer, p_count_buffer_offset, p_max_draw_count, p_stride);
}

/*******************/
/**** SWAPCHAIN ****/
/*******************/

bool DeviceDriverVulkan::_determine_swapchain_format(ContextDriverVulkan::Surface* r_surface, VkSurfaceFormatKHR &r_surface_format)
{    
    std::vector<VkSurfaceFormatKHR> surface_formats;
    uint32_t format_count = 0;
    VkResult err = vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, r_surface->surface, &format_count, nullptr);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, false, "Couldn't get Vulkan surface present modes.");

	surface_formats.resize(format_count);
	err = vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, r_surface->surface, &format_count, surface_formats.data());
	LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, false, "Couldn't get Vulkan surface present modes.");

    VkSurfaceFormatKHR surface_format = surface_formats[0];
    for (const auto& f : surface_formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surface_format = f;
            break;
        }
    }
    r_surface_format = surface_format;

    return true;
}

void DeviceDriverVulkan::_swapchain_release()
{
    for (VkFramebuffer& fb : swapchain.framebuffers) {
        framebuffer_free(fb);
    }

    for (Image& img : swapchain.images) {
        if (img.image_view) vkDestroyImageView(device, img.image_view, nullptr);
        img.image_view = VK_NULL_HANDLE;
    }
    swapchain.images.clear();

    swapchain.image_index = UINT_MAX;

    if (swapchain.swapchain) {
        vkDestroySwapchainKHR(device, swapchain.swapchain, nullptr);
        swapchain.swapchain = VK_NULL_HANDLE;
    }

    for (VkSemaphore& sem : swapchain.present_semaphores) {
        semaphore_free(sem);
    }

    swapchain.present_semaphores.clear();
}

Error DeviceDriverVulkan::swapchain_create(ContextDriverVulkan::Surface* r_surface)
{
    using enum Error;
    LUMEN_ERR_FAIL_COND_V(!r_surface, Failed);
    swapchain.surface = r_surface;

    VkSurfaceFormatKHR surface_format{};
    if (!_determine_swapchain_format(r_surface, surface_format)) {
        LUMEN_ERR_FAIL_COND_V_MSG(true, Failed, "Vulkan surface did not return any valid formats.");
    } else {
        swapchain.format = surface_format.format;
        swapchain.color_space = surface_format.colorSpace;
    }

    drivers::DeviceDriverVulkan::RenderPassCreateInfo render_pass_ci{};
    render_pass_ci.name = "swapchain_render_pass";
    RenderPassCreateInfo::Attachment color_attachment;
    color_attachment.format = swapchain.format;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.store_op = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.final_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    color_attachment.is_depth = false;
    render_pass_ci.attachments.push_back(color_attachment);
    swapchain.render_pass = render_pass_create(render_pass_ci);

    return Ok;
}

Error DeviceDriverVulkan::swapchain_resize(uint32_t p_desired_framebuffer_count)
{
    using enum Error;
    
    _swapchain_release();

    ContextDriverVulkan::Surface* surface = (ContextDriverVulkan::Surface*)(swapchain.surface);
    VkSurfaceCapabilitiesKHR surface_capabilities = {};
    VkResult err = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface->surface, &surface_capabilities);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't get Vulkan surface capabilities.");

    if (!swapchain.swapchain) {
        if (surface_capabilities.currentExtent.width == 0xFFFFFFFF) {
			surface_capabilities.currentExtent.width = std::clamp(surface->width, surface_capabilities.minImageExtent.width, surface_capabilities.maxImageExtent.width);
			surface_capabilities.currentExtent.height = std::clamp(surface->height, surface_capabilities.minImageExtent.height, surface_capabilities.maxImageExtent.height);
		}
    }

    VkExtent2D extent;
    if (surface_capabilities.currentExtent.width == 0xFFFFFFFF) {
        extent.width = std::clamp(surface->width, surface_capabilities.minImageExtent.width, surface_capabilities.maxImageExtent.width);
		extent.height = std::clamp(surface->height, surface_capabilities.minImageExtent.height, surface_capabilities.maxImageExtent.height);
    } else {
        extent = surface_capabilities.currentExtent;
        surface->width = extent.width;
        surface->height = extent.height;
    }

	if (surface->width == 0 || surface->height == 0) {
		return Failed;
	}

    std::vector<VkPresentModeKHR> present_modes;
    uint32_t present_modes_count = 0;
    err = vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface->surface, &present_modes_count, nullptr);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't get Vulkan surface present modes.");

	present_modes.resize(present_modes_count);
	err = vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface->surface, &present_modes_count, present_modes.data());
	LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't get Vulkan surface present modes.");

    VkPresentModeKHR present_mode = VkPresentModeKHR::VK_PRESENT_MODE_FIFO_KHR;
	std::string present_mode_name = "Enabled";
    if (surface->vsync_enabled) {
        present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
		present_mode_name = "Mailbox";
    } else {
        present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
		present_mode_name = "Disabled";
    }

    bool present_mode_available = false;
    for (auto mode : present_modes) {
        if (mode == present_mode) present_mode_available = true;
    }

	if (!present_mode_available) {
		surface->vsync_enabled = true;
		present_mode = VK_PRESENT_MODE_FIFO_KHR;
	}

	uint32_t desired_swapchain_images = std::max(p_desired_framebuffer_count, surface_capabilities.minImageCount);
	if (surface_capabilities.maxImageCount > 0) {
		desired_swapchain_images = std::min(desired_swapchain_images, surface_capabilities.maxImageCount);
	}

    VkSurfaceFormatKHR surface_format{};
    if (!_determine_swapchain_format(surface, surface_format)) {
        LUMEN_ERR_FAIL_COND_V_MSG(true, Failed, "Vulkan surface did not return any valid formats.");
    } else {
        swapchain.format = surface_format.format;
        swapchain.color_space = surface_format.colorSpace;
    }

    VkSwapchainCreateInfoKHR swap_ci{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    swap_ci.surface = surface->surface;
    swap_ci.minImageCount = desired_swapchain_images;
    swap_ci.imageFormat = swapchain.format;
    swap_ci.imageColorSpace = swapchain.color_space;
    swap_ci.imageExtent = extent;
    swap_ci.imageArrayLayers = 1;
    swap_ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    swap_ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swap_ci.preTransform = surface_capabilities.currentTransform;
    swap_ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swap_ci.presentMode = present_mode;
    swap_ci.clipped = VK_TRUE;

    err = vkCreateSwapchainKHR(device, &swap_ci, nullptr, &swapchain.swapchain);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't create Vulkan swapchain.");
    
    uint32_t image_count = 0;
    err = vkGetSwapchainImagesKHR(device, swapchain.swapchain, &image_count, nullptr);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't get Vulkan swapchain images.");
    
    std::vector<VkImage> raw_images(image_count);
    err = vkGetSwapchainImagesKHR(device, swapchain.swapchain, &image_count, raw_images.data());
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't get Vulkan swapchain images.");

    VkImageViewCreateInfo view_ci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = swapchain.format;
    view_ci.components.r = VK_COMPONENT_SWIZZLE_R;
    view_ci.components.g = VK_COMPONENT_SWIZZLE_G;
    view_ci.components.b = VK_COMPONENT_SWIZZLE_B;
    view_ci.components.a = VK_COMPONENT_SWIZZLE_A;
    view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_ci.subresourceRange.levelCount = 1;
    view_ci.subresourceRange.layerCount = 1;

    swapchain.images.resize(image_count);
	for (uint32_t i = 0; i < image_count; i++) {
        Image& img = swapchain.images[i];
        img = {};
        img.image = raw_images[i];
        img.format = swapchain.format;
        img.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        img.extent = extent;
        img.allocation = nullptr;
        img.state.layout = VK_IMAGE_LAYOUT_UNDEFINED;
        img.state.stage  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        img.state.access = 0;

        view_ci.image = img.image;
        err = vkCreateImageView(device, &view_ci, nullptr, &img.image_view);
        LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't create Vulkan image view for swapchain image.");
    }

    swapchain.framebuffers.resize(image_count);
    for (uint32_t i = 0; i < image_count; i++) {
        std::vector<VkImageView> attachments;
        attachments.push_back(swapchain.images[i].image_view);

        swapchain.framebuffers[i] = framebuffer_create(swapchain.render_pass, attachments, extent);
        LUMEN_ERR_FAIL_COND_V_MSG(!swapchain.framebuffers[i], Failed, "Couldn't create swapchain framebuffer.");
    }

	VkSemaphore semaphore = VK_NULL_HANDLE;
	for (uint32_t i = 0; i < image_count; i++) {
        semaphore = semaphore_create();
		swapchain.present_semaphores.push_back(semaphore);
	}

    swapchain.surface->needs_resize = false;
    return Ok;
}

void DeviceDriverVulkan::swapchain_free()
{
    _swapchain_release();
    render_pass_free(swapchain.render_pass);
}

Error DeviceDriverVulkan::swapchain_acquire_next_image(VkSemaphore p_signal_semaphore)
{
    using enum Error;

    VkResult err = vkAcquireNextImageKHR(device, swapchain.swapchain, UINT64_MAX, p_signal_semaphore, VK_NULL_HANDLE, &swapchain.image_index);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
        log_write("NEEDS_RESIZE from acquire OUT_OF_DATE or SUBOPTIMAL_KHR");
        swapchain.surface->needs_resize = true;
        return Ok;
    }
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't get next Vulkan swapchain image.");
    
    return Ok;
    
}

Error DeviceDriverVulkan::swapchain_update()
{
    using enum Error;
    if (!swapchain.surface->needs_resize) return Ok;
    device_wait_idle();
    return swapchain_resize(frame_count);
}

/***********************/
/**** BINDLESS HEAP ****/
/***********************/

Error DeviceDriverVulkan::bindless_heap_create(uint32_t p_sampled_count, uint32_t p_storage_count, uint32_t p_samplers_count)
{
    using enum Error;
    
    VkPhysicalDeviceVulkan12Properties p12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES };
    VkPhysicalDeviceProperties2 p2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    p2.pNext = &p12;
    vkGetPhysicalDeviceProperties2(physical_device, &p2);

    auto clamp = [](uint32_t want, uint32_t per_set, uint32_t per_stage) {
        uint32_t lim = per_set < per_stage ? per_set : per_stage;
        return want < lim ? want : lim;
    };
    const uint32_t sampled = clamp(p_sampled_count, p12.maxDescriptorSetUpdateAfterBindSampledImages, p12.maxPerStageDescriptorUpdateAfterBindSampledImages);
    const uint32_t storage = clamp(p_storage_count, p12.maxDescriptorSetUpdateAfterBindStorageImages, p12.maxPerStageDescriptorUpdateAfterBindStorageImages);
    const uint32_t samplers = clamp(p_samplers_count, p12.maxDescriptorSetUpdateAfterBindSamplers,      p12.maxPerStageDescriptorUpdateAfterBindSamplers);

    if (sampled < p_sampled_count || storage < p_storage_count || samplers < p_samplers_count) {
        log_write("Bindless heap counts clamped to device limits (sampled %u->%u, storage %u->%u, sampler %u->%u).\n", p_sampled_count, sampled, p_storage_count, storage, p_samplers_count, samplers);
    }

    bindless_heap.sampled_alloc.cap = sampled;
    bindless_heap.storage_alloc.cap = storage;
    bindless_heap.sampler_alloc.cap = samplers;

    VkDescriptorSetLayoutBinding bindings[3]{};
    bindings[BindlessHeap::BINDING_SAMPLED] = { BindlessHeap::BINDING_SAMPLED, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, sampled, VK_SHADER_STAGE_ALL, nullptr };
    bindings[BindlessHeap::BINDING_STORAGE] = { BindlessHeap::BINDING_STORAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, storage, VK_SHADER_STAGE_ALL, nullptr };
    bindings[BindlessHeap::BINDING_SAMPLER] = { BindlessHeap::BINDING_SAMPLER, VK_DESCRIPTOR_TYPE_SAMPLER, samplers, VK_SHADER_STAGE_ALL, nullptr };

    const VkDescriptorBindingFlags bf = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    VkDescriptorBindingFlags flags[3]{ bf, bf, bf};

    VkDescriptorSetLayoutBindingFlagsCreateInfo flags_ci{};
    flags_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    flags_ci.bindingCount = 3;
    flags_ci.pBindingFlags = flags;

    VkDescriptorSetLayoutCreateInfo layout_ci{};
    layout_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_ci.pNext = &flags_ci;
    layout_ci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layout_ci.bindingCount = 3;
    layout_ci.pBindings = bindings;

    VkResult err = vkCreateDescriptorSetLayout(device, &layout_ci, nullptr, &bindless_heap.layout);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't create Vulkan descriptor set layout.");

    VkDescriptorPoolSize pool_sizes[3] = {
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, sampled },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, storage },
        { VK_DESCRIPTOR_TYPE_SAMPLER, samplers },
    };
    VkDescriptorPoolCreateInfo pool_ci{};
    pool_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_ci.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    pool_ci.maxSets = 1;
    pool_ci.poolSizeCount = 3;
    pool_ci.pPoolSizes = pool_sizes;

    err = vkCreateDescriptorPool(device, &pool_ci, nullptr, &bindless_heap.pool);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't create Vulkan descriptor pool.");

    VkDescriptorSetAllocateInfo set_ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    set_ai.descriptorPool = bindless_heap.pool;
    set_ai.descriptorSetCount = 1;
    set_ai.pSetLayouts = &bindless_heap.layout;

    err = vkAllocateDescriptorSets(device, &set_ai, &bindless_heap.set);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't create Vulkan descriptor set.");

    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_ALL;
    push_range.offset = 0;
    push_range.size = BindlessHeap::PUSH_CONSTANT_SIZE;

    VkPipelineLayoutCreateInfo pl_ci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pl_ci.setLayoutCount = 1;
    pl_ci.pSetLayouts = &bindless_heap.layout;
    pl_ci.pushConstantRangeCount = 1;
    pl_ci.pPushConstantRanges = &push_range;

    err = vkCreatePipelineLayout(device, &pl_ci, nullptr, &bindless_heap.pipeline_layout);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't create Vulkan pipeline layout.");

    return Ok;
}

void DeviceDriverVulkan::bindless_heap_free()
{
    if (bindless_heap.pipeline_layout) {
        vkDestroyPipelineLayout(device, bindless_heap.pipeline_layout, nullptr);
        bindless_heap.pipeline_layout = VK_NULL_HANDLE;
    }
    if (bindless_heap.pool) {
        vkDestroyDescriptorPool(device, bindless_heap.pool, nullptr);
        bindless_heap.pool = VK_NULL_HANDLE;
    }
    if (bindless_heap.layout) {
        vkDestroyDescriptorSetLayout(device, bindless_heap.layout, nullptr);
        bindless_heap.layout = VK_NULL_HANDLE;
    }
    bindless_heap.set = VK_NULL_HANDLE;
}

uint32_t DeviceDriverVulkan::bindless_heap_alloc_sampled(VkImageView p_image_view)
{
    uint32_t index = bindless_heap.sampled_alloc.acquire();
    if (index == UINT32_MAX) {
        
        log_write("Bindless heap sampled image array exhausted.");
        return UINT32_MAX;
    }

    VkDescriptorImageInfo info{};
    info.imageView = p_image_view;
    info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    w.dstSet = bindless_heap.set;
    w.dstBinding = BindlessHeap::BINDING_SAMPLED;
    w.dstArrayElement = index;
    w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    w.descriptorCount = 1;
    w.pImageInfo = &info;

    vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
    return index;
}

uint32_t DeviceDriverVulkan::bindless_heap_alloc_storage(VkImageView p_image_view)
{
    uint32_t index = bindless_heap.storage_alloc.acquire();
    if (index == UINT32_MAX) {
        log_write("Bindless heap storage image array exhausted.");
        return UINT32_MAX;
    }

    VkDescriptorImageInfo info{};
    info.imageView = p_image_view;
    info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    w.dstSet = bindless_heap.set;
    w.dstBinding = BindlessHeap::BINDING_STORAGE;
    w.dstArrayElement = index;
    w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w.descriptorCount = 1;
    w.pImageInfo = &info;

    vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
    return index;
}

uint32_t DeviceDriverVulkan::bindless_heap_alloc_sampler(VkSampler p_sampler)
{
    uint32_t index = bindless_heap.sampler_alloc.acquire();
    if (index == UINT32_MAX) {
        log_write("Bindless heap sampler array exhausted.");
        return UINT32_MAX;
    }

    VkDescriptorImageInfo info{};
    info.sampler = p_sampler;

    VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    w.dstSet = bindless_heap.set;
    w.dstBinding = BindlessHeap::BINDING_SAMPLER;
    w.dstArrayElement = index;
    w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    w.descriptorCount = 1;
    w.pImageInfo = &info;

    vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
    return index;

}

void DeviceDriverVulkan::bindless_heap_free_sampled(uint32_t p_index)
{
    if (p_index == UINT32_MAX) return;
    bindless_heap.sampled_alloc.release(p_index);
}

void DeviceDriverVulkan::bindless_heap_free_storage(uint32_t p_index)
{
    if (p_index == UINT32_MAX) return;
    bindless_heap.storage_alloc.release(p_index);
}

void DeviceDriverVulkan::bindless_heap_free_sampler(uint32_t p_index)
{
    if (p_index == UINT32_MAX) return;
    bindless_heap.sampler_alloc.release(p_index);
}

/*********************/
/**** FRAMEBUFFER ****/
/*********************/

VkFramebuffer DeviceDriverVulkan::framebuffer_create(VkRenderPass p_render_pass, std::vector<VkImageView>& p_image_views, VkExtent2D p_extent)
{
    using enum Error;

    VkFramebufferCreateInfo framebuffer_ci{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    framebuffer_ci.renderPass = p_render_pass;
    framebuffer_ci.attachmentCount = (uint32_t)p_image_views.size();
    framebuffer_ci.pAttachments = p_image_views.data();
    framebuffer_ci.width = p_extent.width;
    framebuffer_ci.height = p_extent.height;
    framebuffer_ci.layers = 1;

    VkFramebuffer framebuffer;
    VkResult err = vkCreateFramebuffer(device, &framebuffer_ci, nullptr, &framebuffer);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, VK_NULL_HANDLE, "Couldn't create Vulkan framebuffer.");

    // set_object_name(VK_OBJECT_TYPE_FRAMEBUFFER, (uint64_t)framebuffer, p_ci.name);
    return framebuffer;
}

void DeviceDriverVulkan::framebuffer_free(VkFramebuffer& r_framebuffer)
{
    if (r_framebuffer) {
        vkDestroyFramebuffer(device, r_framebuffer, nullptr);
        r_framebuffer = VK_NULL_HANDLE;
    }
}

/******************/
/**** PIPELINE ****/
/******************/

static uint32_t _read_le_u32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool DeviceDriverVulkan::_pipeline_cache_header_valid(const std::vector<uint8_t>& p_data) const
{
    if (p_data.size() < 32) return false;

    const uint8_t* b = p_data.data();
    const uint32_t header_size = _read_le_u32(b + 0);
    const uint32_t header_version = _read_le_u32(b + 4);
    const uint32_t vendor_id = _read_le_u32(b + 8);
    const uint32_t device_id = _read_le_u32(b + 12);

    if (header_size < 32 || header_size > p_data.size()) return false;
    if (header_version != VK_PIPELINE_CACHE_HEADER_VERSION_ONE) return false;
    if (vendor_id != physical_device_properties.vendorID) return false;
    if (device_id != physical_device_properties.deviceID) return false;
    if (memcmp(b + 16, physical_device_properties.pipelineCacheUUID, VK_UUID_SIZE) != 0) return false;

    return true;
}

void DeviceDriverVulkan::_save_pipeline_cache()
{
    if (!pipeline_cache || pipeline_cache_file.empty()) return;

    size_t size = 0;
    if (vkGetPipelineCacheData(device, pipeline_cache, &size, nullptr) != VK_SUCCESS || size == 0) return;

    std::vector<uint8_t> data(size);
    if (vkGetPipelineCacheData(device, pipeline_cache, &size, data.data()) != VK_SUCCESS) return;
    data.resize(size);

    const std::filesystem::path final_path = pipeline_cache_file;
    std::filesystem::path temp_path = final_path;
    temp_path += ".tmp";

    {
        std::ofstream out(temp_path, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!out) {
            out.close();
            std::error_code rm;
            std::filesystem::remove(temp_path, rm);
            return;
        }
    }

    std::error_code ec;
    std::filesystem::rename(temp_path, final_path, ec);
    if (ec) {
        std::filesystem::remove(final_path, ec);
        std::filesystem::rename(temp_path, final_path, ec);
        if (ec) std::filesystem::remove(temp_path, ec);
    }
}

Error DeviceDriverVulkan::pipeline_cache_create()
{
    using enum Error;

    pipeline_cache_file = (Paths::pipeline_cache() / "pipeline.bin").string();

    std::vector<uint8_t> initial_data;
    if (!pipeline_cache_file.empty()) {
        std::ifstream f(pipeline_cache_file, std::ios::binary | std::ios::ate);
        if (f) {
            const std::streamsize bytes = f.tellg();
            if (bytes > 0) {
                initial_data.resize(static_cast<size_t>(bytes));
                f.seekg(0);
                if (!f.read(reinterpret_cast<char*>(initial_data.data()), bytes)) {
                    initial_data.clear();
                }
            }
        }
    }

    if (!_pipeline_cache_header_valid(initial_data)) {
        initial_data.clear();
    }

    VkPipelineCacheCreateInfo cache_ci{ VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
    cache_ci.initialDataSize = initial_data.size();
    cache_ci.pInitialData = initial_data.empty() ? nullptr : initial_data.data();

    VkResult err = vkCreatePipelineCache(device, &cache_ci, nullptr, &pipeline_cache);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, Failed, "Couldn't create Vulkan pipeline cache.");

    return Ok;
}

void DeviceDriverVulkan::pipeline_cache_free()
{
    _save_pipeline_cache();
    if (pipeline_cache) {
        vkDestroyPipelineCache(device, pipeline_cache, nullptr);
        pipeline_cache = VK_NULL_HANDLE;
    }
}

shaderc_shader_kind DeviceDriverVulkan::_shaderc_kind(ShaderStage p_stage)
{
    switch (p_stage) {
        case DeviceDriverVulkan::ShaderStage::Vertex: return shaderc_vertex_shader;
        case DeviceDriverVulkan::ShaderStage::Fragment: return shaderc_fragment_shader;
        case DeviceDriverVulkan::ShaderStage::Compute: return shaderc_compute_shader;
    }
    return shaderc_vertex_shader;
}

uint64_t DeviceDriverVulkan::_shader_cache_key(const void* p_source, size_t p_len, ShaderStage p_stage)
{
    constexpr uint32_t CACHE_FORMAT = 1;

    uint64_t h = 1469598103934665603ull;
    auto mix = [&](const void* p, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    };
    auto mix_u32 = [&](uint32_t v) { mix(&v, sizeof(v)); };

    mix(p_source, p_len);
    mix_u32(static_cast<uint32_t>(p_stage));
    mix_u32(CACHE_FORMAT);
    mix_u32(static_cast<uint32_t>(shaderc_env_version_vulkan_1_3));
    mix_u32(static_cast<uint32_t>(shaderc_optimization_level_zero));
    return h;
}

static std::wstring _shader_include_resource_name(const char* p_path)
{
    std::string s = "SHADERS_";
    for (const char* c = p_path; *c; ++c) {
        unsigned char ch = (unsigned char)*c;
        bool alnum = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
        s += alnum ? (char)std::toupper(ch) : '_';
    }
    return std::wstring(s.begin(), s.end());
}

struct ShaderIncluder : shaderc::CompileOptions::IncluderInterface
{
    struct Result {
        shaderc_include_result result{};
        std::string name;
        EmbeddedResource::Blob blob;
    };

    shaderc_include_result* GetInclude(const char* p_requested, shaderc_include_type, const char*, size_t) override {
        Result* r = new Result();
        r->name = p_requested;
        r->blob = EmbeddedResource::load(_shader_include_resource_name(p_requested).c_str());
        if (r->blob) {
            r->result.source_name = r->name.c_str();
            r->result.source_name_length = r->name.size();
            r->result.content = (const char*)r->blob.data;
            r->result.content_length = r->blob.size;
        } else {
            r->result.source_name = "";
            r->result.source_name_length = 0;
            r->result.content = "embedded shader include not found";
            r->result.content_length = std::strlen(r->result.content);
        }
        r->result.user_data = r;
        return &r->result;
    }

    void ReleaseInclude(shaderc_include_result* p_data) override {
        delete static_cast<Result*>(p_data->user_data);
    }
};

VkShaderModule DeviceDriverVulkan::shader_create(const ShaderCreateInfo& p_ci)
{
    using enum Error;

    std::vector<uint32_t> spirv_storage;
    const uint32_t* code = p_ci.spirv;
    size_t code_size = p_ci.spirv_size;

    if (!code && p_ci.glsl) {
        shaderc::CompileOptions options;
        options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
        options.SetOptimizationLevel(shaderc_optimization_level_zero);
        options.SetIncluder(std::make_unique<ShaderIncluder>());

        shaderc::PreprocessedSourceCompilationResult pre = shader_compiler->PreprocessGlsl(p_ci.glsl, p_ci.glsl_size, _shaderc_kind(p_ci.stage), p_ci.name ? p_ci.name : "embedded_shader", options);
        LUMEN_ERR_FAIL_COND_V_MSG(pre.GetCompilationStatus() != shaderc_compilation_status_success, VK_NULL_HANDLE, pre.GetErrorMessage().c_str());
        const std::string flat(pre.cbegin(), pre.cend());

        const uint64_t key = _shader_cache_key(flat.data(), flat.size(), p_ci.stage);

        std::filesystem::path cache_file;
        if (!shader_cache_dir.empty()) {
            char name[32];
            std::snprintf(name, sizeof(name), "%016llx.spv", static_cast<unsigned long long>(key));
            cache_file = std::filesystem::path(shader_cache_dir) / name;
        }

        bool loaded = false;
        if (!cache_file.empty()) {
            std::ifstream f(cache_file, std::ios::binary | std::ios::ate);
            if (f) {
                const std::streamsize bytes = f.tellg();
                if (bytes > 0 && (bytes % sizeof(uint32_t)) == 0) {
                    f.seekg(0);
                    spirv_storage.resize(static_cast<size_t>(bytes) / sizeof(uint32_t));
                    if (f.read(reinterpret_cast<char*>(spirv_storage.data()), bytes)) {
                        code = spirv_storage.data();
                        code_size = static_cast<size_t>(bytes);
                        loaded = true;
                    }
                }
            }
        }

        if (!loaded) {
            shaderc::SpvCompilationResult res = shader_compiler->CompileGlslToSpv(flat.c_str(), flat.size(), _shaderc_kind(p_ci.stage), p_ci.name ? p_ci.name : "embedded_shader", options);
            LUMEN_ERR_FAIL_COND_V_MSG(res.GetCompilationStatus() != shaderc_compilation_status_success, VK_NULL_HANDLE, res.GetErrorMessage().c_str());

            spirv_storage.assign(res.cbegin(), res.cend());
            code = spirv_storage.data();
            code_size = spirv_storage.size() * sizeof(uint32_t);

            if (!cache_file.empty()) {
                std::error_code ec;
                std::filesystem::create_directories(shader_cache_dir, ec);
                std::ofstream out(cache_file, std::ios::binary | std::ios::trunc);
                if (out) out.write(reinterpret_cast<const char*>(spirv_storage.data()), static_cast<std::streamsize>(code_size));
            }
        }
    }

    LUMEN_ERR_FAIL_COND_V_MSG(!code || code_size == 0, VK_NULL_HANDLE, "No shader code provided.");

    VkShaderModuleCreateInfo shader_ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    shader_ci.codeSize = code_size;
    shader_ci.pCode = code;

    VkShaderModule module;
    VkResult err = vkCreateShaderModule(device, &shader_ci, nullptr, &module);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, VK_NULL_HANDLE, "Couldn't create Vulkan shader module.");

    set_object_name(VK_OBJECT_TYPE_SHADER_MODULE, (uint64_t)module, p_ci.name);
    return module;
}

void DeviceDriverVulkan::shader_free(VkShaderModule& r_shader)
{
    if (r_shader) {
        vkDestroyShaderModule(device, r_shader, nullptr);
        r_shader = VK_NULL_HANDLE;
    }
}

VkPipelineColorBlendAttachmentState DeviceDriverVulkan::_blend_state(BlendMode p_mode)
{
    VkPipelineColorBlendAttachmentState s{};
    s.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    s.colorBlendOp = VK_BLEND_OP_ADD;
    s.alphaBlendOp = VK_BLEND_OP_ADD;

    switch (p_mode) {
        case DeviceDriverVulkan::BlendMode::None:
            s.blendEnable = VK_FALSE;
            break;
        case DeviceDriverVulkan::BlendMode::Alpha:
            s.blendEnable = VK_TRUE;
            s.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            s.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            s.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            s.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            break;
        case DeviceDriverVulkan::BlendMode::Additive:
            s.blendEnable = VK_TRUE;
            s.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            s.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            s.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            s.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            break;
        case DeviceDriverVulkan::BlendMode::PremultipliedAlpha:
            s.blendEnable = VK_TRUE;
            s.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            s.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            s.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            s.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            break;
    }
    return s;
}

DeviceDriverVulkan::Pipeline DeviceDriverVulkan::graphics_pipeline_create(const GraphicsPipelineCreateInfo& p_ci)
{
    using enum Error;

    LUMEN_ERR_FAIL_COND_V_MSG(!p_ci.render_pass, {}, "Graphics pipeline needs a render pass.");
    LUMEN_ERR_FAIL_COND_V_MSG(!p_ci.vertex_shader, {}, "Graphics pipeline needs a vertex shader.");

    // ---- shader stages ----
    VkPipelineShaderStageCreateInfo stages[2]{};
    uint32_t stage_count = 0;
    
    stages[stage_count] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    stages[stage_count].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[stage_count].module = p_ci.vertex_shader;
    stages[stage_count].pName = "main";
    stage_count++;

    if (p_ci.fragment_shader != VK_NULL_HANDLE) {
        stages[stage_count] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stages[stage_count].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[stage_count].module = p_ci.fragment_shader;
        stages[stage_count].pName = "main";
        stage_count++;
    }

    // ---- vertex input ----
    VkPipelineVertexInputStateCreateInfo vertex_input{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vertex_input.vertexBindingDescriptionCount = 0;
    vertex_input.vertexAttributeDescriptionCount = 0;

    // ---- input assembly ----
    VkPipelineInputAssemblyStateCreateInfo input_assembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    input_assembly.topology = p_ci.topology;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    // ---- viewport/scissor ----
    VkPipelineViewportStateCreateInfo viewport_state{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;
    viewport_state.pViewports = nullptr;
    viewport_state.pScissors = nullptr;

    VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic_state{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates = dynamic_states;

    // ---- rasterizer ----
    VkPipelineRasterizationStateCreateInfo rasterizer{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = p_ci.polygon_mode;
    rasterizer.cullMode = p_ci.cull_mode;
    rasterizer.frontFace = p_ci.front_face;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.lineWidth = 1.0f;

    // ---- multisample ----
    VkPipelineMultisampleStateCreateInfo multisample{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisample.rasterizationSamples = p_ci.samples;
    multisample.sampleShadingEnable = VK_FALSE;

    // ---- depth/stencil ----
    VkPipelineDepthStencilStateCreateInfo depth_stencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depth_stencil.depthTestEnable = p_ci.depth_test ? VK_TRUE : VK_FALSE;
    depth_stencil.depthWriteEnable = p_ci.depth_write ? VK_TRUE : VK_FALSE;
    depth_stencil.depthCompareOp = p_ci.depth_compare;
    depth_stencil.depthBoundsTestEnable = VK_FALSE;
    depth_stencil.stencilTestEnable = VK_FALSE;

    // ---- color blend ----
    std::vector<VkPipelineColorBlendAttachmentState> blend_attachments;
    if (!p_ci.blend_overrides.empty()) {
        blend_attachments = p_ci.blend_overrides;
    } else if (p_ci.blend_modes.empty()) {
        blend_attachments.push_back(_blend_state(BlendMode::None));
    } else {
        blend_attachments.reserve(p_ci.blend_modes.size());
        for (BlendMode m : p_ci.blend_modes)
            blend_attachments.push_back(_blend_state(m));
    }

    VkPipelineColorBlendStateCreateInfo color_blend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    color_blend.logicOpEnable = VK_FALSE;
    color_blend.attachmentCount = (uint32_t)blend_attachments.size();
    color_blend.pAttachments = blend_attachments.data();

    // ---- assemble ----
    VkGraphicsPipelineCreateInfo pipeline_ci{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipeline_ci.stageCount = stage_count;
    pipeline_ci.pStages = stages;
    pipeline_ci.pVertexInputState = &vertex_input;
    pipeline_ci.pInputAssemblyState = &input_assembly;
    pipeline_ci.pViewportState = &viewport_state;
    pipeline_ci.pRasterizationState = &rasterizer;
    pipeline_ci.pMultisampleState = &multisample;
    pipeline_ci.pDepthStencilState = &depth_stencil;
    pipeline_ci.pColorBlendState = &color_blend;
    pipeline_ci.pDynamicState = &dynamic_state;
    pipeline_ci.layout = bindless_heap.pipeline_layout;
    pipeline_ci.renderPass = p_ci.render_pass;
    pipeline_ci.subpass = p_ci.subpass;

    Pipeline pipeline;
    pipeline.bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
    VkResult err = vkCreateGraphicsPipelines(device, pipeline_cache, 1, &pipeline_ci, nullptr, &pipeline.pipeline);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, {}, "Couldn't create Vulkan graphics pipeline.");

    set_object_name(VK_OBJECT_TYPE_PIPELINE, (uint64_t)pipeline.pipeline, p_ci.name);
    return pipeline;
}

DeviceDriverVulkan::Pipeline DeviceDriverVulkan::compute_pipeline_create(const ComputePipelineCreateInfo& p_ci)
{
    using enum Error;

    LUMEN_ERR_FAIL_COND_V_MSG(!p_ci.compute_shader, {}, "Compute pipeline needs a compute shader.");

    VkPipelineShaderStageCreateInfo stage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = p_ci.compute_shader;
    stage.pName = "main";

    VkComputePipelineCreateInfo pipeline_ci{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    pipeline_ci.stage = stage;
    pipeline_ci.layout = bindless_heap.pipeline_layout;

    Pipeline pipeline;
    pipeline.bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
    VkResult err = vkCreateComputePipelines(device, pipeline_cache, 1, &pipeline_ci, nullptr, &pipeline.pipeline);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, {}, "Couldn't create Vulkan compute pipeline.");

    set_object_name(VK_OBJECT_TYPE_PIPELINE, (uint64_t)pipeline.pipeline, p_ci.name);
    return pipeline;
}

void DeviceDriverVulkan::pipeline_free(Pipeline& r_pipeline)
{
    if (r_pipeline.pipeline) {
        vkDestroyPipeline(device, r_pipeline.pipeline, nullptr);
        r_pipeline.pipeline = VK_NULL_HANDLE;
    }    
}

void DeviceDriverVulkan::command_bind_pipeline(VkCommandBuffer p_cmd, const Pipeline& p_pipeline)
{
    vkCmdBindPipeline(p_cmd, p_pipeline.bind_point, p_pipeline.pipeline);
}

void DeviceDriverVulkan::_command_bind_uniform_sets(VkCommandBuffer p_cmd, VkPipelineBindPoint p_bind_point, const std::vector<VkDescriptorSet>& p_sets, uint32_t p_first_set_index, uint32_t p_dynamic_offset)
{
    if (p_sets.empty()) return;
    const uint32_t* offsets = nullptr;
    uint32_t offset_count = 0;
    if (p_dynamic_offset != UINT32_MAX) {
        offsets = &p_dynamic_offset;
        offset_count = 1;
    }
	vkCmdBindDescriptorSets(p_cmd, p_bind_point, bindless_heap.pipeline_layout, p_first_set_index, (uint32_t)p_sets.size(), p_sets.data(), offset_count, offsets);
}

void DeviceDriverVulkan::command_bind_graphics_uniform_sets(VkCommandBuffer p_cmd, const std::vector<VkDescriptorSet>& p_sets, uint32_t p_first_set_index, uint32_t p_dynamic_offset)
{
    _command_bind_uniform_sets(p_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_sets, p_first_set_index, p_dynamic_offset);
}

void DeviceDriverVulkan::command_bind_compute_uniform_sets(VkCommandBuffer p_cmd, const std::vector<VkDescriptorSet>& p_sets, uint32_t p_first_set_index, uint32_t p_dynamic_offset)
{
    _command_bind_uniform_sets(p_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p_sets, p_first_set_index, p_dynamic_offset);
}

void DeviceDriverVulkan::command_compute_dispatch(VkCommandBuffer p_cmd, uint32_t p_x_groups, uint32_t p_y_groups, uint32_t p_z_groups)
{
    vkCmdDispatch(p_cmd, p_x_groups, p_y_groups, p_z_groups);
}

void DeviceDriverVulkan::command_compute_dispatch_indirect(VkCommandBuffer p_cmd, const Buffer& p_indirect_buffer, uint64_t p_offset)
{
    vkCmdDispatchIndirect(p_cmd, p_indirect_buffer.buffer, p_offset);
}

/*********************/
/**** RENDER PASS ****/
/*********************/

VkRenderPass DeviceDriverVulkan::render_pass_create(const RenderPassCreateInfo& p_ci)
{
    using enum Error;

    std::vector<VkAttachmentDescription> descs(p_ci.attachments.size());
    std::vector<VkAttachmentReference> color_refs;
    VkAttachmentReference depth_ref{};
    bool has_depth = false;

    for (uint32_t i = 0; i < p_ci.attachments.size(); i++) {
        const RenderPassCreateInfo::Attachment& a = p_ci.attachments[i];
        VkAttachmentDescription& d = descs[i];
        d.format = a.format;
        d.samples = a.samples;
        d.loadOp = a.load_op;
        d.storeOp = a.store_op;
        d.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        d.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        d.initialLayout = a.initial_layout;
        d.finalLayout = a.final_layout;

        if (a.is_depth) {
            depth_ref = { i, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL };
            has_depth = true;
        } else {
            color_refs.push_back({ i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
        }
    }

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = (uint32_t)color_refs.size();
    subpass.pColorAttachments = color_refs.data();
    if (has_depth) subpass.pDepthStencilAttachment = &depth_ref;

    VkSubpassDependency default_dep{};
    default_dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    default_dep.dstSubpass = 0;
    default_dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    default_dep.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    default_dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    default_dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    const VkSubpassDependency* dep = p_ci.dependency ? p_ci.dependency : &default_dep;

    VkRenderPassCreateInfo render_pass_ci{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    render_pass_ci.attachmentCount = (uint32_t)descs.size();
    render_pass_ci.pAttachments = descs.data();
    render_pass_ci.subpassCount = 1;
    render_pass_ci.pSubpasses = &subpass;
    render_pass_ci.dependencyCount = 1;
    render_pass_ci.pDependencies = dep;

    VkRenderPass render_pass;
    VkResult err = vkCreateRenderPass(device, &render_pass_ci, nullptr, &render_pass);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, VK_NULL_HANDLE, "Couldn't create Vulkan render pass.");

    set_object_name(VK_OBJECT_TYPE_RENDER_PASS, (uint64_t)render_pass, p_ci.name);
    return render_pass;
}

void DeviceDriverVulkan::render_pass_free(VkRenderPass& r_render_pass)
{
    if (r_render_pass) {
        vkDestroyRenderPass(device, r_render_pass, nullptr);
        r_render_pass = VK_NULL_HANDLE;
    }
}

void DeviceDriverVulkan::command_begin_render_pass(VkCommandBuffer p_cmd, VkRenderPass p_render_pass, VkFramebuffer p_framebuffer, VkExtent2D p_extent, const std::vector<VkClearValue>& p_clear_values)
{
    VkRenderPassBeginInfo render_pass_bi{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    render_pass_bi.renderPass = p_render_pass;
    render_pass_bi.framebuffer = p_framebuffer;
    render_pass_bi.renderArea = { {0,0}, p_extent };
    render_pass_bi.clearValueCount = (uint32_t)p_clear_values.size();
    render_pass_bi.pClearValues = p_clear_values.data();
    vkCmdBeginRenderPass(p_cmd, &render_pass_bi, VK_SUBPASS_CONTENTS_INLINE);
}

void DeviceDriverVulkan::command_end_render_pass(VkCommandBuffer p_cmd)
{
    vkCmdEndRenderPass(p_cmd);
}

/**************/
/**** MISC ****/
/**************/

void DeviceDriverVulkan::set_object_name(VkObjectType p_type, uint64_t p_handle, const char* p_name)
{
    if (!p_name) return;
    auto fn = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT");
    if (!fn) return;
    VkDebugUtilsObjectNameInfoEXT info{ VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
    info.objectType = p_type;
    info.objectHandle = p_handle;
    info.pObjectName = p_name;
    fn(device, &info);
}

DeviceDriverVulkan::GpuDescription DeviceDriverVulkan::gpu_describe() const
{
    GpuDescription d;

    VkPhysicalDeviceDriverProperties driver_props{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
    VkPhysicalDeviceProperties2 props2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    props2.pNext = &driver_props;
    vkGetPhysicalDeviceProperties2(physical_device, &props2);

    const VkPhysicalDeviceProperties& p = props2.properties;

    d.name = p.deviceName;
    d.type = vk::to_string(vk::PhysicalDeviceType(p.deviceType));
    d.driver_id = vk::to_string(vk::DriverId(driver_props.driverID));
    d.driver_name = driver_props.driverName;

    char api[32];
    std::snprintf(api, sizeof(api), "%u.%u.%u", VK_API_VERSION_MAJOR(p.apiVersion), VK_API_VERSION_MINOR(p.apiVersion), VK_API_VERSION_PATCH(p.apiVersion));
    d.api_version = api;

    return d;
}

}
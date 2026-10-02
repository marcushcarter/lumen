#include <drivers/imgui/imgui_driver.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_vulkan.h>
#include <implot.h>
#include <vulkan/vulkan.h>
#include <iostream>

namespace lumen::drivers {
    
Error ImGuiDriver::initialize(const ImGuiDriverCreateInfo& p_create_info)
{
    using enum Error;

    device = p_create_info.device;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    if (p_create_info.enable_docking) {
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    }

    if (p_create_info.ini_path) {
        ini_path_storage = p_create_info.ini_path;
        io.IniFilename = ini_path_storage.c_str();
    } else {
        io.IniFilename = nullptr;
    }
    
    ImGui::StyleColorsDark();

    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 100 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 },
    };

    VkDescriptorPoolCreateInfo pool_ci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    pool_ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_ci.maxSets = 100;
    pool_ci.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes));
    pool_ci.pPoolSizes = pool_sizes;

    VkResult err = vkCreateDescriptorPool(device, &pool_ci, nullptr, &descriptor_pool);
    LUMEN_ERR_FAIL_COND_V_MSG(err != VK_SUCCESS, FAILED,
        "Failed to initialize ImGui descriptor pool.");

    LUMEN_ERR_FAIL_COND_V_MSG(!ImGui_ImplWin32_Init(p_create_info.hwnd), FAILED,
        "Failed to initialize ImGui Win32 backend.");

    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    platform_io.Platform_CreateVkSurface = [](ImGuiViewport* vp, ImU64 vk_instance, const void* vk_allocator, ImU64* out_vk_surface) -> int {
        VkWin32SurfaceCreateInfoKHR surface_ci{ VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
        surface_ci.hinstance = GetModuleHandleW(nullptr);
        surface_ci.hwnd = static_cast<HWND>(vp->PlatformHandle);

        return vkCreateWin32SurfaceKHR(
            reinterpret_cast<VkInstance>(vk_instance), &surface_ci,
            reinterpret_cast<const VkAllocationCallbacks*>(vk_allocator),
            reinterpret_cast<VkSurfaceKHR*>(out_vk_surface)
        );
    };
    
    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.Instance = p_create_info.instance;
    init_info.PhysicalDevice = p_create_info.physical_device;
    init_info.Device = p_create_info.device;
    init_info.QueueFamily = p_create_info.queue_family;
    init_info.Queue = p_create_info.queue;
    init_info.DescriptorPool = descriptor_pool;
    init_info.MinImageCount = p_create_info.image_count;
    init_info.ImageCount = p_create_info.image_count;
    init_info.PipelineInfoMain.RenderPass = p_create_info.render_pass;
    init_info.PipelineInfoMain.Subpass = p_create_info.subpass;
    init_info.CheckVkResultFn = [](VkResult err){ if(err) fprintf(stderr, "[Lumen] Vulkan error in ImGui backend: %d\n", err); };

    LUMEN_ERR_FAIL_COND_V_MSG(!ImGui_ImplVulkan_Init(&init_info), FAILED, "Failed to initialize ImGui Vulkan backend.");

    texture_cache.initialize(p_create_info.sampler);

    return OK;
}

void ImGuiDriver::shutdown()
{
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    if (descriptor_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
        descriptor_pool = VK_NULL_HANDLE;
    }
}

void ImGuiDriver::begin_frame(uint64_t fn, uint32_t fc, uint64_t epoch)
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    texture_cache.begin_frame(fn, fc, epoch);
}

void ImGuiDriver::end_frame(uint64_t fn)
{
    texture_cache.collect(fn);
}

void ImGuiDriver::render()
{
    ImGui::Render();
}

void ImGuiDriver::record_commands(VkCommandBuffer p_cmd)
{
    ImDrawData* draw_data = ImGui::GetDrawData();
    if (draw_data) {
        ImGui_ImplVulkan_RenderDrawData(draw_data, p_cmd);
    }
}

}
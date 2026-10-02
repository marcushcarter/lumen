#pragma once

namespace lumen {

struct RenderGraph;
struct FrameData;
struct TextureCache;
struct GeometryPool;
struct ProfilingSettings;
namespace drivers { struct DeviceDriverVulkan; struct ImGuiDriver; }

struct RenderContext
{
    drivers::DeviceDriverVulkan* dd = nullptr;
    drivers::ImGuiDriver* imgui = nullptr;
    RenderGraph* graph = nullptr;
    TextureCache* textures = nullptr;
    GeometryPool* geometry = nullptr;
    FrameData* frame = nullptr;
    const ProfilingSettings* profiling = nullptr;
};

}
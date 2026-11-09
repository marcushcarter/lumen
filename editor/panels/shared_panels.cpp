#include <editor/panels/shared_panels.h>
#include <editor/panels/content_browser_panel.h>
#include <editor/panels/gpu_profiler_panel.h>
#include <editor/panels/memory_panel.h>

namespace lumen {

void SharedPanels::initialize()
{
    content_browser = _add<ContentBrowserPanel>();
    gpu_profiler = _add<GpuProfilerPanel>();
    memory = _add<MemoryPanel>();
}

void SharedPanels::shutdown()
{
    for (auto& p : panels) p->shutdown();
    content_browser = nullptr;
    gpu_profiler = nullptr;
    memory = nullptr;
    panels.clear();
}

}

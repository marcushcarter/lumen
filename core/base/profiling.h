#pragma once

namespace lumen {

struct ProfilingSettings
{
    // GPU profiling always includes pipeline statistics (when the device supports them) and cull stats.
    bool gpu = false;

    bool gpu_on() const { return gpu; }
};

}
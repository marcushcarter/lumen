#pragma once

namespace lumen {

struct ProfilingSettings
{
    bool gpu = false;

    bool gpu_on() const { return gpu; }
};

}

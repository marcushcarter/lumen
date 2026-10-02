#pragma once

namespace lumen {

struct ProfilingSettings
{
    bool gpu = false;
    bool gpu_pipeline_stats = false;

    bool gpu_on() const { return gpu; }
    bool gpu_pipeline_stats_on() const { return gpu && gpu_pipeline_stats; }
    bool cull_stats_on() const { return gpu_pipeline_stats_on(); }
};

}
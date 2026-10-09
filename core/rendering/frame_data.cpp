#include <core/rendering/frame_data.h>

namespace lumen {

void FrameData::reset()
{
    instances_scratch.clear();
    transforms_scratch.clear();
    instance_count = 0;
    cluster_ref_capacity = 0;
    lights_scratch.clear();
    directional_count = 0;
    light_count = MAX_DIRECTIONAL_LIGHTS;
}

}
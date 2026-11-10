#include <core/rendering/frame_data.h>

namespace lumen {

void FrameData::reset()
{
    instances_scratch.clear();
    skins_scratch.clear();
    candidates.clear();
    candidate_count = 0;
    joints_scratch.clear();
    transforms_scratch.clear();
    instance_count = 0;
    cluster_ref_capacity = 0;
}

}

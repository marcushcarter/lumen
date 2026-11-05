#pragma once
#include <core/animation/anim_eval.h>
#include <core/assets/lanim.h>
#include <core/assets/lblendspace.h>
#include <core/assets/loverlay.h>
#include <editor/assets/rig_preview.h>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace lumen {

struct EditorContext;

struct AnimPreview
{
    static constexpr double POLL_S = 1.0;

    struct LoadedClip {
        LAnimation clip;
        std::vector<int32_t> map;
        uint64_t mapped_version = UINT64_MAX;
        bool failed = false;
    };

    template<typename T>
    struct LoadedAsset {
        T asset;
        std::filesystem::file_time_type time{};
        double poll = -1.0e9;
        bool failed = false;
    };

    std::unordered_map<Guid, LoadedClip> _clips;
    std::unordered_map<Guid, LoadedAsset<LBlendSpace>> _spaces;
    std::unordered_map<Guid, LoadedAsset<LOverlay>> _overlays;
    AnimEvaluator evaluator;
    std::vector<float> _mask;

    const LoadedClip* clip(EditorContext& ctx, const RigPreview& p_rig, Guid p_guid);
    const LBlendSpace* blend_space(EditorContext& ctx, Guid p_guid);
    const LOverlay* overlay(EditorContext& ctx, Guid p_guid);
    Guid blend_space_mesh(EditorContext& ctx, const RigPreview& p_rig, const LBlendSpace& p_space);
    void blend_weights(EditorContext& ctx, const RigPreview& p_rig, const LBlendSpace& p_space, float p_value, float p_direction, std::vector<float>& r_weights);
    bool blend_pose(EditorContext& ctx, const RigPreview& p_rig, const LBlendSpace& p_space, float p_value, float p_direction, float p_advance, float& r_phase, JointTransform* r_pose);
    void overlay_apply(EditorContext& ctx, const RigPreview& p_rig, const LOverlay& p_overlay, float p_time, float p_weight, JointTransform* r_pose);
    void mask_weights(const LOverlay& p_overlay, const RigPreview& p_rig, std::vector<float>& r_weights) const;
};

}

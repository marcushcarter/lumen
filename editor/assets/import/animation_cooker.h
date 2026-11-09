#pragma once
#include <editor/assets/import/mesh_cooker.h>
#include <core/assets/lanim.h>
#include <filesystem>

struct aiAnimation;
struct aiScene;

namespace lumen {

struct Project;
struct EditorContext;

struct AnimationCooker
{
    static constexpr float SAMPLE_RATE = 30.0f;

    struct Job {
        std::filesystem::path source;
        std::filesystem::path dst_lanim;
        std::filesystem::path content_bin;
        std::string clip;
        bool loop = true;
        vec3 translation = vec3(0.0f);
        vec3 rotation = vec3(0.0f);
        float scale = 1.0f;
    };

    struct Record {
        std::filesystem::path source;
        std::string clip;
        bool loop = true;
        Guid skeleton;
        MeshCooker::CookSettings settings;
    };

    static bool build(const aiScene* p_scene, const aiAnimation* p_anim, const MeshCooker::CookSettings& p_settings, const LSkeleton* p_skeleton, LAnimation& r_clip);
    static Guid find_skeleton(const Project& p_project, const aiAnimation* p_anim, LSkeleton& r_skeleton);
    static Error cook(const Job& p_job, const LAnimation& p_clip);
    static bool load_record(const std::filesystem::path& p_lanim, Record& r_record);
    static Error reimport(EditorContext& ctx, const std::filesystem::path& p_lanim);
};

}

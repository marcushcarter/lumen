#pragma once
#include <core/animation/anim_eval.h>
#include <core/assets/lanim.h>
#include <core/assets/lblendspace.h>
#include <core/assets/lskeleton.h>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace lumen {

struct AnimLibrary : ClipProvider
{
    static constexpr double POLL_S = 1.0;
    static constexpr const char* SPACE_EXTENSION = ".lblendspace";

    struct Clip {
        LAnimation clip;
        bool failed = false;
        std::unordered_map<Guid, std::vector<int32_t>> maps;
    };

    struct Space {
        LBlendSpace space;
        std::filesystem::path path;
        std::filesystem::file_time_type time{};
        double poll = -1.0e9;
        bool failed = false;
    };

    std::filesystem::path content_dir;
    std::filesystem::path assets_dir;
    std::unordered_map<Guid, std::filesystem::path> space_paths;
    std::unordered_map<Guid, Clip> clips;
    std::unordered_map<Guid, Space> spaces;
    const LSkeleton* skeleton = nullptr;
    double time = 0.0;
    double scanned = -1.0e9;

    void load(const std::filesystem::path& p_content_dir, const std::filesystem::path& p_assets_dir);
    void clear();
    void tick(float p_dt) { time += p_dt; }

    const LBlendSpace* blend_space(Guid p_guid);
    BoundClip clip(Guid p_guid) override;

    static std::filesystem::path cooked_space_path(const std::filesystem::path& p_content_dir, Guid p_guid);
    void _scan();
};

}

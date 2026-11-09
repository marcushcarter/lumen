#pragma once
#include <editor/assets/asset_references.h>
#include <string>
#include <vector>

namespace lumen {

struct EditorContext;

struct ReferencesDialog
{
    static constexpr float LIST_ROWS = 10.0f;

    enum class Mode : uint8_t { FIND, REPLACE };

    bool active = false;
    Mode mode = Mode::FIND;
    Guid target;
    AssetType type = AssetType::NONE;
    std::string name;
    std::vector<AssetReference> refs;
    std::vector<uint8_t> checked;
    Guid replacement;
    Guid _warned_for;
    std::vector<std::string> warnings;
    bool delete_original = false;

    void open(EditorContext& ctx, Guid p_target, Mode p_mode);
    void draw(EditorContext& ctx);
};

}

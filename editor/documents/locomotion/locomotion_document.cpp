#include <editor/documents/locomotion/locomotion_document.h>
#include <core/base/error.h>
#include <editor/application/editor_context.h>
#include <editor/assets/asset_registry.h>
#include <editor/assets/import/clip_analyzer.h>
#include <editor/documents/locomotion/panels/locomotion_details_panel.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace lumen {

Error LocomotionDocument::create(EditorContext& ctx)
{
    using enum Error;
    if (!_resolve_path(ctx)) return FAILED;
    if (set.load(path) != OK || set.guid != guid) {
        log_write("Locomotion Set: failed to load '%s'.", path.string().c_str());
        return FAILED;
    }
    saved = set;
    _refresh_title();

    _create_layout();
    viewport.allow_grid = false;
    layout.add_panel<LocomotionDetailsPanel>()->tab = this;
    _create_preview();
    orbit.update();
    return OK;
}

Error LocomotionDocument::save(EditorContext& ctx)
{
    (void)ctx;
    if (set.save(path) != Error::OK) {
        log_write("Locomotion Set: failed to save '%s'.", path.string().c_str());
        return Error::FAILED;
    }
    saved = set;
    _refresh_title();
    return Error::OK;
}

void LocomotionDocument::on_update(EditorContext& ctx, float p_dt)
{
    (void)p_dt;
    _draw_layout(ctx);
    _update_orbit();
    orbit.update();
}

static bool _has(const std::vector<std::string>& p_tokens, const char* p_token)
{
    return std::find(p_tokens.begin(), p_tokens.end(), p_token) != p_tokens.end();
}

static bool _excluded(const std::vector<std::string>& p_tokens)
{
    static constexpr const char* SKIP[] = { "clf", "crf", "crouch", "props", "impulse", "additive", "arms", "jump", "fall", "falling", "land", "flail", "mantle", "roll", "rotate", "transition", "detail", "accel", "secondary", "stance", "look", "sweep" };
    for (const char* s : SKIP) {
        if (_has(p_tokens, s)) return true;
    }
    return false;
}

static int _direction(const std::vector<std::string>& p_tokens, const ClipMeta& p_tags)
{
    if (_has(p_tokens, "lf")) return 2;
    if (_has(p_tokens, "lb")) return 3;
    if (_has(p_tokens, "rf")) return 4;
    if (_has(p_tokens, "rb")) return 5;
    if (!p_tags.has_direction) return 0;
    const float d = p_tags.direction;
    if (d == 0.0f) return 0;
    if (std::abs(d) == 180.0f) return 1;
    if (d == -90.0f) return 2;
    if (d == 90.0f) return 4;
    return -1;
}

static int _match(const std::vector<std::string>& p_tokens, const ClipMeta& p_tags)
{
    const bool pose = _has(p_tokens, "pose");
    if (_has(p_tokens, "mantle") && !_has(p_tokens, "montage")) {
        if (_has(p_tokens, "2m")) return (int)LocoSlot::MANTLE_2M;
        if (_has(p_tokens, "1m")) return _has(p_tokens, "rh") ? (int)LocoSlot::MANTLE_1M_RH : (int)LocoSlot::MANTLE_1M_LH;
        return -1;
    }
    if (_has(p_tokens, "roll") && !_has(p_tokens, "clf") && !_has(p_tokens, "crf") && !_has(p_tokens, "montage")) return (int)LocoSlot::ROLL;
    if (!_has(p_tokens, "additive") && !_has(p_tokens, "falling") && !_has(p_tokens, "clf") && !_has(p_tokens, "crf")) {
        if (_has(p_tokens, "jump")) {
            if (_has(p_tokens, "loop")) return (int)LocoSlot::JUMP_LOOP;
            const bool left = _has(p_tokens, "lf");
            const bool right = _has(p_tokens, "rf");
            if (!left && !right) return -1;
            if (p_tags.gait == ClipGait::WALK) return left ? (int)LocoSlot::JUMP_WALK_LF : (int)LocoSlot::JUMP_WALK_RF;
            if (p_tags.gait == ClipGait::RUN || p_tags.gait == ClipGait::JOG) return left ? (int)LocoSlot::JUMP_RUN_LF : (int)LocoSlot::JUMP_RUN_RF;
            return -1;
        }
        if (_has(p_tokens, "fall")) return _has(p_tokens, "fast") ? (int)LocoSlot::FALL_FAST : (int)LocoSlot::FALL_LOOP;
        if (_has(p_tokens, "land")) {
            if (_has(p_tokens, "heavy")) return (int)LocoSlot::LAND_HEAVY;
            if (_has(p_tokens, "light")) return (int)LocoSlot::LAND_LIGHT;
            return -1;
        }
    }
    if (_has(p_tokens, "impulse") && p_tags.gait == ClipGait::SPRINT && !_has(p_tokens, "clf") && !_has(p_tokens, "crf")) return (int)LocoSlot::SPRINT_IMPULSE;
    if (_has(p_tokens, "clf")) {
        const auto clf = std::find(p_tokens.begin(), p_tokens.end(), "clf");
        const auto stand = std::find(p_tokens.begin(), p_tokens.end(), "n");
        if (_has(p_tokens, "to") && stand != p_tokens.end()) return stand < clf ? (int)LocoSlot::CROUCH_ENTER : (int)LocoSlot::CROUCH_EXIT;
        if (pose) return p_tags.gait == ClipGait::NONE ? (int)LocoSlot::CROUCH_IDLE : -1;
        if (!p_tags.has_direction) return -1;
        const float d = p_tags.direction;
        if (_has(p_tokens, "rotate")) return d < 0.0f ? (int)LocoSlot::CROUCH_ROTATE_L : (int)LocoSlot::CROUCH_ROTATE_R;
        if (p_tags.role == ClipRole::TURN) {
            if (d == -90.0f) return (int)LocoSlot::CROUCH_TURN_L90;
            if (d == 90.0f) return (int)LocoSlot::CROUCH_TURN_R90;
            if (d == -180.0f) return (int)LocoSlot::CROUCH_TURN_L180;
            if (d == 180.0f) return (int)LocoSlot::CROUCH_TURN_R180;
            return -1;
        }
        if (p_tags.gait != ClipGait::WALK) return -1;
        if (d == 0.0f) return (int)LocoSlot::CROUCH_WALK_F;
        if (std::abs(d) == 180.0f) return (int)LocoSlot::CROUCH_WALK_B;
        if (d == -90.0f) return (int)LocoSlot::CROUCH_WALK_L;
        if (d == 90.0f) return (int)LocoSlot::CROUCH_WALK_R;
        return -1;
    }
    if (_has(p_tokens, "rotate") && !_has(p_tokens, "clf") && !_has(p_tokens, "crf") && !_has(p_tokens, "crouch") && p_tags.has_direction) {
        if (p_tags.direction < 0.0f) return (int)LocoSlot::ROTATE_L;
        if (p_tags.direction > 0.0f) return (int)LocoSlot::ROTATE_R;
        return -1;
    }
    if (_excluded(p_tokens)) return -1;
    if (p_tags.role == ClipRole::IDLE) return (int)LocoSlot::IDLE;
    if (pose) {
        bool only = true;
        for (const std::string& t : p_tokens) only = only && (t == "als" || t == "n" || t == "pose" || t == "base");
        return only ? (int)LocoSlot::IDLE : -1;
    }
    if (p_tags.role == ClipRole::TURN && p_tags.has_direction) {
        if (p_tags.direction == -90.0f) return (int)LocoSlot::TURN_L90;
        if (p_tags.direction == 90.0f) return (int)LocoSlot::TURN_R90;
        if (p_tags.direction == -180.0f) return (int)LocoSlot::TURN_L180;
        if (p_tags.direction == 180.0f) return (int)LocoSlot::TURN_R180;
        return -1;
    }
    if (p_tags.role != ClipRole::CYCLE) return -1;
    const int dir = _direction(p_tokens, p_tags);
    if (dir < 0) return -1;
    switch (p_tags.gait) {
        case ClipGait::WALK: return (int)LocoSlot::WALK_F + dir;
        case ClipGait::RUN:
        case ClipGait::JOG: return (int)LocoSlot::RUN_F + dir;
        case ClipGait::SPRINT: return dir == 0 ? (int)LocoSlot::SPRINT_F : -1;
        default: return -1;
    }
}

uint32_t LocomotionDocument::auto_fill(EditorContext& ctx)
{
    if (!ctx.assets) return 0;
    const std::filesystem::path folder = path.parent_path();
    int best_score[LOCO_SLOT_COUNT];
    Guid best[LOCO_SLOT_COUNT];
    for (uint32_t i = 0; i < LOCO_SLOT_COUNT; ++i) best_score[i] = -1;
    for (const auto& [guid_key, entry] : ctx.assets->entries) {
        if (entry.type != AssetType::ANIMATION) continue;
        const std::vector<std::string> tokens = clip_name_tokens(entry.name);
        ClipMeta tags;
        clip_name_tags(entry.name, tags);
        const int slot = _match(tokens, tags);
        if (slot < 0) continue;
        int score = 1;
        if (_has(tokens, "n")) score += 2;
        if (entry.path.parent_path() == folder) score += 1;
        if (score <= best_score[slot]) continue;
        best_score[slot] = score;
        best[slot] = guid_key;
    }
    uint32_t filled = 0;
    for (uint32_t i = 0; i < LOCO_SLOT_COUNT; ++i) {
        if (set.clips[i].valid() || !best[i].valid()) continue;
        set.clips[i] = best[i];
        filled++;
    }
    return filled;
}

}

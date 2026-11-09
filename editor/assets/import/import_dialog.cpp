#include <editor/assets/import/import_dialog.h>
#include <editor/application/editor_context.h>
#include <editor/ui/ui_style.h>
#include <editor/ui/ui_widgets.h>
#include <core/assets/lmesh.h>
#include <core/assets/lmodel.h>
#include <imgui.h>
#include <algorithm>
#include <iterator>
#include <cstdio>
#include <cstring>
#include <string>

namespace lumen {

ImportKind import_kind(const std::filesystem::path& p_src)
{
    std::string e = p_src.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    if (e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".tga" || e == ".bmp") return ImportKind::TEXTURE;
    if (e == ".fbx" || e == ".gltf" || e == ".glb" || e == ".obj" || e == ".dae" || e == ".stl" || e == ".ply" || e == ".3ds" || e == ".blend") return ImportKind::MODEL;
    return ImportKind::NONE;
}

static int _name_filter(ImGuiInputTextCallbackData* p_data)
{
    return p_data->EventChar < 32 || std::strchr("<>:\"/\\|?*", (int)p_data->EventChar) ? 1 : 0;
}

bool ImportDialog::push(EditorContext& ctx, const std::filesystem::path& p_src, const std::filesystem::path& p_dst_dir)
{
    (void)ctx;
    const ImportKind kind = import_kind(p_src);
    if (kind == ImportKind::NONE) return false;

    Pending p;
    p.source = p_src;
    p.dst_dir = p_dst_dir;
    p.kind = kind;
    if (kind == ImportKind::TEXTURE) p.texture.usage = texture_usage_guess(p_src);
    const std::u8string stem = p_src.stem().u8string();
    std::snprintf(p.name, sizeof(p.name), "%s", (const char*)stem.c_str());
    _enqueue(std::move(p));
    return true;
}

bool ImportDialog::push_reimport(const ReimportTarget& p_target)
{
    if (!reimport_has_settings(p_target.kind)) return false;
    ReimportSettings settings;
    if (!reimport_load_settings(p_target, settings)) return false;

    Pending p;
    p.reimport = true;
    p.target = p_target;
    p.source = settings.source;
    p.dst_dir = p_target.asset.parent_path();
    switch (p_target.kind) {
        case ReimportKind::TEXTURE: p.kind = ImportKind::TEXTURE; break;
        case ReimportKind::MESH: p.kind = ImportKind::MESH; break;
        default: p.kind = ImportKind::MODEL; break;
    }
    p.model = settings.model;
    p.texture = settings.texture;
    const std::u8string stem = p_target.asset.stem().u8string();
    std::snprintf(p.name, sizeof(p.name), "%s", (const char*)stem.c_str());
    _enqueue(std::move(p));
    return true;
}

void ImportDialog::_enqueue(Pending&& r_pending)
{
    if (queue.empty()) {
        total = 0;
        focus_name = true;
    }
    queue.push_back(std::move(r_pending));
    total++;
}

void ImportDialog::clear()
{
    queue.clear();
    total = 0;
}

void ImportDialog::_next()
{
    queue.pop_front();
    focus_name = !queue.empty();
}

void ImportDialog::_draw_model(ModelCooker::Settings& r_settings, bool p_materials)
{
    MeshCooker::CookSettings& mesh = r_settings.mesh_settings;
    if (ui_category("Mesh")) {
        ImGui::Checkbox("Load Skeleton", &mesh.load_skeleton);
        bool dag = mesh.dag_level == MeshDagLevel::CLUSTERED_DAG;
        if (ImGui::Checkbox("Build DAG", &dag)) mesh.dag_level = dag ? MeshDagLevel::CLUSTERED_DAG : MeshDagLevel::CLUSTERED;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Builds LODs. Turn off for small props and exact geometry.");
        int verts = (int)mesh.max_verts;
        if (ImGui::DragInt("Max Cluster Vertices", &verts, 1.0f, (int)MeshCooker::MIN_CLUSTER_VERTS, (int)LMESH_CLUSTER_MAX_VERTS, "%d", ImGuiSliderFlags_AlwaysClamp)) mesh.max_verts = (uint32_t)verts;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Most vertices one cluster may hold");
        int tris = (int)mesh.max_tris;
        if (ImGui::DragInt("Max Cluster Triangles", &tris, 1.0f, (int)MeshCooker::MIN_CLUSTER_TRIS, (int)LMESH_CLUSTER_MAX_TRIS, "%d", ImGuiSliderFlags_AlwaysClamp)) mesh.max_tris = (uint32_t)tris;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Most triangles one cluster may hold");
        ImGui::BeginDisabled(!dag);
        int group = (int)mesh.group_size;
        if (ImGui::DragInt("Group Size", &group, 0.1f, (int)MeshCooker::MIN_GROUP_SIZE, (int)LMESH_PART_MAX_CLUSTERS, "%d", ImGuiSliderFlags_AlwaysClamp)) mesh.group_size = (uint32_t)group;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Clusters merged and simplified together for each coarser LOD");
        int levels = (int)mesh.max_levels;
        if (ImGui::DragInt("Max LOD Levels", &levels, 0.1f, 1, (int)MeshCooker::MAX_LOD_LEVELS, "%d", ImGuiSliderFlags_AlwaysClamp)) mesh.max_levels = (uint32_t)levels;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Most LOD levels built for the mesh");
        ImGui::EndDisabled();
    }
    if (ui_category("Transform")) {
        ui_drag_float3_axes("Import Translation", &mesh.translation.x, 0.01f);
        ui_drag_float3_axes("Import Rotation (Degrees)", &mesh.rotation.x, 0.5f, "%.1f");
        ImGui::DragFloat("Import Uniform Scale", &mesh.scale, 0.01f, 0.001f, 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    }
    _draw_collision(mesh.collision, &mesh.generate_collision);
    if (p_materials && ui_category("Materials")) {
        bool generate = r_settings.mode == ModelImportMode::FULL;
        if (ImGui::Checkbox("Generate Materials", &generate)) r_settings.mode = generate ? ModelImportMode::FULL : ModelImportMode::MESH_ONLY;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Creates materials and imports their textures");
    }
    ui_category_flush();
}

static const char* const TEXTURE_USAGE_LABELS[] = { "Color", "Normal Map", "Mask", "Linear" };
static const char* const TEXTURE_QUALITY_LABELS[] = { "Fast", "Normal", "Best" };
static constexpr uint32_t TEXTURE_MAX_SIZES[] = { 0, 8192, 4096, 2048, 1024, 512, 256 };

void ImportDialog::_draw_texture(TextureCooker::CookSettings& r_settings)
{
    if (ui_category("Texture")) {
        int usage = (int)r_settings.usage;
        if (ImGui::Combo("Usage", &usage, TEXTURE_USAGE_LABELS, (int)std::size(TEXTURE_USAGE_LABELS))) r_settings.usage = (TextureUsage)usage;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Color is sRGB. Normal Map, Mask and Linear are stored as raw data.");
        if (r_settings.usage == TextureUsage::NORMAL) {
            ImGui::Checkbox("Flip Green", &r_settings.flip_green);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Turn on for DirectX-style normal maps");
        }
        ImGui::DragFloat("Alpha Cutoff", &r_settings.alpha_cutoff, 0.01f, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Keeps alpha-tested coverage stable across mips. 0 turns it off.");
    }
    if (ui_category("Compression")) {
        int quality = (int)r_settings.quality;
        if (ImGui::Combo("Quality", &quality, TEXTURE_QUALITY_LABELS, (int)std::size(TEXTURE_QUALITY_LABELS))) r_settings.quality = (TextureQuality)quality;
        char current[16];
        if (r_settings.max_size) std::snprintf(current, sizeof(current), "%u", r_settings.max_size);
        else std::snprintf(current, sizeof(current), "Original");
        if (ImGui::BeginCombo("Max Size", current)) {
            for (uint32_t size : TEXTURE_MAX_SIZES) {
                char label[16];
                if (size) std::snprintf(label, sizeof(label), "%u", size);
                else std::snprintf(label, sizeof(label), "Original");
                if (ImGui::Selectable(label, size == r_settings.max_size)) r_settings.max_size = size;
            }
            ImGui::EndCombo();
        }
        ImGui::Checkbox("Generate Mips", &r_settings.generate_mips);
    }
    ui_category_flush();
}

static constexpr float CM_PER_M = 100.0f;

void collision_settings_fields(CollisionSettings& r_settings)
{
    const char* current = collision_type_label(r_settings.type);
    if (ImGui::BeginCombo("Collision Type", current)) {
        for (uint32_t i = 0; i < (uint32_t)CollisionType::COUNT; i++) {
            const CollisionType t = (CollisionType)i;
            if (ImGui::Selectable(collision_type_label(t), t == r_settings.type)) r_settings.type = t;
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Triangle Mesh is for static objects only. Moving objects need a hull or a primitive.");

    if (r_settings.type == CollisionType::MESH || r_settings.type == CollisionType::HULL) {
        float error_cm = r_settings.error * CM_PER_M;
        if (ImGui::DragFloat("Collision Error", &error_cm, 0.1f, 0.0f, 100.0f, "%.1f cm", ImGuiSliderFlags_AlwaysClamp)) r_settings.error = error_cm / CM_PER_M;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("How far the collision may stray from the real surface. 0 keeps every triangle.");
    }
    float feature_cm = r_settings.min_feature * CM_PER_M;
    if (ImGui::DragFloat("Min Feature Size", &feature_cm, 0.5f, 0.0f, 500.0f, "%.1f cm", ImGuiSliderFlags_AlwaysClamp)) r_settings.min_feature = feature_cm / CM_PER_M;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Separate pieces smaller than this are left out. The largest piece is always kept.");
    ImGui::Checkbox("Exclude Masked Materials", &r_settings.exclude_masked);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Leaves out alpha-masked and transparent materials, like leaves and grass cards");
}

void ImportDialog::_draw_collision(CollisionSettings& r_settings, bool* r_generate)
{
    if (!ui_category("Collision")) return;
    if (r_generate) {
        ImGui::Checkbox("Generate Collision", r_generate);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Builds a collision asset next to the mesh from the source geometry");
    }
    const bool enabled = !r_generate || *r_generate;
    if (!enabled) ImGui::BeginDisabled();
    collision_settings_fields(r_settings);
    if (!enabled) ImGui::EndDisabled();
}

void ImportDialog::_start(EditorContext& ctx, Pending& r_pending)
{
    if (r_pending.reimport) {
        ReimportSettings settings;
        settings.source = r_pending.source;
        settings.model = r_pending.model;
        settings.texture = r_pending.texture;
        reimport_asset(ctx, r_pending.target.asset, &settings);
        return;
    }
    const std::wstring name = r_pending.name[0] ? std::filesystem::path((const char8_t*)r_pending.name).wstring() : r_pending.source.stem().wstring();
    if (r_pending.kind == ImportKind::TEXTURE) {
        TextureCooker::import_async(ctx, r_pending.source, r_pending.dst_dir / (name + L".ltexture"), r_pending.texture);
        return;
    }
    r_pending.model.name = name;
    ModelCooker::import_async(ctx, r_pending.source, r_pending.dst_dir, r_pending.model);
}

void ImportDialog::_draw_header(Pending& r_pending)
{
    const UiStyle& ui = ui_style();
    const float full = ImGui::GetContentRegionAvail().x;

    ImGui::TextUnformatted("Asset Name:");
    ImGui::SetNextItemWidth(full);
    if (r_pending.reimport) {
        focus_name = false;
        ImGui::BeginDisabled();
        ImGui::InputText("##name", r_pending.name, sizeof(r_pending.name), ImGuiInputTextFlags_ReadOnly);
        ImGui::EndDisabled();
    } else {
        if (focus_name) {
            ImGui::SetKeyboardFocusHere();
            focus_name = false;
        }
        ImGui::InputText("##name", r_pending.name, sizeof(r_pending.name), ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_CallbackCharFilter, _name_filter);
    }

    ImGui::Dummy(ImVec2(0.0f, ui.space.lg));
    ImGui::TextUnformatted("Source File:");
    const std::u8string path = r_pending.source.generic_u8string();
    ImGui::PushStyleColor(ImGuiCol_Text, ui.color.text_dim);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + full);
    ImGui::TextUnformatted((const char*)path.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.0f, ui.space.lg));
}

void ImportDialog::_draw_footer(EditorContext& ctx, Pending& r_pending)
{
    const UiStyle& ui = ui_style();
    const ImGuiStyle& style = ImGui::GetStyle();

    ImGui::Dummy(ImVec2(0.0f, ui.space.lg));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, ui.space.sm));

    const float start_x = ImGui::GetCursorPosX();
    const float full = ImGui::GetContentRegionAvail().x;
    if (total > 1) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%u of %u", total - (uint32_t)queue.size() + 1, total);
        ImGui::SameLine();
    }
    const float button_w = ui.import_dialog.button_width;
    const bool many = queue.size() > 1;
    const float buttons = many ? 3.0f : 2.0f;
    ImGui::SetCursorPosX(start_x + full - button_w * buttons - style.ItemSpacing.x * (buttons - 1.0f));
    bool accept_all = false;
    if (many) {
        ui_push_buttons(UiButton::NEUTRAL);
        accept_all = ImGui::Button(r_pending.reimport ? "Reimport All" : "Import All", ImVec2(button_w, 0.0f));
        ui_pop_buttons();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Accepts this and every remaining item with the settings shown for each");
        ImGui::SameLine();
    }
    ui_push_buttons(UiButton::PRIMARY);
    bool accept = ImGui::Button(r_pending.reimport ? "Reimport" : "Import", ImVec2(button_w, 0.0f));
    ui_pop_buttons();
    ImGui::SameLine();
    ui_push_buttons(UiButton::NEUTRAL);
    bool cancel = ImGui::Button("Cancel", ImVec2(button_w, 0.0f));
    ui_pop_buttons();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        cancel |= ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        accept |= !cancel && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    }

    if (accept_all) {
        while (!queue.empty()) {
            _start(ctx, queue.front());
            queue.pop_front();
        }
        focus_name = false;
        ImGui::CloseCurrentPopup();
        return;
    }
    if (accept) _start(ctx, r_pending);
    if (accept || cancel) {
        _next();
        if (queue.empty()) ImGui::CloseCurrentPopup();
    }
}

static const char* _dialog_title(const ImportDialog::Pending& p_pending)
{
    switch (p_pending.kind) {
        case ImportKind::TEXTURE: return p_pending.reimport ? "Reimport Texture" : "Import Texture";
        case ImportKind::MESH: return "Reimport Mesh";
        default: return p_pending.reimport ? "Reimport Model" : "Import Model";
    }
}

void ImportDialog::draw(EditorContext& ctx)
{
    static constexpr const char* POPUP_ID = "###import_dialog";
    static constexpr ImGuiWindowFlags FLAGS = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize;

    if (queue.empty()) {
        if (ImGui::IsPopupOpen(POPUP_ID) && ImGui::BeginPopupModal(POPUP_ID, nullptr, FLAGS)) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        return;
    }

    const UiStyle& ui = ui_style();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    Pending& p = queue.front();

    char title[64];
    std::snprintf(title, sizeof(title), "%s%s", _dialog_title(p), POPUP_ID);
    if (!ImGui::IsPopupOpen(POPUP_ID)) ImGui::OpenPopup(POPUP_ID);
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(ui.import_dialog.width, 0.0f), ImVec2(ui.import_dialog.width, vp->WorkSize.y * ui.import_dialog.max_height_ratio));
    if (!ImGui::BeginPopupModal(title, nullptr, FLAGS)) return;

    ImGui::PushID(&p);
    ui_field_push_style();
    _draw_header(p);
    if (p.kind == ImportKind::TEXTURE) _draw_texture(p.texture);
    else _draw_model(p.model, p.kind == ImportKind::MODEL);
    ui_field_pop_style();
    _draw_footer(ctx, p);
    ImGui::PopID();
    ImGui::EndPopup();
}

}

#include <editor/assets/import/import_dialog.h>
#include <editor/application/editor_context.h>
#include <editor/ui/ui_style.h>
#include <editor/ui/ui_widgets.h>
#include <core/assets/ltexture.h>
#include <core/assets/lmesh.h>
#include <core/assets/lmodel.h>
#include <imgui.h>
#include <algorithm>
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

template <typename E>
static void _enum_combo(const char* p_label, E& r_value, E p_last, const char* (*p_name)(E))
{
    if (!ImGui::BeginCombo(p_label, p_name(r_value))) return;
    for (int i = 0; i <= (int)p_last; ++i) {
        if (ImGui::Selectable(p_name((E)i), i == (int)r_value)) r_value = (E)i;
    }
    ImGui::EndCombo();
}

static void _max_size_combo(uint32_t& r_size)
{
    static constexpr uint32_t SIZES[] = { 0, 256, 512, 1024, 2048, 4096 };
    char preview[16];
    std::snprintf(preview, sizeof(preview), r_size == 0 ? "None" : "%u", r_size);
    if (!ImGui::BeginCombo("Max Size", preview)) return;
    for (uint32_t s : SIZES) {
        char label[16];
        std::snprintf(label, sizeof(label), s == 0 ? "None" : "%u", s);
        if (ImGui::Selectable(label, s == r_size)) r_size = s;
    }
    ImGui::EndCombo();
}

static int _name_filter(ImGuiInputTextCallbackData* p_data)
{
    return p_data->EventChar < 32 || std::strchr("<>:\"/\\|?*", (int)p_data->EventChar) ? 1 : 0;
}

bool ImportDialog::push(const std::filesystem::path& p_src, const std::filesystem::path& p_dst_dir)
{
    Pending p;
    p.source = p_src;
    p.dst_dir = p_dst_dir;
    p.kind = import_kind(p_src);
    if (p.kind == ImportKind::NONE) return false;
    const std::u8string stem = p_src.stem().u8string();
    std::snprintf(p.name, sizeof(p.name), "%s", (const char*)stem.c_str());
    if (p.kind == ImportKind::TEXTURE) p.texture.usage = texture_usage_guess(p_src);
    if (queue.empty()) {
        total = 0;
        focus_name = true;
    }
    queue.push_back(std::move(p));
    total++;
    return true;
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

void ImportDialog::_draw_texture(TextureCooker::CookSettings& r_settings)
{
    if (!ui_category("Texture", nullptr, true)) return;
    _enum_combo("Usage", r_settings.usage, TextureUsage::LINEAR, texture_usage_name);
    _enum_combo("Quality", r_settings.quality, TextureQuality::BEST, texture_quality_name);
    _max_size_combo(r_settings.max_size);
    ImGui::Checkbox("Generate Mips", &r_settings.generate_mips);
    if (r_settings.usage == TextureUsage::NORMAL) ImGui::Checkbox("Flip Green", &r_settings.flip_green);
}

void ImportDialog::_draw_model(ModelImporter::Settings& r_settings)
{
    MeshCooker::CookSettings& mesh = r_settings.mesh_settings;
    if (ui_category("Mesh", nullptr, true)) {
        ImGui::Checkbox("Load Skeleton", &mesh.load_skeleton);
        bool dag = mesh.dag_level == MeshDagLevel::CLUSTERED_DAG;
        if (ImGui::Checkbox("Build DAG", &dag)) mesh.dag_level = dag ? MeshDagLevel::CLUSTERED_DAG : MeshDagLevel::CLUSTERED;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Builds LODs. Turn off for small props and exact geometry.");
    }
    if (ui_category("Transform", nullptr, true)) {
        ui_drag_float3_axes("Import Translation", &mesh.translation.x, 0.01f);
        ui_drag_float3_axes("Import Rotation (Degrees)", &mesh.rotation.x, 0.5f, "%.1f");
        ImGui::DragFloat("Import Uniform Scale", &mesh.scale, 0.01f, 0.001f, 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    }
    if (ui_category("Materials", nullptr, true)) {
        bool generate = r_settings.mode == ModelImportMode::FULL;
        if (ImGui::Checkbox("Generate Materials", &generate)) r_settings.mode = generate ? ModelImportMode::FULL : ModelImportMode::MESH_ONLY;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Creates materials and imports their textures");
        ImGui::BeginDisabled(!generate);
        _enum_combo("Quality", r_settings.texture_settings.quality, TextureQuality::BEST, texture_quality_name);
        _max_size_combo(r_settings.texture_settings.max_size);
        ImGui::EndDisabled();
    }
}

void ImportDialog::_start(EditorContext& ctx, Pending& r_pending)
{
    const std::wstring name = r_pending.name[0] ? std::filesystem::path((const char8_t*)r_pending.name).wstring() : r_pending.source.stem().wstring();
    if (r_pending.kind == ImportKind::TEXTURE) TextureCooker::import_async(ctx, r_pending.source, r_pending.dst_dir / (name + L".ltexture"), r_pending.texture);
    if (r_pending.kind == ImportKind::MODEL) {
        r_pending.model.name = name;
        ModelImporter::import_async(ctx, r_pending.source, r_pending.dst_dir, r_pending.model);
    }
}

void ImportDialog::_draw_header(Pending& r_pending)
{
    const UiStyle& ui = ui_style();
    const float full = ImGui::GetContentRegionAvail().x;

    ImGui::TextUnformatted("Asset Name:");
    ImGui::SetNextItemWidth(full);
    if (focus_name) {
        ImGui::SetKeyboardFocusHere();
        focus_name = false;
    }
    ImGui::InputText("##name", r_pending.name, sizeof(r_pending.name), ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_CallbackCharFilter, _name_filter);

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
    ImGui::SetCursorPosX(start_x + full - button_w * 2.0f - style.ItemSpacing.x);
    ui_push_buttons(UiButton::PRIMARY);
    bool accept = ImGui::Button("Import", ImVec2(button_w, 0.0f));
    ui_pop_buttons();
    ImGui::SameLine();
    ui_push_buttons(UiButton::NEUTRAL);
    bool cancel = ImGui::Button("Cancel", ImVec2(button_w, 0.0f));
    ui_pop_buttons();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        cancel |= ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        accept |= !cancel && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    }

    if (accept) _start(ctx, r_pending);
    if (accept || cancel) {
        _next();
        if (queue.empty()) ImGui::CloseCurrentPopup();
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
    std::snprintf(title, sizeof(title), "%s%s", p.kind == ImportKind::MODEL ? "Import Model" : "Import Texture", POPUP_ID);
    if (!ImGui::IsPopupOpen(POPUP_ID)) ImGui::OpenPopup(POPUP_ID);
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(ui.import_dialog.width, 0.0f), ImVec2(ui.import_dialog.width, vp->WorkSize.y * ui.import_dialog.max_height_ratio));
    if (!ImGui::BeginPopupModal(title, nullptr, FLAGS)) return;

    ImGui::PushID(&p);
    ui_field_push_style();
    _draw_header(p);
    if (p.kind == ImportKind::TEXTURE) _draw_texture(p.texture);
    if (p.kind == ImportKind::MODEL) _draw_model(p.model);
    ui_field_pop_style();
    _draw_footer(ctx, p);
    ImGui::PopID();
    ImGui::EndPopup();
}

}

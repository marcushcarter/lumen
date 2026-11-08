#include <editor/assets/import/import_dialog.h>
#include <editor/application/editor_context.h>
#include <editor/ui/ui_style.h>
#include <editor/ui/ui_widgets.h>
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

static int _name_filter(ImGuiInputTextCallbackData* p_data)
{
    return p_data->EventChar < 32 || std::strchr("<>:\"/\\|?*", (int)p_data->EventChar) ? 1 : 0;
}

bool ImportDialog::push(EditorContext& ctx, const std::filesystem::path& p_src, const std::filesystem::path& p_dst_dir)
{
    const ImportKind kind = import_kind(p_src);
    if (kind == ImportKind::NONE) return false;
    if (kind == ImportKind::TEXTURE) {
        TextureCooker::CookSettings settings;
        settings.usage = texture_usage_guess(p_src);
        TextureCooker::import_async(ctx, p_src, p_dst_dir / (p_src.stem().wstring() + L".ltexture"), settings);
        return true;
    }

    Pending p;
    p.source = p_src;
    p.dst_dir = p_dst_dir;
    p.kind = kind;
    const std::u8string stem = p_src.stem().u8string();
    std::snprintf(p.name, sizeof(p.name), "%s", (const char*)stem.c_str());
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

void ImportDialog::_draw_model(ModelCooker::Settings& r_settings)
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
    }
    ui_category_flush();
}

void ImportDialog::_start(EditorContext& ctx, Pending& r_pending)
{
    r_pending.model.name = r_pending.name[0] ? std::filesystem::path((const char8_t*)r_pending.name).wstring() : r_pending.source.stem().wstring();
    ModelCooker::import_async(ctx, r_pending.source, r_pending.dst_dir, r_pending.model);
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
    std::snprintf(title, sizeof(title), "Import Model%s", POPUP_ID);
    if (!ImGui::IsPopupOpen(POPUP_ID)) ImGui::OpenPopup(POPUP_ID);
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(ui.import_dialog.width, 0.0f), ImVec2(ui.import_dialog.width, vp->WorkSize.y * ui.import_dialog.max_height_ratio));
    if (!ImGui::BeginPopupModal(title, nullptr, FLAGS)) return;

    ImGui::PushID(&p);
    ui_field_push_style();
    _draw_header(p);
    _draw_model(p.model);
    ui_field_pop_style();
    _draw_footer(ctx, p);
    ImGui::PopID();
    ImGui::EndPopup();
}

}

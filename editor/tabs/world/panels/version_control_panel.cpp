#include <editor/tabs/world/panels/version_control_panel.h>
#include <core/base/hash.h>
#include <editor/application/editor_context.h>
#include <editor/ui/ui_widgets.h>
#include <editor/ui/tab_strip.h>
#include <editor/ui/ui_style.h>
#include <drivers/windows/dialogs_win32.h>
#include <core/project/project.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <ctime>
#include <mutex>
#include <unordered_map>

namespace lumen {

using GitState = drivers::GitDriver::State;

static bool _is_main(const std::string& p_name)
{
    return p_name == "main" || p_name == "master";
}

void VersionControlPanel::draw_contents(EditorContext& ctx)
{
    if (!ctx.git) return;
    _sync(ctx);

    if (cached.state == GitState::CHECKING) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ui_style().outliner.pad_x);
        ImGui::TextDisabled("Checking...");
        return;
    }
    if (cached.state != GitState::NO_GIT && !ctx.git->busy && ImGui::GetTime() - last_poll > POLL_INTERVAL) {
        ctx.git->changes_poll();
        last_poll = ImGui::GetTime();
    }
    if (cached.state == GitState::NO_GIT || cached.state == GitState::NO_REPO || !_identity()) {
        _draw_banner(ctx);
        _draw_setup(ctx);
        return;
    }

    _draw_toolbar(ctx);
    _draw_banner(ctx);
    if (cached.state == GitState::NO_REMOTE || editing_remote) _draw_remote(ctx);
    const float footer_h = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    _draw_graph(ctx, footer_h);
    _draw_footer();

    _draw_rename_popup(ctx);
}

void VersionControlPanel::_sync(EditorContext& ctx)
{
    drivers::GitDriver& git = *ctx.git;
    {
        std::lock_guard lock(git.mutex);
        if (git.status.version != cached.version) cached = git.status;
    }
    if (cached.version != built_version) {
        _build_graph();
        built_version = cached.version;
    }
    if (git.busy) return;

    if (commit_pending) {
        commit_pending = false;
        if (!cached.failed) message[0] = '\0';
    }
    if (download_pending) {
        download_pending = false;
        bool behind = false;
        const std::string prefix = std::string(drivers::GitDriver::REMOTE) + "/";
        for (const drivers::GitDriver::Ref& l : cached.refs) {
            if (l.remote) continue;
            for (const drivers::GitDriver::Ref& r : cached.refs) behind |= r.remote && r.name == prefix + l.name && r.hash != l.hash;
        }
        if (!cached.failed && behind && ctx.reload_project_callback) {
            drivers::GitDriver* g = ctx.git;
            ctx.reload_project_callback([g] { g->download_now(); });
        }
    }
}

void VersionControlPanel::_commit(EditorContext& ctx)
{
    if (_message_blank() || cached.changes.empty() || ctx.git->busy) return;
    if (ctx.save_all_callback) ctx.save_all_callback();
    if (ctx.git->commit(message)) commit_pending = true;
}

void VersionControlPanel::_go_to(EditorContext& ctx, const std::string& p_hash)
{
    if (!ctx.reload_project_callback || ctx.git->busy) return;
    drivers::GitDriver* g = ctx.git;
    if (g->changes_now().empty()) {
        ctx.reload_project_callback([g, p_hash] { g->go_to_now(p_hash); });
        return;
    }
    if (!drivers::Win32Dialogs::confirm(L"Uncommitted Changes", L"Discard uncommitted changes and continue?", L"You have changes that aren't committed. Continuing throws them away and new files are deleted. Commit first if you want to keep them.")) return;
    ctx.reload_project_callback([g, p_hash] {
        if (g->discard_now()) g->go_to_now(p_hash);
    });
}

void VersionControlPanel::_draw_setup(EditorContext& ctx)
{
    const float pad_x = ui_style().outliner.pad_x;
    drivers::GitDriver& git = *ctx.git;
    ImGui::Indent(pad_x);
    ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x - pad_x);
    ImGui::BeginDisabled(git.busy);

    if (cached.state == GitState::NO_GIT) {
        ImGui::TextWrapped("Git for Windows isn't installed. Version control needs it to run.");
        ui_link("Download Git for Windows", GIT_DOWNLOAD_URL);
        if (ImGui::Button(ICON_FA_ROTATE "  Retry")) git.refresh();
    } else if (!_identity()) {
        ImGui::TextWrapped("Your name and email are attached to every commit.");
        ImGui::SetNextItemWidth(-pad_x);
        ImGui::InputTextWithHint("##vc_name", "Name", user_name, FIELD_SIZE);
        ImGui::SetNextItemWidth(-pad_x);
        ImGui::InputTextWithHint("##vc_email", "Email", user_email, FIELD_SIZE);
        ImGui::BeginDisabled(user_name[0] == '\0' || user_email[0] == '\0');
        if (ImGui::Button(ICON_FA_USER "  Save Identity")) git.identity_set(user_name, user_email);
        ImGui::EndDisabled();
    } else if (cached.state == GitState::NO_REPO) {
        ImGui::TextWrapped("This project doesn't have a timeline yet. Starting one adds a README.md and an empty .gitignore, then makes the first commit.");
        ImGui::BeginDisabled(cached.problem == drivers::GitDriver::Problem::NO_FOLDER);
        if (ImGui::Button(ICON_FA_CLOCK_ROTATE_LEFT "  Start Timeline")) git.repo_create(ctx.project ? ctx.project->name : std::string("Project"));
        ImGui::EndDisabled();
    }

    ImGui::EndDisabled();
    ImGui::PopTextWrapPos();
    ImGui::Unindent(pad_x);
}

void VersionControlPanel::_draw_toolbar(EditorContext& ctx)
{
    const UiStyle& ui = ui_style();
    drivers::GitDriver& git = *ctx.git;
    const float pad_x = ui.outliner.pad_x;
    const float bw = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const bool busy = git.busy;
    const bool ready = cached.state == GitState::READY;
    const uint32_t change_count = (uint32_t)cached.changes.size();
    const uint32_t up_count = (uint32_t)cached.unuploaded.size();
    const ImGuiHoveredFlags tip = ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled;
    char label[64];

    ui_push_buttons(UiButton::GHOST);
    ImGui::BeginDisabled(busy);

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad_x);
    if (ImGui::Button(ICON_FA_ROTATE "##vc_refresh", ImVec2(bw, bw))) git.refresh();
    if (ImGui::IsItemHovered(tip)) ImGui::SetTooltip(busy ? "Working..." : "Refresh timeline");

    ImGui::SameLine();
    ImGui::BeginDisabled(!ready);
    std::snprintf(label, sizeof(label), ICON_FA_CLOUD_ARROW_UP "  Upload (%u)##vc_upload", up_count);
    if (ImGui::Button(label, ImVec2(0.0f, bw))) git.upload();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(tip)) {
        if (!ready) ImGui::SetTooltip("Connect to GitHub first");
        else if (up_count > 0) ImGui::SetTooltip("Upload %u commit(s) to GitHub", up_count);
        else ImGui::SetTooltip("Everything is uploaded");
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!ready);
    std::snprintf(label, sizeof(label), ICON_FA_CLOUD_ARROW_DOWN "  Download (%d)##vc_download", cached.incoming);
    if (ImGui::Button(label, ImVec2(0.0f, bw)) && git.fetch()) download_pending = true;
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(tip)) {
        if (!ready) ImGui::SetTooltip("Connect to GitHub first");
        else if (cached.incoming > 0) ImGui::SetTooltip("Download %d new commit(s) from GitHub", cached.incoming);
        else ImGui::SetTooltip("Download new commits from GitHub");
    }

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad_x);
    ui_field_push_style();
    ImGui::SetNextItemWidth(-(bw * 3.0f + spacing * 3.0f + pad_x));
    const bool enter = ImGui::InputTextWithHint("##vc_message", "What changed?", message, MESSAGE_SIZE, ImGuiInputTextFlags_EnterReturnsTrue);
    ui_field_pop_style();
    if (enter) _commit(ctx);

    ImGui::SameLine();
    ImGui::BeginDisabled(_message_blank() || change_count == 0);
    if (ImGui::Button(ICON_FA_FLOPPY_DISK "##vc_save", ImVec2(bw, bw))) _commit(ctx);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(tip)) ImGui::SetTooltip(change_count == 0 ? "Nothing to commit" : (_message_blank() ? "Describe what changed first" : "Commit"));

    ImGui::SameLine();
    ImGui::BeginDisabled(change_count == 0 || !ctx.reload_project_callback);
    ImGui::PushStyleColor(ImGuiCol_Text, ui.color.danger);
    const bool discard = ImGui::Button(ICON_FA_TRASH "##vc_discard", ImVec2(bw, bw));
    ImGui::PopStyleColor();
    if (discard) {
        if (drivers::Win32Dialogs::confirm(L"Discard Changes", L"Discard all uncommitted changes?", L"The project goes back to the commit you're on. Every change since then is lost and new files are deleted. This can't be undone.")) {
            drivers::GitDriver* g = ctx.git;
            ctx.reload_project_callback([g] { g->discard_now(); });
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(tip)) ImGui::SetTooltip("Discard changes");

    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, change_count > 0 ? ui.color.text : ui.color.text_dim);
    ImGui::Button(ICON_FA_CIRCLE_QUESTION "##vc_changes", ImVec2(bw, bw));
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();
        if (change_count == 0) {
            ImGui::TextDisabled("No changes since your last commit");
        } else {
            ImGui::Text("%u change(s)", change_count);
            ImGui::Separator();
            const uint32_t shown = std::min(change_count, CHANGES_TOOLTIP_MAX);
            for (uint32_t i = 0; i < shown; i++) ImGui::TextUnformatted(cached.changes[i].c_str() + 3);
            if (change_count > shown) ImGui::TextDisabled("+%u more", change_count - shown);
        }
        ImGui::EndTooltip();
    }
    ui_pop_buttons();
}

bool VersionControlPanel::_message_blank() const
{
    for (const char* c = message; *c; c++) {
        if (!std::isspace((unsigned char)*c)) return false;
    }
    return true;
}

void VersionControlPanel::_draw_banner(EditorContext& ctx)
{
    using Problem = drivers::GitDriver::Problem;
    const UiStyle& ui = ui_style();
    const float pad_x = ui.outliner.pad_x;
    drivers::GitDriver& git = *ctx.git;
    const bool busy = git.busy;
    if (cached.operation == drivers::GitDriver::Operation::NONE && cached.error.empty()) return;

    ImGui::Indent(pad_x);
    ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x - pad_x);
    ImGui::BeginDisabled(busy);

    if (cached.operation != drivers::GitDriver::Operation::NONE) {
        const char* op = drivers::GitDriver::operation_name(cached.operation);
        ImGui::PushStyleColor(ImGuiCol_Text, ui.color.warning);
        ImGui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION "  A %s started outside the editor isn't finished. Committing and switching may fail until it's aborted.", op);
        ImGui::PopStyleColor();
        ImGui::BeginDisabled(!ctx.reload_project_callback);
        if (ImGui::SmallButton("Abort")) {
            if (drivers::Win32Dialogs::confirm(L"Abort", L"Abort the unfinished operation?", L"The project goes back to how it was before it started. Changes it made are discarded.")) {
                drivers::GitDriver* g = ctx.git;
                ctx.reload_project_callback([g] { g->operation_abort_now(); });
            }
        }
        ImGui::EndDisabled();
    }

    if (!cached.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui.color.warning);
        ImGui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION "  %s", cached.error.c_str());
        ImGui::PopStyleColor();
        const Problem p = cached.problem;
        bool first = true;
        auto next = [&]() { if (!first) ImGui::SameLine(); first = false; };
        if ((p == Problem::REMOTE_MISSING || p == Problem::AUTH) && !cached.remote_url.empty()) {
            next();
            if (ImGui::SmallButton("Change URL")) {
                editing_remote = true;
                std::snprintf(remote_url, sizeof(remote_url), "%s", cached.remote_url.c_str());
            }
            next();
            if (ImGui::SmallButton("Disconnect")) {
                if (drivers::Win32Dialogs::confirm(L"Disconnect", L"Disconnect from GitHub?", L"Your commits stay here. Nothing on GitHub is deleted. You can connect a repository again at any time.")) {
                    git.remote_remove();
                    editing_remote = false;
                }
            }
        }
        if (p == Problem::LOCKED) {
            next();
            if (ImGui::SmallButton("Unlock")) git.lock_clear();
        }
        next();
        if (ImGui::SmallButton("Dismiss")) git.error_dismiss();
    }

    ImGui::EndDisabled();
    ImGui::PopTextWrapPos();
    ImGui::Unindent(pad_x);
    ImGui::Spacing();
}

void VersionControlPanel::_draw_remote(EditorContext& ctx)
{
    const float pad_x = ui_style().outliner.pad_x;
    drivers::GitDriver& git = *ctx.git;
    ImGui::Indent(pad_x);
    ImGui::BeginDisabled(git.busy);
    ImGui::TextDisabled(editing_remote ? "New GitHub repository URL:" : "Back up to GitHub: paste an empty repository's URL.");
    const ImGuiStyle& style = ImGui::GetStyle();
    const float connect_w = ImGui::CalcTextSize("Connect").x + style.FramePadding.x * 2.0f;
    const float cancel_w = editing_remote ? ImGui::CalcTextSize("Cancel").x + style.FramePadding.x * 2.0f + style.ItemSpacing.x : 0.0f;
    ImGui::SetNextItemWidth(-(connect_w + cancel_w + style.ItemSpacing.x + pad_x));
    const bool enter = ImGui::InputTextWithHint("##vc_remote", "https://github.com/you/project.git", remote_url, FIELD_SIZE, ImGuiInputTextFlags_EnterReturnsTrue);
    bool blank = true;
    for (const char* c = remote_url; *c; c++) blank &= std::isspace((unsigned char)*c) != 0;
    ImGui::SameLine();
    ImGui::BeginDisabled(blank);
    if (ImGui::Button("Connect") || (enter && !blank)) {
        git.remote_set(remote_url);
        editing_remote = false;
    }
    ImGui::EndDisabled();
    if (editing_remote) {
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) editing_remote = false;
    }
    ImGui::EndDisabled();
    ImGui::Unindent(pad_x);
    ImGui::Spacing();
}

void VersionControlPanel::_draw_graph(EditorContext& ctx, float p_footer_h)
{
    const UiStyle& ui = ui_style();
    const float font = ImGui::GetFontSize();
    const float row_h = ImFloor(font * 1.75f);
    const float lane_w = ImFloor(font * 1.45f);
    const float radius = font * 0.34f;
    const float thick = ImMax(2.0f, font * 0.15f);
    const float corner_max = lane_w;
    const float kappa = 0.552f;
    const uint32_t n = (uint32_t)cached.commits.size();
    const uint32_t rows = n + 1;
    const ImU32 gray = ui.color.text_dim;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("##vc_graph", ImVec2(0.0f, -p_footer_h), ImGuiChildFlags_None);
    ImGui::PopStyleVar();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float scroll = ImGui::GetScrollY();
    const float view_h = ImGui::GetWindowHeight();

    ImGui::InvisibleButton("##vc_rows", ImVec2(ImMax(1.0f, width), row_h * (float)rows), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    int32_t hover_row = NONE;
    if (hovered) {
        const int32_t r = (int32_t)ImFloor((ImGui::GetMousePos().y - origin.y) / row_h);
        if (r >= 0 && r < (int32_t)n) hover_row = r;
    }
    if (hover_row != NONE && (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))) {
        selected = hover_row;
        selected_hash = cached.commits[(size_t)hover_row].hash;
    }
    if (hover_row != NONE && ImGui::IsItemClicked(ImGuiMouseButton_Right) && groups[(size_t)row_group[(size_t)hover_row]].branch) {
        menu_row = hover_row;
        ImGui::OpenPopup("##vc_commit_menu");
    }
    if (hover_row != NONE && hover_row != head_row && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !ctx.git->busy) {
        _go_to(ctx, cached.commits[(size_t)hover_row].hash);
    }
    if (ImGui::IsWindowFocused() && n > 0) {
        int32_t next = selected;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) next = selected == NONE ? 0 : ImMin(selected + 1, (int32_t)n - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) next = selected == NONE ? 0 : ImMax(selected - 1, 0);
        if (next != selected) {
            selected = next;
            selected_hash = cached.commits[(size_t)next].hash;
            scroll_to_selected = true;
        }
        if (selected != NONE && ImGui::IsKeyPressed(ImGuiKey_Enter) && groups[(size_t)row_group[(size_t)selected]].branch) {
            menu_row = selected;
            ImGui::OpenPopup("##vc_commit_menu");
        }
    }
    if (scroll_to_selected && selected != NONE) {
        const float top = row_h * (float)selected;
        if (top < scroll) ImGui::SetScrollY(top);
        else if (top + row_h > scroll + view_h) ImGui::SetScrollY(top + row_h - view_h);
        scroll_to_selected = false;
    }

    const uint32_t first = (uint32_t)ImMax(0.0f, ImFloor(scroll / row_h) - 1.0f);
    const uint32_t last = (uint32_t)ImMin((float)rows, ImCeil((scroll + view_h) / row_h) + 1.0f);
    auto visible = [&](uint32_t p_lo, uint32_t p_hi) { return p_hi >= first && p_lo <= last; };
    auto lane_x = [&](uint32_t p_lane) { return ImFloor(origin.x + lane_w * 0.75f + lane_w * (float)p_lane) + 0.5f; };
    auto row_y = [&](uint32_t p_row) { return ImFloor(origin.y + row_h * (float)p_row + row_h * 0.5f) + 0.5f; };

    for (uint32_t r = first; r < last && r < n; r++) {
        const ImVec2 a(origin.x, origin.y + row_h * (float)r);
        const ImVec2 b(origin.x + width, a.y + row_h);
        if ((int32_t)r == selected) dl->AddRectFilled(a, b, ImGui::GetColorU32(ImGuiCol_Header));
        else if ((int32_t)r == hover_row) dl->AddRectFilled(a, b, ImGui::GetColorU32(ImGuiCol_HeaderHovered, 0.5f));
    }

    if (main_group != NONE && n > 0) {
        const Group& m = groups[(size_t)main_group];
        if (visible(m.last_row, n)) dl->AddLine(ImVec2(lane_x(m.lane), row_y(m.last_row)), ImVec2(lane_x(m.lane), row_y(n)), gray, thick);
    }

    for (const Group& g : groups) {
        if (g.last_row <= g.first_row || !visible(g.first_row, g.last_row)) continue;
        const float x = lane_x(g.lane);
        dl->AddLine(ImVec2(x, row_y(g.first_row)), ImVec2(x, row_y(g.last_row)), g.color, thick);
    }

    for (const Edge& e : edges) {
        if (!visible(e.child, e.parent)) continue;
        const Group& gc = groups[(size_t)row_group[e.child]];
        const Group& gp = groups[(size_t)row_group[e.parent]];
        const float xc = lane_x(gc.lane);
        const float xp = lane_x(gp.lane);
        const float yc = row_y(e.child);
        const float yp = row_y(e.parent);
        if (gc.lane == gp.lane) {
            dl->AddLine(ImVec2(xc, yc), ImVec2(xp, yp), e.merge ? gp.color : gc.color, thick);
            continue;
        }
        const float r = ImMin(corner_max, ImMin(std::fabs(yp - yc), std::fabs(xp - xc)));
        if (!e.merge) {
            const float dir = xp > xc ? 1.0f : -1.0f;
            const ImVec2 p0(xc, yp - r);
            const ImVec2 p3(xc + dir * r, yp);
            dl->AddLine(ImVec2(xc, yc), p0, gc.color, thick);
            dl->AddBezierCubic(p0, ImVec2(xc, yp - r + r * kappa), ImVec2(p3.x - dir * r * kappa, yp), p3, gc.color, thick);
            dl->AddLine(p3, ImVec2(xp, yp), gc.color, thick);
        } else {
            const float dir = xc > xp ? 1.0f : -1.0f;
            const ImVec2 p0(xp, yc + r);
            const ImVec2 p3(xp + dir * r, yc);
            dl->AddLine(ImVec2(xp, yp), p0, gp.color, thick);
            dl->AddBezierCubic(p0, ImVec2(xp, yc + r - r * kappa), ImVec2(p3.x - dir * r * kappa, yc), p3, gp.color, thick);
            dl->AddLine(p3, ImVec2(xc, yc), gp.color, thick);
        }
    }

    for (uint32_t r = first; r < last && r < rows; r++) {
        const float y = row_y(r);
        if (r == n) {
            dl->AddCircleFilled(ImVec2(lane_x(0), y), radius, gray);
            continue;
        }
        const Group& g = groups[(size_t)row_group[r]];
        const ImVec2 c(lane_x(g.lane), y);
        dl->AddCircleFilled(c, radius, g.color);
        if (row_unuploaded[r]) dl->AddCircleFilled(c, ImMax(1.0f, radius - thick), ui.color.panel);
        if ((int32_t)r == head_row) dl->AddCircle(c, radius + thick * 1.75f, ui.color.text, 0, thick);
    }

    const float text_x = origin.x + lane_w * ((float)lane_count + 0.75f);
    const float right = origin.x + width - ui.outliner.pad_x;
    char ago[32];
    for (uint32_t r = first; r < last && r < n; r++) {
        const drivers::GitDriver::Commit& c = cached.commits[r];
        const float cy = row_y(r);
        const float ty = cy - font * 0.5f;

        _time_ago(c.time, ago, sizeof(ago));
        const float meta_w = ImGui::CalcTextSize(ago).x;
        const float meta_x = ImMax(text_x, right - meta_w);
        dl->AddText(ImVec2(meta_x, ty), ui.color.text_dim, ago);

        const float limit = meta_x - ui.space.xxl;
        dl->PushClipRect(ImVec2(text_x, cy - row_h * 0.5f), ImVec2(ImMax(text_x, limit), cy + row_h * 0.5f), true);
        dl->AddText(ImVec2(text_x, ty), (int32_t)r == head_row ? ui.color.text : ImGui::GetColorU32(ImGuiCol_Text, 0.85f), c.summary.c_str());
        dl->PopClipRect();
    }

    if (hover_row != NONE && !ImGui::IsPopupOpen("##vc_commit_menu")) {
        const Group& g = groups[(size_t)row_group[(size_t)hover_row]];
        if (ImGui::BeginTooltip()) {
            if (g.branch) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(g.color), ICON_FA_CODE_BRANCH "  %s", g.name.c_str());
            else ImGui::TextDisabled("Earlier history");
            if (row_unuploaded[(size_t)hover_row]) ImGui::TextDisabled("Not uploaded");
            ImGui::TextDisabled(hover_row == head_row ? "You're here" : "Double-click to go here");
            ImGui::EndTooltip();
        }
    }

    _draw_commit_menu(ctx);
    ImGui::EndChild();
}

void VersionControlPanel::_draw_footer()
{
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ui_style().outliner.pad_x);
    const char* branch = cached.detached ? "Earlier point" : (cached.branch.empty() ? "No branch" : cached.branch.c_str());
    const char* summary = head_row != NONE ? cached.commits[(size_t)head_row].summary.c_str() : "No commits yet";
    ImGui::TextDisabled(ICON_FA_CODE_BRANCH "  %s  -  %s", branch, summary);
}

void VersionControlPanel::_draw_commit_menu(EditorContext& ctx)
{
    ui_menu_push_style();
    if (!ImGui::BeginPopup("##vc_commit_menu")) {
        ui_menu_pop_style();
        return;
    }
    if (menu_row == NONE || menu_row >= (int32_t)cached.commits.size()) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        ui_menu_pop_style();
        return;
    }
    const int32_t gi = row_group[(size_t)menu_row];
    const Group& g = groups[(size_t)gi];
    const drivers::GitDriver::Commit& c = cached.commits[(size_t)menu_row];
    const bool busy = ctx.git->busy;
    const bool can_reload = (bool)ctx.reload_project_callback;
    const bool main = _is_main(g.name);
    const bool on_branch = (cached.branch == g.name && !cached.detached) || (head_row != NONE && row_group[(size_t)head_row] == gi);
    const ImGuiHoveredFlags tip = ImGuiHoveredFlags_AllowWhenDisabled;
    drivers::GitDriver* git = ctx.git;

    bool local_tip = false;
    for (const drivers::GitDriver::Ref& r : cached.refs) local_tip |= !r.remote && r.name == g.name && r.hash == c.hash;
    uint32_t commits = 0;
    for (uint32_t r = g.first_row; r <= g.last_row && r < (uint32_t)row_group.size(); r++) commits += row_group[r] == gi ? 1u : 0u;
    const bool only_commit = commits == 1 && !c.parents.empty();

    if (g.branch) {
        bool local_exists = false;
        for (const drivers::GitDriver::Ref& r : cached.refs) local_exists |= !r.remote && r.name == g.name;
        if (ImGui::MenuItem(ICON_FA_PEN "  Rename Branch", nullptr, false, !main && local_exists && !busy)) {
            rename_branch = g.name;
            std::snprintf(rename_buffer, sizeof(rename_buffer), "%s", g.name.c_str());
            rename_open = true;
        }

        if (!main && !local_exists && ImGui::IsItemHovered(tip)) ImGui::SetTooltip("This branch only exists on GitHub. Go to it first to rename it.");

        ImGui::MenuItem(ICON_FA_CODE_MERGE "  Merge into Main", nullptr, false, false);
        if (ImGui::IsItemHovered(tip)) ImGui::SetTooltip("Coming soon");

        const bool commit_deletable = local_tip && !c.parents.empty() && !(main && only_commit) && can_reload;
        if (ImGui::MenuItem(ICON_FA_ERASER "  Delete Commit", nullptr, false, commit_deletable && !busy)) {
            const bool dirty = !git->changes_now().empty();
            const std::wstring dirty_note = dirty ? L" Your uncommitted changes are discarded too." : L"";
            if (only_commit && !main) {
                std::wstring body = std::wstring(L"This is the only commit on the branch, so the whole branch is deleted") + (g.uploaded ? L", including its copy on GitHub" : L"") + L". You'll be moved to main." + dirty_note + L" This can't be undone.";
                if (drivers::Win32Dialogs::confirm(L"Delete Commit", L"Delete this commit?", body.c_str())) {
                    const std::string name = g.name;
                    ctx.reload_project_callback([git, name, dirty] {
                        if (dirty && !git->discard_now()) return;
                        git->branch_delete_now(name, true);
                    });
                }
            } else {
                std::wstring body = std::wstring(L"The commit is removed and you're moved to the previous commit on this branch.") + (row_unuploaded[(size_t)menu_row] ? L"" : L" It was uploaded, so it's removed from GitHub too.") + dirty_note + L" This can't be undone.";
                if (drivers::Win32Dialogs::confirm(L"Delete Commit", L"Delete this commit?", body.c_str())) {
                    const std::string name = g.name;
                    ctx.reload_project_callback([git, name, dirty] {
                        if (dirty && !git->discard_now()) return;
                        git->commit_delete_now(name);
                    });
                }
            }
        }
        if (!commit_deletable && ImGui::IsItemHovered(tip)) ImGui::SetTooltip(local_tip ? "The first commit can't be deleted" : "Only the newest commit on a branch can be deleted");

        if (ImGui::MenuItem(ICON_FA_TRASH "  Delete Branch", nullptr, false, !main && !busy && (!on_branch || can_reload))) {
            const bool dirty = on_branch && !git->changes_now().empty();
            const std::wstring dirty_note = dirty ? L" Your uncommitted changes are discarded too." : L"";
            wchar_t count[64];
            std::swprintf(count, 64, L"%u commit(s)", commits);
            std::wstring body = std::wstring(count) + L" only exist on this branch and will be lost." + (g.uploaded ? L" Its copy on GitHub is deleted too." : L" It was never uploaded, so there's no backup.") + (on_branch ? L" You'll be moved to main." + dirty_note : L"") + L" This can't be undone.";
            if (drivers::Win32Dialogs::confirm(L"Delete Branch", L"Delete this branch?", body.c_str())) {
                const std::string name = g.name;
                if (on_branch) {
                    ctx.reload_project_callback([git, name, dirty] {
                        if (dirty && !git->discard_now()) return;
                        git->branch_delete_now(name, true);
                    });
                } else {
                    git->branch_delete(name);
                }
            }
        }
        if (main && ImGui::IsItemHovered(tip)) ImGui::SetTooltip("Main can't be deleted");
    }

    ImGui::EndPopup();
    ui_menu_pop_style();
}

void VersionControlPanel::_draw_rename_popup(EditorContext& ctx)
{
    const char* id = "Rename Branch###vc_rename";
    if (rename_open && !ImGui::IsPopupOpen(id)) ImGui::OpenPopup(id);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) return;

    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
    const bool enter = ImGui::InputText("##vc_rename_field", rename_buffer, FIELD_SIZE, ImGuiInputTextFlags_EnterReturnsTrue);
    bool close = false;
    ImGui::BeginDisabled(rename_buffer[0] == '\0');
    if (ImGui::Button("Rename") || (enter && rename_buffer[0] != '\0')) {
        ctx.git->branch_rename(rename_branch, rename_buffer);
        close = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) close = true;
    if (close) {
        rename_open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

int32_t VersionControlPanel::_add_group(const std::string& p_name)
{
    groups.push_back({ p_name, _branch_color(p_name), 0, UINT32_MAX, 0, 0, 0, false, false });
    return (int32_t)groups.size() - 1;
}

void VersionControlPanel::_claim(int32_t p_group, int32_t p_row, const std::vector<int32_t>& p_first_parent)
{
    while (p_row != NONE && row_group[(size_t)p_row] == NONE) {
        row_group[(size_t)p_row] = p_group;
        p_row = p_first_parent[(size_t)p_row];
    }
}

void VersionControlPanel::_build_graph()
{
    using Commit = drivers::GitDriver::Commit;
    const std::vector<Commit>& commits = cached.commits;
    const uint32_t n = (uint32_t)commits.size();

    groups.clear();
    edges.clear();
    row_group.assign(n, NONE);
    row_unuploaded.assign(n, 0);
    lane_count = 1;
    main_group = NONE;
    head_row = NONE;

    std::unordered_map<std::string, int32_t> row_of;
    row_of.reserve(n);
    for (uint32_t i = 0; i < n; i++) row_of.emplace(commits[i].hash, (int32_t)i);
    auto find_row = [&](const std::string& p_hash) {
        const auto it = row_of.find(p_hash);
        return it == row_of.end() ? NONE : it->second;
    };

    std::vector<int32_t> first_parent(n, NONE);
    for (uint32_t i = 0; i < n; i++) {
        if (!commits[i].parents.empty()) first_parent[i] = find_row(commits[i].parents[0]);
    }
    head_row = find_row(cached.head);
    for (const std::string& h : cached.unuploaded) {
        const int32_t r = find_row(h);
        if (r != NONE) row_unuploaded[(size_t)r] = 1;
    }

    struct Branch {
        std::string name;
        int32_t local = NONE;
        std::vector<std::pair<std::string, int32_t>> remotes;
        int32_t newest = INT32_MAX;
    };

    std::vector<Branch> branches;
    for (const drivers::GitDriver::Ref& ref : cached.refs) {
        const int32_t row = find_row(ref.hash);
        if (row == NONE) continue;
        std::string logical = ref.name;
        if (ref.remote) {
            const size_t slash = logical.find('/');
            if (slash != std::string::npos) logical = logical.substr(slash + 1);
        }
        auto it = std::find_if(branches.begin(), branches.end(), [&](const Branch& l) { return l.name == logical; });
        if (it == branches.end()) {
            branches.push_back({});
            branches.back().name = logical;
            it = branches.end() - 1;
        }
        if (ref.remote) it->remotes.push_back({ ref.name, row });
        else it->local = row;
        it->newest = std::min(it->newest, row);
    }

    std::string main_name;
    for (const char* candidate : { "main", "master" }) {
        if (std::any_of(branches.begin(), branches.end(), [&](const Branch& l) { return l.name == candidate; })) {
            main_name = candidate;
            break;
        }
    }
    if (main_name.empty()) main_name = cached.branch.empty() ? (branches.empty() ? std::string() : branches.front().name) : cached.branch;

    std::sort(branches.begin(), branches.end(), [&](const Branch& a, const Branch& b) {
        const bool am = a.name == main_name, bm = b.name == main_name;
        if (am != bm) return am;
        const bool ac = a.name == cached.branch, bc = b.name == cached.branch;
        if (ac != bc) return ac;
        return a.newest < b.newest;
    });

    for (const Branch& l : branches) {
        int32_t start = l.local;
        for (const auto& [name, row] : l.remotes) {
            if (start == NONE) {
                start = row;
                continue;
            }
            if (row >= start) continue;
            int32_t walk = row;
            while (walk != NONE && walk < start) walk = first_parent[(size_t)walk];
            if (walk == start) start = row;
        }
        if (start == NONE) continue;
        const int32_t g = _add_group(l.name);
        groups[(size_t)g].branch = true;
        groups[(size_t)g].uploaded = !l.remotes.empty();
        if (l.name == main_name) main_group = g;
        _claim(g, start, first_parent);
        for (const auto& [name, row] : l.remotes) {
            if (row_group[(size_t)row] != NONE) continue;
            _claim(_add_group(name), row, first_parent);
        }
    }
    if (head_row != NONE && row_group[(size_t)head_row] == NONE) _claim(_add_group("HEAD"), head_row, first_parent);

    std::vector<std::string> merged_into(n);
    for (uint32_t i = 0; i < n; i++) {
        for (size_t k = 1; k < commits[i].parents.size(); k++) {
            const int32_t p = find_row(commits[i].parents[k]);
            if (p != NONE && merged_into[(size_t)p].empty()) merged_into[(size_t)p] = _merged_name(commits[i].summary);
        }
    }
    for (uint32_t r = 0; r < n; r++) {
        if (row_group[r] != NONE) continue;
        const std::string name = merged_into[r].empty() ? commits[r].hash.substr(0, 7) : merged_into[r];
        _claim(_add_group(name), (int32_t)r, first_parent);
    }
    if (main_group == NONE && n > 0) main_group = row_group[n - 1];

    for (uint32_t r = 0; r < n; r++) {
        Group& g = groups[(size_t)row_group[r]];
        g.first_row = std::min(g.first_row, r);
        g.last_row = std::max(g.last_row, r);
    }
    for (Group& g : groups) {
        g.top = g.first_row;
        g.bottom = g.last_row;
    }

    for (uint32_t i = 0; i < n; i++) {
        for (size_t k = 0; k < commits[i].parents.size(); k++) {
            const int32_t p = find_row(commits[i].parents[k]);
            if (p == NONE || row_group[(size_t)p] == row_group[i]) continue;
            edges.push_back({ i, (uint32_t)p, k > 0 });
            if (k == 0) {
                Group& g = groups[(size_t)row_group[i]];
                g.bottom = std::max(g.bottom, (uint32_t)p);
            } else {
                Group& g = groups[(size_t)row_group[(size_t)p]];
                g.top = std::min(g.top, i);
            }
        }
    }
    if (main_group != NONE) groups[(size_t)main_group].bottom = n;

    std::vector<uint32_t> order;
    for (uint32_t g = 0; g < (uint32_t)groups.size(); g++) {
        if ((int32_t)g != main_group) order.push_back(g);
    }
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        if (groups[a].top != groups[b].top) return groups[a].top < groups[b].top;
        return groups[a].bottom < groups[b].bottom;
    });
    std::vector<int64_t> lane_end = { INT64_MAX };
    for (const uint32_t g : order) {
        uint32_t lane = 1;
        while (lane < lane_end.size() && lane_end[lane] >= (int64_t)groups[g].top) lane++;
        if (lane == lane_end.size()) lane_end.push_back(0);
        lane_end[lane] = groups[g].bottom;
        groups[g].lane = lane;
    }
    lane_count = (uint32_t)lane_end.size();

    selected = selected_hash.empty() ? NONE : find_row(selected_hash);
    if (selected == NONE) {
        selected_hash.clear();
    }
}

ImU32 VersionControlPanel::_branch_color(const std::string& p_name)
{
    uint32_t h = hash_fnv32(p_name);
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB((float)(h % 360u) / 360.0f, 0.62f, 0.92f, r, g, b);
    return IM_COL32((int)(r * 255.0f), (int)(g * 255.0f), (int)(b * 255.0f), 255);
}

std::string VersionControlPanel::_merged_name(const std::string& p_summary)
{
    auto quoted = [&](const char* p_prefix) -> std::string {
        if (!p_summary.starts_with(p_prefix)) return {};
        const size_t start = std::strlen(p_prefix);
        const size_t end = p_summary.find('\'', start);
        return end == std::string::npos ? std::string() : p_summary.substr(start, end - start);
    };
    std::string name = quoted("Merge branch '");
    if (name.empty()) {
        name = quoted("Merge remote-tracking branch '");
        const size_t slash = name.find('/');
        if (slash != std::string::npos) name = name.substr(slash + 1);
    }
    if (name.empty() && p_summary.starts_with("Merge pull request #")) {
        const size_t from = p_summary.find(" from ");
        if (from != std::string::npos) {
            name = p_summary.substr(from + 6);
            const size_t slash = name.find('/');
            if (slash != std::string::npos) name = name.substr(slash + 1);
            const size_t space = name.find(' ');
            if (space != std::string::npos) name = name.substr(0, space);
        }
    }
    return name;
}

void VersionControlPanel::_time_ago(int64_t p_time, char* r_out, size_t p_size)
{
    const int64_t d = ImMax<int64_t>(0, (int64_t)std::time(nullptr) - p_time);
    if (d < 60) std::snprintf(r_out, p_size, "just now");
    else if (d < 3600) std::snprintf(r_out, p_size, "%lldm ago", (long long)(d / 60));
    else if (d < 86400) std::snprintf(r_out, p_size, "%lldh ago", (long long)(d / 3600));
    else if (d < 86400 * 30) std::snprintf(r_out, p_size, "%lldd ago", (long long)(d / 86400));
    else if (d < 86400 * 365) std::snprintf(r_out, p_size, "%lldmo ago", (long long)(d / (86400 * 30)));
    else std::snprintf(r_out, p_size, "%lldy ago", (long long)(d / (86400 * 365)));
}

}

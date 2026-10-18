#include <editor/tabs/tab_manager.h>
#include <editor/ui/tab_strip.h>
#include <cstdio>

namespace lumen {

void TabManager::add(EditorTab* p_tab, TabKind p_kind, Guid p_guid)
{
    if (EditorTab* t = find(p_kind, p_guid)) {
        focus(t);
        return;
    }
    p_tab->kind = p_kind;
    p_tab->guid = p_guid;
    p_tab->_owned = false;
    p_tab->_created = true;
    tabs.push_back(p_tab);
    focus(p_tab);
}

size_t TabManager::_index_of(TabKind p_kind, Guid p_guid) const
{
    for (size_t i = 0; i < tabs.size(); ++i) {
        if (tabs[i]->kind == p_kind && tabs[i]->guid == p_guid) return i;
    }
    return NPOS;
}

EditorTab* TabManager::find(TabKind p_kind, Guid p_guid) const
{
    const size_t i = _index_of(p_kind, p_guid);
    return i == NPOS ? nullptr : tabs[i];
}

bool TabManager::is_active(TabKind p_kind, Guid p_guid) const
{
    return active && active->kind == p_kind && active->guid == p_guid;
}

void TabManager::focus(EditorTab* p_tab)
{
    active = p_tab;
}

void TabManager::close(TabKind p_kind, Guid p_guid, bool p_force)
{
    close_requests.push_back({ p_kind, p_guid, p_force });
}

void TabManager::clear(EditorContext& ctx)
{
    while (!tabs.empty()) _remove_at(ctx, tabs.size() - 1);
    close_requests.clear();
}

void TabManager::draw_bar(ImVec2 p_min, ImVec2 p_max, float p_reserved_right, ImU32 p_bg, const std::function<void(ImVec2, ImVec2)>& p_block)
{
    ++_frame;

    float natural = 0.0f;
    for (EditorTab* t : tabs) natural += TabStrip::dock_natural_width(t->title(), t->closable());

    char label[192];
    TabStrip strip;
    strip.begin("##titlebar_tabs", p_min, p_max, TabStrip::Edge::TOP, natural, p_reserved_right, p_bg);
    for (EditorTab* t : tabs) {
        const bool closable = t->closable();
        std::snprintf(label, sizeof(label), "%s###tab_%u_%016llx", t->title(), (unsigned)t->kind, (unsigned long long)t->guid.value);

        bool close_clicked = false;
        if (strip.dock_tab(label, t == active, closable ? &close_clicked : nullptr, closable && t->dirty())) active = t;
        p_block(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        if (close_clicked) close(t->kind, t->guid);
    }
    strip.end();
}

void TabManager::update(EditorContext& ctx, float p_dt)
{
    _process_closes(ctx);

    for (size_t i = 0; i < tabs.size();) {
        EditorTab* t = tabs[i];
        if (t->_created) { ++i; continue; }
        if (t->create(ctx) == Error::OK) {
            t->_created = true;
            ++i;
            continue;
        }
        _remove_at(ctx, i);
    }

    for (EditorTab* t : tabs) t->on_tick(ctx);

    _resolve_active();
    if (active) active->on_update(ctx, p_dt);
}

void TabManager::_process_closes(EditorContext& ctx)
{
    for (const CloseRequest& r : close_requests) {
        const size_t i = _index_of(r.kind, r.guid);
        if (i == NPOS) continue;
        EditorTab* t = tabs[i];
        if (!t->closable()) continue;

        if (!r.force && t->dirty()) {
            active = t;
            // you have unsaved changes
            continue;
        }
        _remove_at(ctx, i);
    }
    close_requests.clear();
}

void TabManager::_remove_at(EditorContext& ctx, size_t p_index)
{
    EditorTab* t = tabs[p_index];
    if (active == t) active = nullptr;
    tabs.erase(tabs.begin() + p_index);

    if (!t->_owned) return;
    if (t->_created) t->destroy(ctx);
    for (size_t i = 0; i < _owned.size(); ++i) {
        if (_owned[i].get() == t) {
            _owned.erase(_owned.begin() + i);
            break;
        }
    }
}

void TabManager::_resolve_active()
{
    if (!active) {
        for (EditorTab* t : tabs) {
            if (!active || t->_last_selected > active->_last_selected) active = t;
        }
    }
    if (!active) return;

    active->_last_selected = _frame;
}

}
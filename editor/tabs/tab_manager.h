#pragma once
#include <editor/tabs/editor_tab.h>
#include <imgui.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace lumen {

struct TabManager
{
    static constexpr size_t NPOS = SIZE_MAX;

    struct CloseRequest
    {
        TabKind kind;
        Guid guid;
        bool force = false;
    };

    std::vector<EditorTab*> tabs;
    std::vector<std::unique_ptr<EditorTab>> _owned;
    std::vector<CloseRequest> close_requests;
    EditorTab* active = nullptr;
    uint64_t _frame = 0;

    template <class T>
    T* open(Guid p_guid = {}) {
        if (EditorTab* t = find(T::KIND, p_guid)) {
            focus(t);
            return static_cast<T*>(t);
        }
        auto up = std::make_unique<T>();
        up->kind = T::KIND;
        up->guid = p_guid;
        up->_owned = true;
        T* raw = up.get();
        _owned.push_back(std::move(up));
        tabs.push_back(raw);
        focus(raw);
        return raw;
    }

    void add(EditorTab* p_tab, TabKind p_kind, Guid p_guid = {});

    EditorTab* find(TabKind p_kind, Guid p_guid) const;
    bool is_active(TabKind p_kind, Guid p_guid = {}) const;
    void focus(EditorTab* p_tab);
    void close(TabKind p_kind, Guid p_guid, bool p_force = false);
    void clear(EditorContext& ctx);

    void draw_bar(ImVec2 p_min, ImVec2 p_max, float p_reserved_right, ImU32 p_bg, const std::function<void(ImVec2, ImVec2)>& p_block);
    void update(EditorContext& ctx, float p_dt);

    size_t _index_of(TabKind p_kind, Guid p_guid) const;
    void _process_closes(EditorContext& ctx);
    void _remove_at(EditorContext& ctx, size_t p_index);
    void _resolve_active();
};

}
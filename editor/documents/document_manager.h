#pragma once
#include <editor/documents/editor_document.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace lumen {

struct DocumentManager
{
    static constexpr size_t NPOS = SIZE_MAX;

    struct CloseRequest {
        DocumentKind kind;
        Guid guid;
        bool force = false;
    };

    std::vector<std::unique_ptr<EditorDocument>> documents;
    static constexpr uint32_t DESTROY_DELAY_FRAMES = 4;

    struct Retired {
        std::unique_ptr<EditorDocument> document;
        uint32_t frames = 0;
    };

    std::vector<CloseRequest> close_requests;
    std::vector<Retired> retired;

    template <class T>
    T* open(Guid p_guid = {}) {
        if (EditorDocument* w = find(T::KIND, p_guid)) {
            focus(w);
            return static_cast<T*>(w);
        }
        auto up = std::make_unique<T>();
        up->kind = T::KIND;
        up->guid = p_guid;
        T* raw = up.get();
        documents.push_back(std::move(up));
        return raw;
    }

    size_t _index_of(DocumentKind p_kind, Guid p_guid) const;
    void _process_closes(EditorContext& ctx);
    static bool _confirm_discard(const EditorDocument& p_document);
    void _remove_at(EditorContext& ctx, size_t p_index, bool p_now = false);
    void _collect(EditorContext& ctx, bool p_all);

    EditorDocument* find(DocumentKind p_kind, Guid p_guid) const;
    void focus(EditorDocument* p_document);
    void close(DocumentKind p_kind, Guid p_guid, bool p_force = false);
    void clear(EditorContext& ctx);
    bool confirm_discard_all() const;
    bool any_dirty() const;
    void update(EditorContext& ctx);
};

}

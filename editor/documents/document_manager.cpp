#include <editor/documents/document_manager.h>
#include <imgui_internal.h>
#include <editor/ui/ui_style.h>
#include <drivers/windows/dialogs_win32.h>
#include <drivers/windows/window_driver_win32.h>
#include <string>
#include <cstdio>

namespace lumen {

size_t DocumentManager::_index_of(DocumentKind p_kind, Guid p_guid) const
{
    for (size_t i = 0; i < documents.size(); ++i) {
        if (documents[i]->kind == p_kind && documents[i]->guid == p_guid) return i;
    }
    return NPOS;
}

EditorDocument* DocumentManager::find(DocumentKind p_kind, Guid p_guid) const
{
    const size_t i = _index_of(p_kind, p_guid);
    return i == NPOS ? nullptr : documents[i].get();
}

void DocumentManager::focus(EditorDocument* p_document)
{
    if (p_document) p_document->_focus_request = true;
}

void DocumentManager::close(DocumentKind p_kind, Guid p_guid, bool p_force)
{
    close_requests.push_back({ p_kind, p_guid, p_force });
}

void DocumentManager::clear(EditorContext& ctx)
{
    while (!documents.empty()) _remove_at(ctx, documents.size() - 1, true);
    _collect(ctx, true);
    close_requests.clear();
}

bool DocumentManager::any_dirty() const
{
    for (const auto& w : documents) if (w->dirty()) return true;
    return false;
}

void DocumentManager::update(EditorContext& ctx)
{
    _collect(ctx, false);
    _process_closes(ctx);

    for (size_t i = 0; i < documents.size();) {
        EditorDocument* w = documents[i].get();
        if (w->_created) { ++i; continue; }
        if (w->create(ctx) == Error::OK) {
            w->_created = true;
            ++i;
            continue;
        }
        _remove_at(ctx, i, true);
    }

    for (auto& w : documents) w->on_tick(ctx);
}

void DocumentManager::_process_closes(EditorContext& ctx)
{
    for (const CloseRequest& r : close_requests) {
        const size_t i = _index_of(r.kind, r.guid);
        if (i == NPOS) continue;
        EditorDocument* w = documents[i].get();
        if (!r.force && w->dirty() && !_confirm_discard(ctx, *w)) continue;
        _remove_at(ctx, i);
    }
    close_requests.clear();
}

bool DocumentManager::_confirm_discard(EditorContext& ctx, EditorDocument& r_document)
{
    using Choice = drivers::Win32Dialogs::SaveChoice;
    const std::wstring heading = L"'" + drivers::WindowDriverWin32::utf8_to_wide(r_document.title()) + L"' has unsaved changes";
    const Choice choice = drivers::Win32Dialogs::ask_save(L"Lumen Editor", heading.c_str(), L"Do you want to save them before closing?", L"Save and Close");
    if (choice == Choice::CANCEL) return false;
    if (choice == Choice::SAVE) return r_document.save(ctx) == Error::OK;
    return true;
}

bool DocumentManager::confirm_discard_all(EditorContext& ctx)
{
    using Choice = drivers::Win32Dialogs::SaveChoice;
    uint32_t count = 0;
    EditorDocument* only = nullptr;
    for (const auto& w : documents) {
        if (!w->dirty()) continue;
        count++;
        only = w.get();
    }
    if (count == 0) return true;
    if (count == 1) return _confirm_discard(ctx, *only);
    const std::wstring heading = std::to_wstring(count) + L" documents have unsaved changes";
    const Choice choice = drivers::Win32Dialogs::ask_save(L"Lumen Editor", heading.c_str(), L"Do you want to save them before closing?", L"Save All and Close");
    if (choice == Choice::CANCEL) return false;
    if (choice == Choice::DISCARD) return true;
    bool ok = true;
    for (const auto& w : documents) {
        if (w->dirty() && w->save(ctx) != Error::OK) ok = false;
    }
    return ok;
}

void DocumentManager::_remove_at(EditorContext& ctx, size_t p_index, bool p_now)
{
    std::unique_ptr<EditorDocument> w = std::move(documents[p_index]);
    documents.erase(documents.begin() + (std::ptrdiff_t)p_index);
    if (p_now) {
        if (w->_created) w->destroy(ctx);
        return;
    }
    retired.push_back({ std::move(w), 0 });
}

void DocumentManager::_collect(EditorContext& ctx, bool p_all)
{
    for (size_t i = 0; i < retired.size();) {
        Retired& r = retired[i];
        if (!p_all && ++r.frames < DESTROY_DELAY_FRAMES) { ++i; continue; }
        if (r.document->_created) r.document->destroy(ctx);
        retired.erase(retired.begin() + (std::ptrdiff_t)i);
    }
}

}

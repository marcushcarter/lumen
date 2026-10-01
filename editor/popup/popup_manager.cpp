// #include <editor/popup/popup_manager.h>

// namespace lumen {

// void PopupManager::register_popup(std::unique_ptr<Popup> p)
// {
//     popups.push_back(std::move(p));
// }

// void PopupManager::open(std::string_view p_name)
// {
//     for (auto& p : popups) {
//         if (p_name == p->name()) {
//             p->open = true;
//             return;
//         }
//     }
// }

// void PopupManager::draw(EditorContext& ctx)
// {
//     for (auto& p : popups) p->draw(ctx);
// }

// }
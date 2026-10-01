// #pragma once
// #include <editor/popup/popup.h>
// #include <editor/editor_context.h>
// #include <vector>
// #include <memory>
// #include <string_view>

// namespace lumen {

// struct PopupManager
// {
//     std::vector<std::unique_ptr<Popup>> popups;

//     void register_popup(std::unique_ptr<Popup> p);
//     void open(std::string_view p_name);
//     void draw(EditorContext& ctx);

//     template <typename T>
//     T* get(std::string_view p_name) {
//         for (auto& p : popups) if (p_name == p->name()) return static_cast<T*>(p.get());
//         return nullptr;
//     }
// };
    
// }
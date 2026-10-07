#include <drivers/windows/dialogs_win32.h>
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <filesystem>

namespace lumen::drivers {

std::wstring Win32Dialogs::save_file(const wchar_t* p_filter, const wchar_t* p_default_ext)
{
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = p_filter;
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = p_default_ext;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (GetSaveFileNameW(&ofn)) return std::wstring(path);
    return {};
}

std::wstring Win32Dialogs::open_file(const wchar_t* p_filter)
{
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = p_filter;
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) return std::wstring(path);
    return {};
}

std::vector<std::wstring> Win32Dialogs::open_files(const wchar_t* p_filter)
{
    std::vector<wchar_t> buf(64 * 1024, 0);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = p_filter;
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = static_cast<DWORD>(buf.size());
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return {};
    std::vector<std::wstring> out;
    const wchar_t* p = buf.data();
    std::wstring first = p;
    p += first.size() + 1;
    if (*p == L'\0') {
        out.push_back(std::move(first));
    } else {
        const std::filesystem::path dir = first;
        while (*p) {
            out.push_back((dir / p).wstring());
            p += std::wcslen(p) + 1;
        }
    }
    return out;
}

std::wstring Win32Dialogs::open_folder(const wchar_t* p_title)
{
    std::wstring result;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool needs_uninit = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return result;
    IFileOpenDialog* dialog = nullptr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (SUCCEEDED(hr)) {
        DWORD opts = 0;
        dialog->GetOptions(&opts);
        dialog->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        if (p_title) dialog->SetTitle(p_title);
        dialog->SetOkButtonLabel(L"Select Folder");
        if (dialog->Show(GetActiveWindow()) == S_OK) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    result.assign(path);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
    }
    if (needs_uninit) CoUninitialize();
    return result;
}

bool Win32Dialogs::warning(const wchar_t* p_title, const wchar_t* p_heading, const wchar_t* p_body, bool p_dont_show_option)
{
    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.hwndParent = GetActiveWindow();
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    config.dwCommonButtons = TDCBF_OK_BUTTON;
    config.pszWindowTitle = p_title;
    config.pszMainIcon = TD_WARNING_ICON;
    config.pszMainInstruction = p_heading;
    config.pszContent = p_body;
    if (p_dont_show_option) config.pszVerificationText = L"Don't show this again";

    BOOL dont_show = FALSE;
    HRESULT hr = TaskDialogIndirect(&config, nullptr, nullptr, &dont_show);
    if (FAILED(hr)) {
        MessageBoxW(config.hwndParent, p_body, p_title, MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
        return false;
    }
    return dont_show == TRUE;
}

bool Win32Dialogs::confirm(const wchar_t* p_title, const wchar_t* p_heading, const wchar_t* p_body)
{
    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.hwndParent = GetActiveWindow();
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    config.dwCommonButtons = TDCBF_OK_BUTTON | TDCBF_CANCEL_BUTTON;
    config.nDefaultButton = IDOK;
    config.pszWindowTitle = p_title;
    config.pszMainIcon = TD_WARNING_ICON;
    config.pszMainInstruction = p_heading;
    config.pszContent = p_body;

    int button = IDCANCEL;
    HRESULT hr = TaskDialogIndirect(&config, &button, nullptr, nullptr);
    if (FAILED(hr)) button = MessageBoxW(config.hwndParent, p_body, p_title, MB_OKCANCEL | MB_ICONWARNING | MB_SETFOREGROUND);
    return button == IDOK;
}

}
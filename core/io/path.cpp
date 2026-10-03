#include <core/io/path.h>
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>

// #include <string>
// #include <vector>
// #include <algorithm>
// #include <system_error>

namespace lumen {

static std::filesystem::path _known_folder(const KNOWNFOLDERID& p_id, std::wstring_view p_subpath)
{
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(p_id, 0, nullptr, &raw)) || !raw) {
        if (raw) CoTaskMemFree(raw);
        return {};
    }
    std::filesystem::path dir = raw;
    CoTaskMemFree(raw);

    dir /= L"Ballistic Games/Lumen";
    if (!p_subpath.empty()) dir /= p_subpath;

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}
    
std::filesystem::path Paths::local_data(std::wstring_view p_subpath) { return _known_folder(FOLDERID_LocalAppData, p_subpath); }
std::filesystem::path Paths::local_low_data(std::wstring_view p_subpath) { return _known_folder(FOLDERID_LocalAppDataLow, p_subpath); }
std::filesystem::path Paths::roaming_data(std::wstring_view p_subpath) { return _known_folder(FOLDERID_RoamingAppData, p_subpath); }

std::filesystem::path Paths::shader_cache() { return local_data(L"shader_cache"); }
std::filesystem::path Paths::pipeline_cache() { return local_data(L"pipeline_cache"); }
std::filesystem::path Paths::screenshots() { return roaming_data(L"screenshots"); }
std::filesystem::path Paths::logs() { return roaming_data(L"logs"); }

std::filesystem::path Paths::executable_dir()
{
    wchar_t buf[MAX_PATH]{};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) return {};
    return std::filesystem::path(buf).parent_path();
}

Error Paths::set_hidden(const std::filesystem::path& p_path, bool p_hidden)
{
    using enum Error;
    DWORD attrs = GetFileAttributesW(p_path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return FAILED;
    DWORD next = p_hidden ? (attrs | FILE_ATTRIBUTE_HIDDEN) : (attrs & ~FILE_ATTRIBUTE_HIDDEN);
    if (next == attrs) return OK;
    return SetFileAttributesW(p_path.c_str(), next) ? OK : FAILED;
}

static bool _select_in_open_explorer(const std::filesystem::path& p_dir, const std::filesystem::path& p_file)
{
    IShellWindows* windows = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&windows)))) return false;

    bool found = false;
    long count = 0;
    windows->get_Count(&count);

    for (long i = 0; i < count && !found; ++i) {
        VARIANT index{};
        index.vt = VT_I4;
        index.lVal = i;

        IDispatch* disp = nullptr;
        if (windows->Item(index, &disp) != S_OK || !disp) continue;

        IServiceProvider* sp = nullptr;
        IShellBrowser* browser = nullptr;
        IShellView* view = nullptr;
        IFolderView* folder_view = nullptr;
        IShellItem* folder = nullptr;
        PWSTR folder_path = nullptr;

        if (SUCCEEDED(disp->QueryInterface(IID_PPV_ARGS(&sp))) &&
            SUCCEEDED(sp->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser))) &&
            SUCCEEDED(browser->QueryActiveShellView(&view)) &&
            SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&folder_view))) &&
            SUCCEEDED(folder_view->GetFolder(IID_PPV_ARGS(&folder))) &&
            SUCCEEDED(folder->GetDisplayName(SIGDN_FILESYSPATH, &folder_path))) {

            std::error_code ec;
            if (std::filesystem::equivalent(folder_path, p_dir, ec)) {
                found = true;
                if (!p_file.empty()) {
                    PIDLIST_ABSOLUTE pidl = nullptr;
                    if (SUCCEEDED(SHParseDisplayName(p_file.c_str(), nullptr, &pidl, 0, nullptr))) {
                        view->SelectItem(ILFindLastID(pidl), SVSI_SELECT | SVSI_DESELECTOTHERS | SVSI_ENSUREVISIBLE | SVSI_FOCUSED);
                        CoTaskMemFree(pidl);
                    }
                }
            }
        }

        if (folder_path) CoTaskMemFree(folder_path);
        if (folder) folder->Release();
        if (folder_view) folder_view->Release();
        if (view) view->Release();
        if (browser) browser->Release();
        if (sp) sp->Release();
        disp->Release();
    }

    windows->Release();
    return found;
}

void Paths::reveal_in_explorer(const std::filesystem::path& p_path)
{
    std::filesystem::path native = p_path;
    native.make_preferred();

    const bool is_dir = std::filesystem::is_directory(p_path);
    const std::filesystem::path dir = is_dir ? native : native.parent_path();

    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool needs_uninit = SUCCEEDED(init);
    const bool com_ok = SUCCEEDED(init) || init == RPC_E_CHANGED_MODE;
    const bool reused = com_ok && _select_in_open_explorer(dir, is_dir ? std::filesystem::path() : native);
    if (needs_uninit) CoUninitialize();
    if (reused) return;

    if (is_dir) {
        ShellExecuteW(nullptr, L"open", native.c_str(), nullptr, nullptr, SW_SHOWNOACTIVATE);
    } else {
        std::wstring arg = L"/select,\"" + native.wstring() + L"\"";
        ShellExecuteW(nullptr, nullptr, L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNOACTIVATE);
    }
}

void Paths::move(const std::filesystem::path& p_src, const std::filesystem::path& p_dst)
{
    if (p_src.empty() || p_dst.empty()) return;
    if (p_src.parent_path() == p_dst) return;

    auto s = p_src.begin();
    auto d = p_dst.begin();
    bool src_is_prefix = true;
    for (; s != p_src.end(); ++s, ++d) {
        if (d == p_dst.end() || *d != *s) { src_is_prefix = false; break; }
    }
    if (src_is_prefix) return;

    const std::filesystem::path dst = p_dst / p_src.filename();
    if (std::filesystem::exists(dst)) return;

    std::error_code ec;
    std::filesystem::rename(p_src, dst, ec);
}

void Paths::rename(const std::filesystem::path& p_path, std::string_view p_new_stem)
{
    std::filesystem::path dst = p_path;
    dst.replace_filename(std::string(p_new_stem) + p_path.extension().string());
    if (dst == p_path) return;

    std::error_code ec;
    if (std::filesystem::exists(dst, ec)) return;
    std::filesystem::rename(p_path, dst, ec);
}

void Paths::remove_to_recycle(const std::filesystem::path& p_path)
{
    std::error_code ec;
    if (!std::filesystem::exists(p_path, ec)) return;
    std::filesystem::path native = p_path;
    native.make_preferred();
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool needs_uninit = SUCCEEDED(init);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) return;
    IFileOperation* op = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&op));
    if (SUCCEEDED(hr)) {
        op->SetOperationFlags(FOF_NO_UI | FOF_ALLOWUNDO | FOFX_RECYCLEONDELETE);
        IShellItem* item = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(native.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
            op->DeleteItem(item, nullptr);
            op->PerformOperations();
            item->Release();
        }
        op->Release();
    }
    if (needs_uninit) CoUninitialize();
}

bool Paths::is_under(const std::filesystem::path& p_path, const std::filesystem::path& p_base)
{
    auto pb = p_path.begin();
    auto bb = p_base.begin();
    for (; bb != p_base.end(); ++bb, ++pb) if (pb == p_path.end() || *pb != *bb) return false;
    return true;
}

bool Paths::has_subdir(const std::filesystem::path& p_path)
{
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(p_path, ec)) if (e.is_directory(ec)) return true;
    return false;
}

void Paths::gather_subdirs(const std::filesystem::path& p_path, std::vector<std::filesystem::path>& p_out)
{
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(p_path, ec)) if (e.is_directory(ec)) p_out.push_back(e.path());
    std::sort(p_out.begin(), p_out.end());
}

};
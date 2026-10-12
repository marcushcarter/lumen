#include <core/application/launch_args.h>
#include <windows.h>
#include <shellapi.h>
#include <cwchar>

namespace lumen {

LaunchArgs LaunchArgs::parse_command_line()
{
    LaunchArgs args;

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return args;

    for (int i = 1; i < argc; i++) {
        const wchar_t* a = argv[i];
        if (std::wcscmp(a, L"--game") == 0) args.game = true;
        else if (std::wcscmp(a, L"--pie") == 0) args.pie = true;
        else if (std::wcscmp(a, L"--project") == 0 && i + 1 < argc) args.project = argv[++i];
    }

    LocalFree(argv);
    return args;
}

}

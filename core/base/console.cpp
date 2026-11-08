#include <core/base/console.h>
#include <core/base/error.h>
#include <map>

namespace lumen {

struct ConsoleCommand
{
    std::string help;
    ConsoleFn fn;
};

static std::map<std::string, ConsoleCommand, std::less<>>& _commands()
{
    static std::map<std::string, ConsoleCommand, std::less<>> commands;
    return commands;
}

void console_register(const char* p_name, const char* p_help, ConsoleFn p_fn)
{
    _commands()[p_name] = { p_help ? p_help : "", std::move(p_fn) };
}

void console_unregister(const char* p_name)
{
    auto& commands = _commands();
    auto it = commands.find(std::string_view(p_name));
    if (it != commands.end()) commands.erase(it);
}

bool console_execute(std::string_view p_line)
{
    ConsoleArgs args;
    size_t i = 0;
    while (i < p_line.size()) {
        while (i < p_line.size() && (p_line[i] == ' ' || p_line[i] == '\t')) i++;
        const size_t start = i;
        while (i < p_line.size() && p_line[i] != ' ' && p_line[i] != '\t') i++;
        if (i > start) args.push_back(p_line.substr(start, i - start));
    }
    if (args.empty()) return false;

    auto& commands = _commands();
    if (args[0] == "help") {
        for (const auto& [name, cmd] : commands) log_write("  %s  %s", name.c_str(), cmd.help.c_str());
        return true;
    }
    auto it = commands.find(args[0]);
    if (it == commands.end()) {
        log_write("Unknown command '%.*s'. Type 'help' for a list.", (int)args[0].size(), args[0].data());
        return false;
    }
    ConsoleArgs rest(args.begin() + 1, args.end());
    it->second.fn(rest);
    return true;
}

}

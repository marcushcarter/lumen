#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace lumen {

using ConsoleArgs = std::vector<std::string_view>;
using ConsoleFn = std::function<void(const ConsoleArgs& p_args)>;

void console_register(const char* p_name, const char* p_help, ConsoleFn p_fn);
void console_unregister(const char* p_name);
bool console_execute(std::string_view p_line);

}

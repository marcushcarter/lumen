#pragma once
#include <sal.h>
#include <cstdint>
#include <string>
#include <string_view>

namespace lumen {

void str_lower(std::string& r_text);
std::string str_lowered(std::string_view p_text);
void str_appendf(std::string& r_out, _Printf_format_string_ const char* p_fmt, ...);
int str_natural_compare(const char* p_a, const char* p_b);
const char* fmt_bytes(uint64_t p_bytes);

}

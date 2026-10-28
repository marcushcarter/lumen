#include <core/base/strings.h>
#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>

namespace lumen {

void str_lower(std::string& r_text)
{
    for (char& c : r_text) c = (char)std::tolower((unsigned char)c);
}

std::string str_lowered(std::string_view p_text)
{
    std::string out(p_text);
    str_lower(out);
    return out;
}

void str_appendf(std::string& r_out, const char* p_fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, p_fmt);
    const int n = std::vsnprintf(buf, sizeof(buf), p_fmt, args);
    va_end(args);
    if (n > 0) r_out.append(buf, (size_t)std::min(n, (int)sizeof(buf) - 1));
}

int str_natural_compare(const char* p_a, const char* p_b)
{
    auto is_digit = [](char c) { return c >= '0' && c <= '9'; };
    const char* a = p_a;
    const char* b = p_b;
    while (*a && *b) {
        if (is_digit(*a) && is_digit(*b)) {
            while (*a == '0') a++;
            while (*b == '0') b++;
            const char* ea = a;
            const char* eb = b;
            while (is_digit(*ea)) ea++;
            while (is_digit(*eb)) eb++;
            if (ea - a != eb - b) return (ea - a) < (eb - b) ? -1 : 1;
            for (; a < ea; a++, b++) {
                if (*a != *b) return *a < *b ? -1 : 1;
            }
            continue;
        }
        const int d = std::toupper((unsigned char)*a) - std::toupper((unsigned char)*b);
        if (d) return d;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

const char* fmt_bytes(uint64_t p_bytes)
{
    thread_local char pool[8][24];
    thread_local uint32_t next = 0;
    char* buf = pool[next++ & 7];
    if (p_bytes >= (1ull << 30)) std::snprintf(buf, 24, "%.2f GiB", double(p_bytes) / double(1ull << 30));
    else if (p_bytes >= (1ull << 20)) std::snprintf(buf, 24, "%.1f MiB", double(p_bytes) / double(1ull << 20));
    else if (p_bytes >= (1ull << 10)) std::snprintf(buf, 24, "%.0f KiB", double(p_bytes) / double(1ull << 10));
    else std::snprintf(buf, 24, "%llu B", (unsigned long long)p_bytes);
    return buf;
}

}

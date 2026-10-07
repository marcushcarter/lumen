#include <core/base/error.h>
#include <iterator>
#include <cstdio>
#include <cstdarg>

namespace lumen {

const char *error_names[] = {
	"OK",
	"Failed",
	"Canceled",
};

static_assert(std::size(error_names) == static_cast<size_t>(Error::MAX));

LogSink& log_sink() {
    static LogSink instance;
    return instance;
}

void LogSink::write(const char* s)
{
    std::lock_guard lock(mutex);
    fputs(s, stderr);
    fputc('\n', stderr);
    lines.emplace_back(s);
    if (lines.size() > max_lines)
        lines.erase(lines.begin(), lines.begin() + (lines.size() - max_lines));
}

void LogSink::clear() {
    std::lock_guard lock(mutex);
    lines.clear();
}

std::string LogSink::to_string()
{
    std::lock_guard lock(mutex);
    std::string out;
    for (const std::string& line : lines) {
        out += line;
        out += '\n';
    }
    return out;
}

void log_write(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    log_sink().write(buf);
}

}
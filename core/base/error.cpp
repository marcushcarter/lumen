#include <core/base/error.h>
#include <cstdarg>
#include <cstdio>
#include <iterator>

namespace lumen {

const char* error_names[] = {
    "OK",
    "Failed",
    "Canceled",
};

static_assert(std::size(error_names) == static_cast<size_t>(Error::MAX));

LogSink& log_sink()
{
    static LogSink instance;
    return instance;
}

void LogSink::write(const char* p_line)
{
    std::lock_guard lock(mutex);
    fputs(p_line, stderr);
    fputc('\n', stderr);
    lines.emplace_back(p_line);
    while (lines.size() > MAX_LINES) lines.pop_front();
}

void LogSink::clear()
{
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

void log_write(const char* p_fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, p_fmt);
    vsnprintf(buf, sizeof(buf), p_fmt, args);
    va_end(args);
    log_sink().write(buf);
}

}

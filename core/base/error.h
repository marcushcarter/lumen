#pragma once
#pragma once
#include <vector>
#include <string>
#include <mutex>
#include <cstdio>

namespace lumen {

enum class Error 
{
    OK,
    FAILED,
    CANCELED,
    MAX
};

extern const char *error_names[];

struct LogSink
{
    std::vector<std::string> lines;
    size_t max_lines = 4096;
    std::mutex mutex;

    void write(const char* s);
    void clear();
    std::string to_string();
};

LogSink& log_sink();
void log_write(const char* fmt, ...); 

}

#define LUMEN_ERR_FAIL_COND_V(m_cond, m_retval) \
    if (m_cond) { \
        lumen::log_write("[Lumen] Condition \"%s\" failed at %s:%d\n", #m_cond, __FILE__, __LINE__); \
        return m_retval; \
    } else \
        ((void)0)

#define LUMEN_ERR_FAIL_COND_V_MSG(m_cond, m_retval, m_msg) \
    if (m_cond) { \
        lumen::log_write("[Lumen] Condition \"%s\" failed at %s:%d\n%s\n", #m_cond, __FILE__, __LINE__, m_msg); \
        return m_retval; \
    } else \
        ((void)0)

#define LUMEN_ERR_FAIL_COND(m_cond) \
    if (m_cond) { \
        lumen::log_write("[Lumen] Condition \"%s\" failed at %s:%d\n", #m_cond, __FILE__, __LINE__); \
        return; \
    } else \
        ((void)0)

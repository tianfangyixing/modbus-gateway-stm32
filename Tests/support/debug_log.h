#ifndef HOST_TEST_DEBUG_LOG_H
#define HOST_TEST_DEBUG_LOG_H

#include <stdarg.h>
#include <stdio.h>

/* Keep log argument evaluation without linking the target RTT/SystemView transport. */
static inline int debug_log_printf(const char *format, ...)
{
    va_list arguments;
    int result;

    va_start(arguments, format);
    result = vsnprintf(NULL, 0U, format, arguments);
    va_end(arguments);
    return result;
}

#endif

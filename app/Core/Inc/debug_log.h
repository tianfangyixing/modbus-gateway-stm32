#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include "SEGGER_RTT.h"

#define debug_log_printf(...) SEGGER_RTT_printf(0, __VA_ARGS__)

#ifdef __cplusplus
extern "C" {
#endif

void debug_log_init(void);

static inline unsigned debug_log_write(const void *pBuffer, unsigned numBytes)
{
    return SEGGER_RTT_Write(0, pBuffer, numBytes);
}

#ifdef __cplusplus
}
#endif

#endif /* DEBUG_LOG_H */

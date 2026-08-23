#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

void debug_log_init(void);

/* Task-context only. A positive result means queued, not transmitted. */
int debug_log(const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif /* DEBUG_LOG_H */

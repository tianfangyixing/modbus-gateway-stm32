#ifndef MANAGEMENT_TEST_DEBUG_LOG_H
#define MANAGEMENT_TEST_DEBUG_LOG_H

int management_transport_test_log(const char *format, ...);

#define debug_log_printf(...) management_transport_test_log(__VA_ARGS__)

#endif

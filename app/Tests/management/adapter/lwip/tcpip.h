#ifndef MANAGEMENT_TEST_TCPIP_H
#define MANAGEMENT_TEST_TCPIP_H
void management_test_lock_tcpip(void);
void management_test_unlock_tcpip(void);
#define LOCK_TCPIP_CORE() management_test_lock_tcpip()
#define UNLOCK_TCPIP_CORE() management_test_unlock_tcpip()
#endif

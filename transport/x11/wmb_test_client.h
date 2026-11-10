#ifndef WMB_TEST_CLIENT_H
#define WMB_TEST_CLIENT_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    bool done;
    int result;
    bool visible, awaiting_key, keyboard_received, mouse_received, release;
    uint32_t window;
    uint32_t hold_ms;
    const char *capture_path;
} WmbTestResult;
void wmb_raw_test_client(void *argument);
void wmb_xlib_test_client(void *argument);
void wmb_flow_test_client(void *argument);
void wmb_timeout_test_client(void *argument);
void wmb_wire_test_client(void *argument);
#endif

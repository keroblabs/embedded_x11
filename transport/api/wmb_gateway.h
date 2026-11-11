#ifndef WMB_GATEWAY_H
#define WMB_GATEWAY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
struct wmb_api;
typedef struct {
    void *context;
    bool (*is_runtime_thread)(void *context);
    /* Must not cancel/exit the caller: submitted arguments remain borrowed. */
    void (*wait_ms)(void *context, uint32_t milliseconds);
    uint32_t (*tick_ms)(void *context);
} wmb_gateway_port_t;
typedef struct {
    uint32_t posted, completed, rejected;
    size_t clients;
} wmb_gateway_stats_t;

/* Configure once from the runtime thread after wmb_main succeeds. */
int wmb_gateway_start(const wmb_gateway_port_t *port);
const struct wmb_api *wmb_gateway_table(void);
const struct wmb_api *wmb_api_native_table(void);
bool wmb_gateway_is_owner(void);
bool wmb_gateway_stopping(void);
void wmb_gateway_poll(void);
void wmb_gateway_stop(void);
bool wmb_gateway_try_finish(void);
int wmb_gateway_result(void);
size_t wmb_gateway_count(void);
void wmb_gateway_stats(wmb_gateway_stats_t *result);
#ifdef __cplusplus
}
#endif
#endif

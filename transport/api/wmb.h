#ifndef WMB_COMPONENT_H
#define WMB_COMPONENT_H
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Optional compiled XML; keep it valid through component shutdown. */
    const char *jwm_configuration;
} wmb_config_t;

/* The caller initializes the runtime and continues yielding after startup. */
int wmb_main(const wmb_config_t *configuration);
/* 0 while active, 1 on clean completion, negative on failure. */
int wmb_poll(void);
int wmb_shutdown(void);
bool wmb_ready(void);
/* Applications close their displays and return after the WM has unmanaged them. */
bool wmb_stop_requested(void);
#ifdef __cplusplus
}
#endif
#endif

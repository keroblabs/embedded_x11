#ifndef WMB_API_INTERNAL_H
#define WMB_API_INTERNAL_H
#include "api_calls.h"
#include <stdio.h>

Display *wmb_api_open(const char *name);
int wmb_api_next_event(Display *display, XEvent *event);
XID wmb_api_alloc_id(Display *display);
int wmb_api_yield(uint32_t milliseconds);
uint32_t wmb_api_revision(void);
_Noreturn void wmb_api_fatal(const char *reason);
void wmb_api_require_owner(void);
#endif

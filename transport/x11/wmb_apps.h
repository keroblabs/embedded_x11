#ifndef WMB_APPLICATIONS_H
#define WMB_APPLICATIONS_H
#include <stddef.h>
void wmb_apps_reap(void);
void wmb_apps_stop(void);
size_t wmb_apps_count(void);
int wmb_apps_result(void);
#endif

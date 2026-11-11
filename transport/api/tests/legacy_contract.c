#include "wmb_api.h"
#include "wmb_test_wrappers.h"
#if WMB_API_VERSION != 1
#error "This application must be compiled against the frozen v1 SDK"
#endif
int wmb_legacy_contract(const void *table)
{
    return wmb_app_wmb_bind(table) == 0 && XDefaultScreen(NULL) == 42 ? 0 : -1;
}

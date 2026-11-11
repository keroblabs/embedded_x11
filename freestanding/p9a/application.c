#include "wmb_api.h"
#include <stdint.h>

#ifndef WMB_API_ADDRESS
#error "The application binding must use the same configured load base as the provider"
#endif
__attribute__((section(".wmb_binding"), used))
const uintptr_t wmb_application_api_address = WMB_API_ADDRESS;
int wmb_hello_main(unsigned iterations);
int main(void)
{
    if (wmb_bind((const wmb_api_t *)(uintptr_t)WMB_API_ADDRESS) != 0) return 1;
    return wmb_hello_main(1);
}

#ifndef WMB_HOST_RPC_SERVER_H
#define WMB_HOST_RPC_SERVER_H
#include <stdbool.h>
#include <stdint.h>
int wmb_rpc_server_start(const char *endpoint, uint32_t abi_version);
bool wmb_rpc_server_stop_requested(void);
bool wmb_rpc_server_take_capture(void);
void wmb_rpc_server_begin_shutdown(void);
int wmb_rpc_server_join(void);
#endif

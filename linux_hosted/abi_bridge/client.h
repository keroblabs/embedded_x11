#ifndef WMB_HOST_BRIDGE_CLIENT_H
#define WMB_HOST_BRIDGE_CLIENT_H
/* Host-only binding scaffold. Applications themselves use the ordinary SDK. */
int wmb_bridge_bind(const char *endpoint);
int wmb_bridge_disconnect(void);
#endif

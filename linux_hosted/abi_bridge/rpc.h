#ifndef WMB_HOST_RPC_H
#define WMB_HOST_RPC_H
#include "wmb_api.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RPC_ARGUMENTS 16u
#define RPC_RESULT_BLOB 16u
#define RPC_BLOBS 17u
#define RPC_LIMIT (1024u * 1024u)
#define RPC_MAGIC UINT32_C(0x57425231)
#define RPC_HELLO UINT32_C(0xfffffff0)
#define RPC_STOP UINT32_C(0xfffffff1)
#define RPC_CAPTURE UINT32_C(0xfffffff2)
typedef struct {
    uint32_t operation, sequence;
    int32_t status;
    uint64_t result, values[RPC_ARGUMENTS];
    unsigned char *data[RPC_BLOBS];
    size_t sizes[RPC_BLOBS];
    void *storage;
    bool owned[RPC_BLOBS];
} RpcFrame;

int rpc_error(const char *reason);
bool rpc_signed(uint64_t value, size_t bytes);
bool rpc_unsigned(uint64_t value, size_t bytes);
void rpc_put32(unsigned char *bytes, uint32_t value);
uint32_t rpc_get32(const unsigned char *bytes);
void rpc_put64(unsigned char *bytes, uint64_t value);
uint64_t rpc_get64(const unsigned char *bytes);
int rpc_read(int descriptor, RpcFrame *frame);
int rpc_write(int descriptor, const RpcFrame *frame);
void rpc_clear(RpcFrame *frame);
int rpc_blob(RpcFrame *frame, unsigned index, const void *data, size_t size);
int rpc_words(RpcFrame *frame, unsigned index, const uint64_t *words, size_t count);
int rpc_read_words(const RpcFrame *frame, unsigned index, uint64_t *words, size_t count);
uint32_t rpc_byte_order(void);

int rpc_pack_gc(RpcFrame *, unsigned, const XGCValues *, uint64_t);
int rpc_unpack_gc(const RpcFrame *, unsigned, XGCValues *, uint64_t);
int rpc_pack_changes(RpcFrame *, unsigned, const XWindowChanges *, uint64_t);
int rpc_unpack_changes(const RpcFrame *, unsigned, XWindowChanges *, uint64_t);
int rpc_pack_attributes(RpcFrame *, unsigned, const XSetWindowAttributes *, uint64_t);
int rpc_unpack_attributes(const RpcFrame *, unsigned, XSetWindowAttributes *, uint64_t);
int rpc_pack_hints(RpcFrame *, unsigned, const XSizeHints *);
int rpc_unpack_hints(const RpcFrame *, unsigned, XSizeHints *);
int rpc_pack_color(RpcFrame *, unsigned, const XColor *, bool);
int rpc_unpack_color(const RpcFrame *, unsigned, XColor *, bool);
int rpc_pack_visual(RpcFrame *, unsigned, const Visual *);
int rpc_unpack_visual(const RpcFrame *, unsigned, Visual *);
int rpc_pack_window(RpcFrame *, unsigned, const XWindowAttributes *, uint64_t);
int rpc_unpack_window(const RpcFrame *, unsigned, XWindowAttributes *, Visual *, uint64_t *);
int rpc_pack_points(RpcFrame *, unsigned, const XPoint *, size_t);
XPoint *rpc_unpack_points(const RpcFrame *, unsigned, size_t);
int rpc_pack_property(RpcFrame *, unsigned, const unsigned char *, size_t, int);
unsigned char *rpc_unpack_property(const RpcFrame *, unsigned, size_t, int, bool);
int rpc_pack_xids(RpcFrame *, unsigned, const unsigned long *, size_t);
unsigned long *rpc_unpack_xids(const RpcFrame *, unsigned, size_t);
int rpc_pack_event(RpcFrame *, unsigned, const XEvent *, bool);
int rpc_unpack_event(const RpcFrame *, unsigned, XEvent *, Display *);
long rpc_event_mask(const XEvent *);
#endif

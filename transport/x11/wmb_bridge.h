#ifndef WMB_X11_BRIDGE_H
#define WMB_X11_BRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "fiber.h"
#include "lists.h"

#define WMB_CONNECTION_LIMIT 32
#define WMB_WIRE_LIMIT (1024u * 1024u)
#define WMB_REPLY_TIMEOUT_MS 5000u
#define WMB_EVENT_LIMIT 128u

typedef struct {
    unsigned char *data;
    size_t size, capacity;
} WmbBytes;

typedef struct WmbConnection WmbConnection;

typedef struct {
    LIST_HEADER;
    WmbConnection *owner;
    size_t size;
    uint64_t serial;
    unsigned char bytes[];
} WmbPacket;

/* Shared across the client/server ABI boundary: no Xlib or DIX types here. */
struct WmbConnection {
    unsigned int slot, index;
    void *server_client;
    bool opening, closing, closed, failed, setup_ready;
    bool allowed, ignored, impervious, in_flight, active, replay;
    bool current_big;
    bool replay_next;
    uint32_t big_words;
    unsigned char *big_base;
    unsigned char *input, *current;
    size_t input_size, input_pos, current_size, pushed_pos;
    WmbBytes pushed, retired, setup, wire;
    list_data_t responses, events;
    unsigned long completed;
    uint64_t serial, current_serial, replay_serial;
    size_t queued_bytes;
    void (*trace)(void *argument, bool output, const void *bytes, size_t size);
    void *trace_data;
};

extern WmbConnection *wmb_connections[WMB_CONNECTION_LIMIT];

void wmb_bridge_server_ready(bool ready);
bool wmb_bridge_is_server(void);
bool_t wmb_bridge_has_work(void *unused);
WmbConnection *wmb_connection_open(void);
int wmb_connection_submit(WmbConnection *connection, unsigned char *bytes, size_t size);
int wmb_connection_close(WmbConnection *connection);
void wmb_connection_destroy(WmbConnection *connection);
void wmb_connection_fail(WmbConnection *connection, const char *reason);
bool wmb_bytes_append(WmbBytes *buffer, const void *bytes, size_t size);
void wmb_bytes_clear(WmbBytes *buffer);
int wmb_connection_output(WmbConnection *connection, const void *bytes, size_t size);
WmbPacket *wmb_packet_take(list_data_t *list);
void wmb_packet_free(WmbPacket *packet);
void wmb_connection_boundary(WmbConnection *connection);
void wmb_connection_init_queues(WmbConnection *connection);

#endif

#include "wmb_bridge.h"
#include "hal_alloc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

WmbConnection *wmb_connections[WMB_CONNECTION_LIMIT];
static fiber_t *server_fiber;
static bool server_ready;
static void packet_deleted(list_node_t *node)
{
    WmbPacket *packet = (WmbPacket *)node;
    if (packet->owner->queued_bytes < packet->size) {
        fputs("WMB transport: invalid packet ownership accounting\n", stderr);
        abort();
    }
    packet->owner->queued_bytes -= packet->size;
}

void wmb_connection_init_queues(WmbConnection *connection)
{
    list_init(&connection->responses, packet_deleted);
    list_init(&connection->events, packet_deleted);
}

void wmb_bridge_server_ready(bool ready)
{
    unsigned int i;
    server_ready = ready;
    server_fiber = ready ? fiber_self() : NULL;
    if (!ready)
        for (i = 0; i < WMB_CONNECTION_LIMIT; i++)
            if (wmb_connections[i]) {
                wmb_connections[i]->closed = true;
                wmb_connections[i]->server_client = NULL;
            }
    fiber_notify();
}

bool wmb_bridge_is_server(void) { return server_fiber == fiber_self() && server_ready; }

bool_t wmb_bridge_has_work(void *unused)
{
    unsigned int i;
    (void)unused;
    for (i = 0; i < WMB_CONNECTION_LIMIT; i++) {
        WmbConnection *c = wmb_connections[i];
        if (c && !c->closed && (c->opening || c->closing ||
            (c->allowed && !c->ignored &&
             (c->pushed_pos < c->pushed.size || c->input_pos < c->input_size))))
            return TRUE;
    }
    return FALSE;
}

bool wmb_bytes_append(WmbBytes *buffer, const void *bytes, size_t size)
{
    size_t required, capacity;
    unsigned char *data;
    if (size > WMB_WIRE_LIMIT || buffer->size > WMB_WIRE_LIMIT - size) return false;
    required = buffer->size + size;
    if (required > buffer->capacity) {
        capacity = buffer->capacity ? buffer->capacity : 256;
        while (capacity < required) capacity *= 2;
        data = wmb_alloc(capacity);
        if (!data) return false;
        if (buffer->size) memcpy(data, buffer->data, buffer->size);
        wmb_free(buffer->data);
        buffer->data = data;
        buffer->capacity = capacity;
    }
    if (size) memcpy(buffer->data + buffer->size, bytes, size);
    buffer->size = required;
    return true;
}

void wmb_bytes_clear(WmbBytes *buffer)
{
    wmb_free(buffer->data);
    *buffer = (WmbBytes){0};
}

void wmb_connection_fail(WmbConnection *c, const char *reason)
{
    fprintf(stderr, "WMB transport: client %u: %s\n", c->index, reason);
    c->failed = true;
}

static bool_t opened(void *argument)
{
    WmbConnection *c = argument;
    return c->failed || c->closed || (c->setup_ready && !c->in_flight);
}

static bool_t completed(void *argument)
{
    WmbConnection *c = argument;
    return !c->in_flight || c->failed || c->closed;
}

static bool_t closed(void *argument) { return ((WmbConnection *)argument)->closed; }

WmbConnection *wmb_connection_open(void)
{
    unsigned int i;
    WmbConnection *c;
    if (wmb_bridge_is_server()) {
        fputs("WMB transport: Xlib cannot run reentrantly on the server fiber\n", stderr);
        return NULL;
    }
    for (i = 0; i < WMB_CONNECTION_LIMIT && wmb_connections[i]; i++) {}
    if (i == WMB_CONNECTION_LIMIT) {
        fputs("WMB transport: connection limit reached\n", stderr);
        return NULL;
    }
    c = wmb_alloc(sizeof(*c));
    if (!c) {
        fputs("WMB transport: connection allocation failed\n", stderr);
        return NULL;
    }
    memset(c, 0, sizeof(*c));
    c->slot = i;
    c->opening = true;
    c->allowed = true;
    wmb_connection_init_queues(c);
    wmb_connections[i] = c;
    fiber_notify();
    if (!fiber_wait_for(opened, c, WMB_REPLY_TIMEOUT_MS) || c->failed || c->closed) {
        wmb_connection_fail(c, "connection setup failed or timed out");
        if (wmb_connection_close(c) == 0) wmb_connection_destroy(c);
        return NULL;
    }
    return c;
}

int wmb_connection_submit(WmbConnection *c, unsigned char *bytes, size_t size)
{
    if (wmb_bridge_is_server() || c->failed || c->closed || c->in_flight ||
        !size || size > WMB_WIRE_LIMIT) {
        wmb_connection_fail(c, "invalid request lease/state");
        return -1;
    }
    c->input = bytes;
    c->input_size = size;
    c->input_pos = 0;
    c->in_flight = true;
    fiber_notify();
    if (!fiber_wait_for(completed, c, WMB_REPLY_TIMEOUT_MS)) {
        wmb_connection_fail(c, "request completion timeout; buffer remains leased");
        return -1;
    }
    return c->failed || c->closed ? -1 : 0;
}

int wmb_connection_close(WmbConnection *c)
{
    if (c->closed) return 0;
    if (!c->server_client && c->opening) {
        c->opening = false;
        c->closed = true;
        return 0;
    }
    c->closing = true;
    fiber_notify();
    if (!fiber_wait_for(closed, c, WMB_REPLY_TIMEOUT_MS)) {
        wmb_connection_fail(c, "close timeout");
        return -1;
    }
    return 0;
}

void wmb_connection_destroy(WmbConnection *c)
{
    if (!c) return;
    if (!c->closed) {
        wmb_connection_fail(c, "cannot free a live connection");
        return;
    }
    wmb_connections[c->slot] = NULL;
    list_clear(&c->responses);
    list_clear(&c->events);
    wmb_bytes_clear(&c->pushed);
    wmb_bytes_clear(&c->retired);
    wmb_bytes_clear(&c->setup);
    wmb_bytes_clear(&c->wire);
    wmb_free(c);
}

void wmb_connection_boundary(WmbConnection *c)
{
    if (!c->active || c->ignored) return;
    c->active = false;
    if (!c->replay) c->completed++;
    c->replay = false;
    if (c->current_big) {
        memcpy(c->big_base + 4, &c->big_words, 4);
        c->current_big = false;
        c->big_base = NULL;
    }
    wmb_bytes_clear(&c->retired);
    c->current = NULL;
    c->current_size = 0;
    if (c->pushed_pos == c->pushed.size) {
        c->pushed.size = 0;
        c->pushed_pos = 0;
    }
    if (c->pushed.size == c->pushed_pos && c->input_pos == c->input_size) {
        c->in_flight = false;
        c->input = NULL;
        c->input_size = c->input_pos = 0;
    }
}

WmbPacket *wmb_packet_take(list_data_t *list)
{
    WmbPacket *packet = list_get_head(list);
    if (packet) list_unlink(list, &packet->node);
    return packet;
}

void wmb_packet_free(WmbPacket *packet)
{
    if (packet) packet_deleted(&packet->node);
    wmb_free(packet);
}

static bool enqueue(WmbConnection *c, const unsigned char *bytes, size_t size)
{
    unsigned int type = bytes[0] & 127u;
    list_data_t *list = type >= 2 ? &c->events : &c->responses;
    WmbPacket *packet;
    if (list == &c->events && list_size(list) >= WMB_EVENT_LIMIT) {
        list_node_t *node = list->head;
        while (node && ((((WmbPacket *)node)->bytes[0] & 127u) != 6)) node = node->next;
        if (node) list_remove(list, node);
        else if (list_size(list) >= 4096) return false;
    }
    if (size > WMB_WIRE_LIMIT || c->queued_bytes > 2u * WMB_WIRE_LIMIT - size)
        return false;
    packet = wmb_alloc(sizeof(*packet) + size);
    if (!packet) return false;
    packet->size = size;
    packet->owner = c;
    packet->serial = c->current_serial;
    c->queued_bytes += size;
    memcpy(packet->bytes, bytes, size);
    list_insert_tail(list, &packet->node);
    return true;
}

int wmb_connection_output(WmbConnection *c, const void *bytes, size_t size)
{
    static const unsigned char padding[3];
    if (!wmb_bytes_append(&c->wire, bytes, size) ||
        !wmb_bytes_append(&c->wire, padding, (4 - (size & 3)) & 3))
        goto failed;
    while (c->wire.size >= (c->setup_ready ? 32u : 8u)) {
        size_t length;
        uint32_t words = 0;
        unsigned int type = c->wire.data[0] & 127u;
        if (!c->setup_ready) {
            uint16_t setup_words;
            memcpy(&setup_words, c->wire.data + 6, 2);
            length = 8u + (size_t)setup_words * 4;
        } else {
            if (type == 1 || type == 35) memcpy(&words, c->wire.data + 4, 4);
            if (words > (WMB_WIRE_LIMIT - 32u) / 4u) goto failed;
            length = 32u + (size_t)words * 4u;
        }
        if (c->wire.size < length) break;
        if (!c->setup_ready) {
            if (type != 1 || !wmb_bytes_append(&c->setup, c->wire.data, length)) goto failed;
            c->setup_ready = true;
        } else {
            if (c->trace) c->trace(c->trace_data, false, c->wire.data, length);
            if (!enqueue(c, c->wire.data, length)) goto failed;
        }
        c->wire.size -= length;
        memmove(c->wire.data, c->wire.data + length, c->wire.size);
    }
    return (int)size;
failed:
    wmb_connection_fail(c, "output allocation, framing, or queue limit failure");
    c->closing = true;
    return -1;
}

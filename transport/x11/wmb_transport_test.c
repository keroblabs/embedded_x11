#include "wmb_bridge.h"
#include "hal_alloc.h"
#include <stdio.h>
#include <string.h>

#define CHECK(value) do { \
    if (!(value)) { fprintf(stderr, "P5 transport FAIL %d: %s\n", __LINE__, #value); return 1; } \
} while (0)

static bool fail_allocation;
void *__real_wmb_alloc(size_t size);
void *__wrap_wmb_alloc(size_t size)
{
    if (fail_allocation) { fail_allocation = false; return NULL; }
    return __real_wmb_alloc(size);
}

int main(void)
{
    WmbConnection connection = {.setup_ready = true, .index = 42};
    WmbBytes bytes = {0};
    WmbPacket *packet;
    unsigned char header[32] = {1}, payload[12] = {6, 0, 1, 0x86, 2, 3, 4, 5, 6, 7, 8, 9};
    unsigned char larger[300] = {0};
    uint32_t words = 3, i, value;
    unsigned char *old;
    wmb_connection_init_queues(&connection);
    memcpy(header + 4, &words, 4);
    CHECK(wmb_connection_output(&connection, header, 32) == 32);
    CHECK(list_is_empty(&connection.responses));
    CHECK(wmb_connection_output(&connection, payload, 8) == 8);
    CHECK(list_is_empty(&connection.responses) && list_is_empty(&connection.events));
    CHECK(wmb_connection_output(&connection, payload + 8, 4) == 4);
    packet = wmb_packet_take(&connection.responses);
    CHECK(packet && packet->size == 44 && !memcmp(packet->bytes + 32, payload, 12));
    wmb_packet_free(packet);
    memset(header, 0, sizeof(header));
    header[0] = 2;
    CHECK(wmb_connection_output(&connection, header, 32) == 32);
    header[0] = 6;
    for (i = 0; i < 1000; i++) {
        memcpy(header + 12, &i, 4);
        CHECK(wmb_connection_output(&connection, header, 32) == 32);
    }
    CHECK(list_size(&connection.events) == WMB_EVENT_LIMIT);
    packet = wmb_packet_take(&connection.events);
    CHECK(packet && packet->bytes[0] == 2);
    wmb_packet_free(packet);
    for (i = 873; i < 1000; i++) {
        packet = wmb_packet_take(&connection.events);
        CHECK(packet && packet->bytes[0] == 6);
        memcpy(&value, packet->bytes + 12, 4);
        CHECK(value == i);
        wmb_packet_free(packet);
    }
    CHECK(connection.queued_bytes == 0 && list_is_empty(&connection.events));
    CHECK(wmb_bytes_append(&bytes, payload, 12));
    old = bytes.data;
    fail_allocation = true;
    CHECK(!wmb_bytes_append(&bytes, larger, sizeof(larger)));
    CHECK(bytes.data == old && bytes.size == 12 && !memcmp(bytes.data, payload, 12));
    wmb_bytes_clear(&bytes);
    words = WMB_WIRE_LIMIT / 4u;
    memset(header, 0, sizeof(header));
    header[0] = 1;
    memcpy(header + 4, &words, 4);
    CHECK(wmb_connection_output(&connection, header, 32) == -1);
    CHECK(connection.failed && connection.closing);
    list_clear(&connection.responses);
    list_clear(&connection.events);
    wmb_bytes_clear(&connection.wire);
    puts("PASS P5 framing, fragmented replies, critical events, motion overflow, OOM, wire limits");
    return 0;
}

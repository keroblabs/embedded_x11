#include "wmb_test_client.h"
#include "wmb_bridge.h"
#include <stdio.h>
#include <string.h>

void wmb_raw_test_client(void *argument)
{
    WmbTestResult *result = argument;
    WmbConnection *c = wmb_connection_open();
    union { uint32_t aligned; unsigned char bytes[4]; } request = {0};
    uint16_t words = 1;
    unsigned long before;
    WmbPacket *error = NULL;
    result->result = 1;
    if (!c) goto done;
    if (wmb_bridge_is_server() || !c->setup_ready || c->setup.size < 40) goto close;
    before = c->completed;
    request.bytes[0] = 127;
    memcpy(request.bytes + 2, &words, 2);
    if (wmb_connection_submit(c, request.bytes, sizeof(request)) != 0 ||
        c->completed != before + 1) goto close;
    request.bytes[0] = 255;
    if (wmb_connection_submit(c, request.bytes, sizeof(request)) != 0) goto close;
    error = wmb_packet_take(&c->responses);
    if (!error || error->size != 32 || error->bytes[0] != 0 || error->bytes[1] != 1)
        goto close;
    result->result = 0;
close:
    wmb_packet_free(error);
    if (wmb_connection_close(c) != 0) result->result = 1;
    wmb_connection_destroy(c);
done:
    printf("P5 raw: setup, NoOperation, BadRequest completion %s\n",
           result->result == 0 ? "PASS" : "FAIL");
    result->done = true;
}

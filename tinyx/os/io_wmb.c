#include "wmb_connection.h"

int ReadRequestFromClient(ClientPtr client)
{
    WmbConnection *c = wmb_server_connection(client);
    unsigned char *data;
    size_t available, length;
    uint16_t words;
    bool pushed;
    if (!c || !wmb_bridge_is_server()) {
        ErrorF("WMB: invalid request reader context\n");
        return -1;
    }
    wmb_connection_boundary(c);
    if (c->ignored || !c->allowed || c->closed || c->failed) return 0;
    pushed = c->pushed_pos < c->pushed.size;
    available = pushed ? c->pushed.size - c->pushed_pos : c->input_size - c->input_pos;
    if (available == 0) return 0;
    data = pushed ? c->pushed.data + c->pushed_pos : c->input + c->input_pos;
    if (available < 4) goto malformed;
    memcpy(&words, data + 2, 2);
    length = (size_t)words * 4;
    if (words == 0) {
        uint32_t big;
        if (!client->big_requests || available < 8) goto malformed;
        memcpy(&big, data + 4, 4);
        if (big < 2 || big > WMB_WIRE_LIMIT / 4u || (size_t)big * 4u > available)
            goto malformed;
        length = (size_t)big * 4u;
        c->big_base = data;
        c->big_words = big;
        c->current_big = true;
        memcpy(data + 4, data, 4);
        data += 4;
        client->req_len = big - 1;
    } else client->req_len = words;
    if (length < 4 || length > available) goto malformed;
    c->current = data;
    c->current_size = length - (c->current_big ? 4u : 0u);
    c->active = true;
    if (c->replay_next) {
        c->current_serial = c->replay_serial;
        c->replay_next = false;
    } else c->current_serial = c->setup_ready ? ++c->serial : 0;
    if (pushed) c->pushed_pos += length;
    else c->input_pos += length;
    client->requestBuffer = data;
    client->swapped = FALSE;
    return (int)length;
malformed:
    wmb_connection_fail(c, "malformed/truncated request length");
    return -1;
}

int WriteToClient(ClientPtr client, int count, const char *buffer)
{
    WmbConnection *c = wmb_server_connection(client);
    if (!c || count < 0 || !wmb_bridge_is_server()) {
        ErrorF("WMB: invalid client output context\n");
        return -1;
    }
    if (!count) return 0;
    return wmb_connection_output(c, buffer, (size_t)count);
}

int FlushClient(ClientPtr client, OsCommPtr os, const char *extra, int count)
{
    (void)os;
    return count ? WriteToClient(client, count, extra) : 0;
}

void ResetOsBuffers(void) {}
void FlushAllOutput(void) {}
void FlushIfCriticalOutputPending(void) {}
void SetCriticalOutputPending(void) { CriticalOutputPending = TRUE; }
void ResetCurrentRequest(ClientPtr client)
{
    WmbConnection *c = wmb_server_connection(client);
    WmbBytes big = {0};
    bool success = false;
    if (c && c->active) {
        if (c->current_big) {
            success = wmb_bytes_append(&big, c->current, 4) &&
                wmb_bytes_append(&big, &c->big_words, 4) &&
                wmb_bytes_append(&big, c->current + 4, c->current_size - 4) &&
                InsertFakeRequest(client, (char *)big.data, (int)big.size);
        } else success = InsertFakeRequest(client, (char *)c->current, (int)c->current_size);
    }
    wmb_bytes_clear(&big);
    if (!success) {
        client->noClientException = -1;
        ErrorF("WMB: request replay allocation/state failure\n");
        return;
    }
    c->replay = true;
    c->replay_next = true;
    c->replay_serial = c->current_serial;
}

int InsertFakeRequest(ClientPtr client, char *data, int count)
{
    WmbConnection *c = wmb_server_connection(client);
    WmbBytes replacement = {0};
    if (!c || count <= 0 ||
        !wmb_bytes_append(&replacement, data, (size_t)count) ||
        !wmb_bytes_append(&replacement,
                          c->pushed.size ? c->pushed.data + c->pushed_pos : NULL,
                          c->pushed.size - c->pushed_pos)) {
        wmb_bytes_clear(&replacement);
        ErrorF("WMB: pushed request allocation/state failure\n");
        return FALSE;
    }
    if (c->active && c->pushed.size &&
        (uintptr_t)c->current >= (uintptr_t)c->pushed.data &&
        (uintptr_t)c->current < (uintptr_t)c->pushed.data + c->pushed.size) {
        wmb_bytes_clear(&c->retired);
        c->retired = c->pushed;
    } else wmb_bytes_clear(&c->pushed);
    c->pushed = replacement;
    c->pushed_pos = 0;
    return TRUE;
}

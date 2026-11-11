#define _POSIX_C_SOURCE 200809L
#include "rpc.h"
#include <sys/socket.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIXED_BYTES (12u + RPC_ARGUMENTS * 8u + RPC_BLOBS * 4u)
_Static_assert(CHAR_BIT == 8 && sizeof(int) == 4 && sizeof(short) == 2,
               "The hosted wire codec requires 8/16/32-bit char/short/int");
int rpc_error(const char *reason)
{
    fprintf(stderr, "WMB host bridge: %s\n", reason);
    return -1;
}
bool rpc_signed(uint64_t value, size_t bytes)
{
    int64_t number = (int64_t)value;
    if (bytes == 8) return true;
    if (!bytes || bytes > 8) return false;
    return number >= -(INT64_C(1) << (bytes * 8 - 1)) &&
           number < (INT64_C(1) << (bytes * 8 - 1));
}
bool rpc_unsigned(uint64_t value, size_t bytes)
{
    return bytes == 8 || (bytes && bytes < 8 && value < (UINT64_C(1) << (bytes * 8)));
}
void rpc_put32(unsigned char *bytes, uint32_t value)
{
    for (unsigned index = 0; index < 4; index++) bytes[index] = (unsigned char)(value >> (index * 8));
}
uint32_t rpc_get32(const unsigned char *bytes)
{
    uint32_t value = 0;
    for (unsigned index = 0; index < 4; index++) value |= (uint32_t)bytes[index] << (index * 8);
    return value;
}
void rpc_put64(unsigned char *bytes, uint64_t value)
{
    for (unsigned index = 0; index < 8; index++) bytes[index] = (unsigned char)(value >> (index * 8));
}
uint64_t rpc_get64(const unsigned char *bytes)
{
    uint64_t value = 0;
    for (unsigned index = 0; index < 8; index++) value |= (uint64_t)bytes[index] << (index * 8);
    return value;
}
uint32_t rpc_byte_order(void)
{
    uint16_t value = 1;
    return *(const unsigned char *)(const void *)&value ? 1u : 2u;
}
static int receive_all(int descriptor, void *memory, size_t size, bool header)
{
    unsigned char *bytes = memory;
    size_t position = 0;
    while (position < size) {
        ssize_t result = recv(descriptor, bytes + position, size - position, 0);
        if (result > 0) position += (size_t)result;
        else if (!result && !position && header) return 1;
        else if (result < 0 && errno == EINTR) continue;
        else {
            if (result < 0) perror("WMB bridge receive");
            else fputs("WMB bridge: truncated frame\n", stderr);
            return -1;
        }
    }
    return 0;
}
static int send_all(int descriptor, const void *memory, size_t size)
{
    const unsigned char *bytes = memory;
    while (size) {
        ssize_t result = send(descriptor, bytes, size, MSG_NOSIGNAL);
        if (result > 0) { bytes += result; size -= (size_t)result; }
        else if (result < 0 && errno == EINTR) continue;
        else { perror("WMB bridge send"); return -1; }
    }
    return 0;
}
void rpc_clear(RpcFrame *frame)
{
    for (unsigned index = 0; index < RPC_BLOBS; index++)
        if (frame->owned[index]) free(frame->data[index]);
    free(frame->storage);
    *frame = (RpcFrame){0};
}
int rpc_blob(RpcFrame *frame, unsigned index, const void *data, size_t size)
{
    unsigned char *copy = NULL;
    if (index >= RPC_BLOBS || size > RPC_LIMIT || (!data && size))
        return rpc_error("invalid blob or length");
    if (size) {
        copy = malloc(size);
        if (!copy) return rpc_error("blob allocation failed");
        memcpy(copy, data, size);
    }
    if (frame->owned[index]) free(frame->data[index]);
    frame->data[index] = copy; frame->sizes[index] = size; frame->owned[index] = true;
    return 0;
}
int rpc_words(RpcFrame *frame, unsigned index, const uint64_t *words, size_t count)
{
    if (count > RPC_LIMIT / 8 || index >= RPC_BLOBS) return rpc_error("oversized scalar record");
    if (rpc_blob(frame, index, words, count * 8) != 0) return -1;
    for (size_t number = 0; number < count; number++)
        rpc_put64(frame->data[index] + number * 8, words[number]);
    return 0;
}
int rpc_read_words(const RpcFrame *frame, unsigned index, uint64_t *words, size_t count)
{
    if (index >= RPC_BLOBS || count > RPC_LIMIT / 8 || frame->sizes[index] != count * 8)
        return rpc_error("incorrect scalar record length");
    for (size_t number = 0; number < count; number++)
        words[number] = rpc_get64(frame->data[index] + number * 8);
    return 0;
}
int rpc_read(int descriptor, RpcFrame *frame)
{
    unsigned char header[16], *body;
    size_t size, position;
    int result = receive_all(descriptor, header, sizeof(header), true);
    if (result) return result;
    size = rpc_get32(header + 4);
    if (rpc_get32(header) != RPC_MAGIC || size < sizeof(header) + FIXED_BYTES || size > RPC_LIMIT)
        return rpc_error("invalid frame magic or bounded length");
    frame->operation = rpc_get32(header + 8); frame->sequence = rpc_get32(header + 12);
    size -= sizeof(header);
    body = malloc(size);
    if (!body) return rpc_error("frame allocation failed");
    frame->storage = body;
    if (receive_all(descriptor, body, size, false) != 0) return -1;
    frame->status = (int32_t)rpc_get32(body);
    frame->result = rpc_get64(body + 4);
    for (unsigned index = 0; index < RPC_ARGUMENTS; index++)
        frame->values[index] = rpc_get64(body + 12 + index * 8);
    position = FIXED_BYTES;
    for (unsigned index = 0; index < RPC_BLOBS; index++) {
        size_t length = rpc_get32(body + 12 + RPC_ARGUMENTS * 8 + index * 4);
        if (length > size - position) return rpc_error("blob exceeds its containing frame");
        frame->sizes[index] = length;
        frame->data[index] = body + position;
        position += length;
    }
    return position == size ? 0 : rpc_error("unaccounted trailing frame bytes");
}
int rpc_write(int descriptor, const RpcFrame *frame)
{
    unsigned char header[16], fixed[FIXED_BYTES] = {0};
    size_t size = sizeof(header) + sizeof(fixed);
    for (unsigned index = 0; index < RPC_BLOBS; index++) {
        if (frame->sizes[index] > RPC_LIMIT - size) return rpc_error("outgoing frame exceeds limit");
        if (frame->sizes[index] && !frame->data[index]) return rpc_error("missing outgoing blob");
        size += frame->sizes[index];
        rpc_put32(fixed + 12 + RPC_ARGUMENTS * 8 + index * 4, (uint32_t)frame->sizes[index]);
    }
    rpc_put32(header, RPC_MAGIC); rpc_put32(header + 4, (uint32_t)size);
    rpc_put32(header + 8, frame->operation); rpc_put32(header + 12, frame->sequence);
    rpc_put32(fixed, (uint32_t)frame->status); rpc_put64(fixed + 4, frame->result);
    for (unsigned index = 0; index < RPC_ARGUMENTS; index++)
        rpc_put64(fixed + 12 + index * 8, frame->values[index]);
    if (send_all(descriptor, header, sizeof(header)) || send_all(descriptor, fixed, sizeof(fixed)))
        return -1;
    for (unsigned index = 0; index < RPC_BLOBS; index++)
        if (send_all(descriptor, frame->data[index], frame->sizes[index])) return -1;
    return 0;
}

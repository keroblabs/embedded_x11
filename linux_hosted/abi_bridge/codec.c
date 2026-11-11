#include "rpc.h"
#include "fields.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define ENCODE(bit, member, kind) do { \
    if (mask & (uint64_t)(bit)) { words[count] = (uint64_t)value->member; } \
    count++; \
} while (0);
#define FIT_I(word, member) rpc_signed(word, sizeof(member))
#define FIT_U(word, member) rpc_unsigned(word, sizeof(member))
#define FIT_C(word, member) (CHAR_MIN < 0 ? rpc_signed(word, sizeof(member)) : rpc_unsigned(word, sizeof(member)))
#define ASSIGN_I(member, word) (member) = (int64_t)(word)
#define ASSIGN_U(member, word) (member) = (word)
#define ASSIGN_C(member, word) (member) = (char)(word)
#define DECODE(bit, member, kind) do { \
    if (mask & (uint64_t)(bit)) { \
        if (!FIT_##kind(words[count], value->member)) return rpc_error("scalar field overflow"); \
        ASSIGN_##kind(value->member, words[count]); \
    } else if (words[count]) return rpc_error("noncanonical masked field"); \
    count++; \
} while (0);
#define COUNT(bit, member, kind) count++;
#define MASKED_CODEC(name, type, fields) \
int rpc_pack_##name(RpcFrame *frame, unsigned index, const type *value, uint64_t mask) \
{ \
    uint64_t words[32] = {0}; size_t count = 0; \
    if (!value) return mask ? rpc_error("NULL masked structure") : 0; \
    fields(ENCODE) \
    return rpc_words(frame, index, words, count); \
} \
int rpc_unpack_##name(const RpcFrame *frame, unsigned index, type *value, uint64_t mask) \
{ \
    uint64_t words[32]; size_t count = 0, expected; \
    fields(COUNT) expected = count; count = 0; \
    if (rpc_read_words(frame, index, words, expected)) return -1; \
    memset(value, 0, sizeof(*value)); \
    fields(DECODE) \
    return 0; \
}
MASKED_CODEC(gc, XGCValues, RPC_GC_FIELDS)
MASKED_CODEC(changes, XWindowChanges, RPC_CHANGES_FIELDS)
MASKED_CODEC(attributes, XSetWindowAttributes, RPC_ATTRIBUTES_FIELDS)

int rpc_pack_hints(RpcFrame *frame, unsigned index, const XSizeHints *value)
{
    uint64_t words[32] = {0}, mask;
    size_t count = 0;
    if (!value) return rpc_error("NULL size hints");
    mask = (uint64_t)value->flags;
    RPC_HINTS_FIELDS(ENCODE)
    return rpc_words(frame, index, words, count);
}
int rpc_unpack_hints(const RpcFrame *frame, unsigned index, XSizeHints *value)
{
    uint64_t words[18], mask;
    size_t count = 0;
    if (rpc_read_words(frame, index, words, 18)) return -1;
    memset(value, 0, sizeof(*value)); mask = words[0];
    RPC_HINTS_FIELDS(DECODE)
    return 0;
}
int rpc_pack_visual(RpcFrame *frame, unsigned index, const Visual *value)
{
    uint64_t words[7] = {0}, mask = UINT64_MAX;
    size_t count = 0;
    if (!value) return 0;
    RPC_VISUAL_FIELDS(ENCODE)
    return rpc_words(frame, index, words, count);
}
int rpc_unpack_visual(const RpcFrame *frame, unsigned index, Visual *value)
{
    uint64_t words[7], mask = UINT64_MAX;
    size_t count = 0;
    if (rpc_read_words(frame, index, words, 7)) return -1;
    memset(value, 0, sizeof(*value));
    RPC_VISUAL_FIELDS(DECODE)
    return 0;
}
int rpc_pack_color(RpcFrame *frame, unsigned index, const XColor *value, bool output)
{
    uint64_t words[5];
    if (!value) return rpc_error("NULL color");
    words[0] = value->red; words[1] = value->green; words[2] = value->blue;
    words[3] = output ? value->pixel : 0;
    words[4] = output ? (unsigned char)value->flags : 0;
    return rpc_words(frame, index, words, output ? 5 : 3);
}
int rpc_unpack_color(const RpcFrame *frame, unsigned index, XColor *value, bool output)
{
    uint64_t words[5] = {0};
    if (rpc_read_words(frame, index, words, output ? 5 : 3)) return -1;
    if (words[0] > USHRT_MAX || words[1] > USHRT_MAX || words[2] > USHRT_MAX ||
        !rpc_unsigned(words[3], sizeof(value->pixel)) || words[4] > UCHAR_MAX)
        return rpc_error("invalid color field");
    *value = (XColor){.red = (unsigned short)words[0], .green = (unsigned short)words[1],
        .blue = (unsigned short)words[2], .pixel = (unsigned long)words[3], .flags = (char)words[4]};
    return 0;
}
int rpc_pack_window(RpcFrame *frame, unsigned index, const XWindowAttributes *value,
                    uint64_t screen)
{
    uint64_t words[30] = {0}, mask = UINT64_MAX;
    size_t count = 0;
    RPC_WINDOW_FIELDS(ENCODE)
    words[count++] = screen;
    words[count++] = value->visual != NULL;
    if (value->visual) {
        const Visual *visual = value->visual;
        words[count++] = visual->visualid; words[count++] = (uint64_t)visual->class;
        words[count++] = visual->red_mask; words[count++] = visual->green_mask;
        words[count++] = visual->blue_mask; words[count++] = (uint64_t)visual->bits_per_rgb;
        words[count++] = (uint64_t)visual->map_entries;
    }
    return rpc_words(frame, index, words, 30);
}
int rpc_unpack_window(const RpcFrame *frame, unsigned index, XWindowAttributes *value,
                      Visual *visual, uint64_t *screen)
{
    uint64_t words[30], mask = UINT64_MAX;
    size_t count = 0;
    RpcFrame nested = {0};
    if (rpc_read_words(frame, index, words, 30)) return -1;
    memset(value, 0, sizeof(*value));
    RPC_WINDOW_FIELDS(DECODE)
    *screen = words[count++];
    if (words[count++] > 1) return rpc_error("invalid visual presence");
    if (words[22]) {
        if (rpc_words(&nested, 0, words + 23, 7)) return -1;
        if (rpc_unpack_visual(&nested, 0, visual)) { rpc_clear(&nested); return -1; }
        rpc_clear(&nested);
        value->visual = visual;
    }
    return 0;
}
int rpc_pack_points(RpcFrame *frame, unsigned index, const XPoint *points, size_t count)
{
    unsigned char *bytes;
    if (count > RPC_LIMIT / 4 || (!points && count)) return rpc_error("invalid point count");
    bytes = calloc(count ? count : 1, 4);
    if (!bytes) return rpc_error("point allocation failed");
    for (size_t number = 0; number < count; number++) {
        uint32_t packed = (uint16_t)points[number].x | ((uint32_t)(uint16_t)points[number].y << 16);
        rpc_put32(bytes + number * 4, packed);
    }
    int result = rpc_blob(frame, index, bytes, count * 4);
    free(bytes);
    return result;
}
XPoint *rpc_unpack_points(const RpcFrame *frame, unsigned index, size_t count)
{
    XPoint *points;
    if (count > RPC_LIMIT / 4 || frame->sizes[index] != count * 4) {
        rpc_error("invalid point array length"); return NULL;
    }
    points = calloc(count ? count : 1, sizeof(*points));
    if (!points) { rpc_error("point allocation failed"); return NULL; }
    for (size_t number = 0; number < count; number++) {
        uint32_t packed = rpc_get32(frame->data[index] + number * 4);
        points[number].x = (short)(int16_t)packed;
        points[number].y = (short)(int16_t)(packed >> 16);
    }
    return points;
}
int rpc_pack_property(RpcFrame *frame, unsigned index, const unsigned char *data,
                      size_t count, int format)
{
    unsigned char *bytes;
    size_t stride;
    int result;
    if (format != 8 && format != 16 && format != 32) return rpc_error("invalid property format");
    stride = (size_t)format / 8;
    if (count > RPC_LIMIT / stride || (!data && count)) return rpc_error("invalid property count");
    bytes = malloc(count ? count * stride : 1);
    if (!bytes) return rpc_error("property encoding allocation failed");
    for (size_t number = 0; number < count; number++) {
        if (format == 8) bytes[number] = data[number];
        else if (format == 16) {
            uint16_t value;
            memcpy(&value, data + number * sizeof(value), sizeof(value));
            bytes[number * 2] = (unsigned char)value;
            bytes[number * 2 + 1] = (unsigned char)(value >> 8);
        } else {
            unsigned long value;
            memcpy(&value, data + number * sizeof(value), sizeof(value));
            rpc_put32(bytes + number * 4, (uint32_t)value);
        }
    }
    result = rpc_blob(frame, index, bytes, count * stride);
    free(bytes);
    return result;
}
unsigned char *rpc_unpack_property(const RpcFrame *frame, unsigned index, size_t count,
                                    int format, bool output)
{
    size_t stride, native_stride;
    unsigned char *data;
    if (format != 8 && format != 16 && format != 32) {
        rpc_error("invalid returned property format"); return NULL;
    }
    stride = (size_t)format / 8;
    native_stride = format == 32 ? sizeof(long) : stride;
    if (count > RPC_LIMIT / stride || frame->sizes[index] != count * stride ||
        count > (SIZE_MAX - 1) / native_stride) {
        rpc_error("invalid returned property count"); return NULL;
    }
    data = calloc(count * native_stride + 1, 1);
    if (!data) { rpc_error("property decoding allocation failed"); return NULL; }
    for (size_t number = 0; number < count; number++) {
        if (format == 8) data[number] = frame->data[index][number];
        else if (format == 16) {
            uint16_t value = frame->data[index][number * 2] |
                             ((uint16_t)frame->data[index][number * 2 + 1] << 8);
            memcpy(data + number * native_stride, &value, sizeof(value));
        } else {
            uint32_t packed = rpc_get32(frame->data[index] + number * 4);
            long value = output ? (long)(int32_t)packed : (long)(unsigned long)packed;
            memcpy(data + number * native_stride, &value, sizeof(value));
        }
    }
    return data;
}
int rpc_pack_xids(RpcFrame *frame, unsigned index, const unsigned long *values, size_t count)
{
    return rpc_pack_property(frame, index, (const unsigned char *)values, count, 32);
}
unsigned long *rpc_unpack_xids(const RpcFrame *frame, unsigned index, size_t count)
{
    return (unsigned long *)(void *)rpc_unpack_property(frame, index, count, 32, false);
}

#include "rpc.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
extern const long _Xevent_to_mask[];

int main(void)
{
    RpcFrame frame = {0}, again = {0};
    XGCValues gc, decoded_gc;
    XSizeHints hints, decoded_hints;
    XColor input, color;
    memset(&gc, 0xa5, sizeof(gc)); gc.foreground = 0x12345678;
    assert(rpc_pack_gc(&frame, 0, &gc, GCForeground) == 0);
    assert(rpc_unpack_gc(&frame, 0, &decoded_gc, GCForeground) == 0);
    assert(decoded_gc.foreground == gc.foreground && decoded_gc.font == 0);
    hints.flags = PMinSize; hints.min_width = 23; hints.min_height = 29;
    assert(rpc_pack_hints(&frame, 1, &hints) == 0);
    assert(rpc_unpack_hints(&frame, 1, &decoded_hints) == 0);
    assert(decoded_hints.min_width == 23 && decoded_hints.max_width == 0);
    input.red = 3; input.green = 5; input.blue = 7;
    assert(rpc_pack_color(&frame, 2, &input, false) == 0);
    assert(rpc_unpack_color(&frame, 2, &color, false) == 0);
    assert(color.pixel == 0 && color.red == 3 && color.blue == 7);
    {
        unsigned long words[] = {0, 0xfedcba98, 0xffffffff, 0x12345678};
        assert(rpc_pack_property(&frame, 3, (const unsigned char *)words, 4, 32) == 0);
        assert(frame.sizes[3] == 16);
        unsigned char *data = rpc_unpack_property(&frame, 3, 4, 32, true);
        assert(data && data[4 * sizeof(long)] == 0);
        for (size_t index = 0; index < 4; index++)
            assert(((long *)(void *)data)[index] == (long)(int32_t)words[index]);
        free(data);
    }
    for (int type = KeyPress; type <= MappingNotify; type++) {
        XEvent original, decoded;
        memset(&original, 0x5a, sizeof(original));
        original.type = type;
        original.xany.display = NULL;
        if (type == ClientMessage) original.xclient.format = 32;
        assert(rpc_pack_event(&frame, 4, &original, false) == 0);
        assert(rpc_unpack_event(&frame, 4, &decoded, NULL) == 0);
        assert(rpc_pack_event(&again, 4, &decoded, false) == 0);
        assert(frame.sizes[4] == again.sizes[4]);
        assert(memcmp(frame.data[4], again.data[4], frame.sizes[4]) == 0);
        for (unsigned state = 0; state < 2; state++) {
            if (type == MotionNotify) original.xmotion.state = state ? Button1Mask | Button3Mask : 0;
            for (unsigned bit = 0; bit < 25; bit++) {
                long mask = 1L << bit;
                bool expected = (_Xevent_to_mask[type] & mask) != 0 &&
                    (type != MotionNotify ||
                     (mask & (PointerMotionMask | PointerMotionHintMask | ButtonMotionMask)) ||
                     (mask & (Button1MotionMask | Button2MotionMask | Button3MotionMask |
                              Button4MotionMask | Button5MotionMask) & original.xmotion.state));
                assert(((rpc_event_mask(&original) & mask) != 0) == expected);
            }
        }
    }
    {
        XEvent event;
        event.type = ClientMessage; event.xclient.window = 2;
        event.xclient.message_type = 3; event.xclient.format = 8;
        memset(event.xclient.data.b, 0x37, sizeof(event.xclient.data.b));
        assert(rpc_pack_event(&frame, 5, &event, true) == 0);
        assert(rpc_get64(frame.data[5] + 8) == 0 && rpc_get64(frame.data[5] + 16) == 0);
    }
    rpc_clear(&frame); rpc_clear(&again);
    puts("P8 RPC codec: masked/uninitialized fields, LP64 properties, all core event records PASS");
    return 0;
}

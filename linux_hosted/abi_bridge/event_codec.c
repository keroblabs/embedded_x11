#include "rpc.h"
#include "event_fields.h"
#include <limits.h>
#include <string.h>

#define WRITE(member, kind) words[count++] = (uint64_t)event->member;
#define FIT_I(word, member) rpc_signed(word, sizeof(member))
#define FIT_U(word, member) rpc_unsigned(word, sizeof(member))
#define FIT_C(word, member) (CHAR_MIN < 0 ? rpc_signed(word, sizeof(member)) : rpc_unsigned(word, sizeof(member)))
#define SET_I(member, word) member = (int64_t)(word)
#define SET_U(member, word) member = (word)
#define SET_C(member, word) member = (char)(word)
#define READ(member, kind) do { \
    if (count >= length || !FIT_##kind(words[count], event->member)) \
        return rpc_error("invalid core event field"); \
    SET_##kind(event->member, words[count]); count++; \
} while (0);

int rpc_pack_event(RpcFrame *frame, unsigned index, const XEvent *event, bool sending)
{
    uint64_t words[64] = {0};
    size_t count = 3;
    if (!event || event->type < KeyPress || event->type > MappingNotify)
        return rpc_error("only core events are part of ABI v1");
    words[0] = (uint64_t)event->type;
    /* SendEvent ignores these fields; do not read uninitialized caller storage. */
    words[1] = sending ? 0 : event->xany.serial;
    words[2] = sending ? 0 : (uint64_t)event->xany.send_event;
    switch (event->type) {
        RPC_EVENT_CASES(WRITE)
    case KeymapNotify:
        for (unsigned number = 1; number < 32; number++)
            words[count++] = (unsigned char)event->xkeymap.key_vector[number];
        break;
    case ClientMessage:
        WRITE(xclient.window, U) WRITE(xclient.message_type, U) WRITE(xclient.format, I)
        if (event->xclient.format == 8)
            for (unsigned number = 0; number < 20; number++)
                words[count++] = (unsigned char)event->xclient.data.b[number];
        else if (event->xclient.format == 16)
            for (unsigned number = 0; number < 10; number++)
                words[count++] = (uint64_t)event->xclient.data.s[number];
        else if (event->xclient.format == 32)
            for (unsigned number = 0; number < 5; number++)
                words[count++] = (uint64_t)event->xclient.data.l[number];
        else return rpc_error("invalid ClientMessage format");
        break;
    default: return rpc_error("unhandled core event");
    }
    return rpc_words(frame, index, words, count);
}

int rpc_unpack_event(const RpcFrame *frame, unsigned index, XEvent *event, Display *display)
{
    uint64_t words[64];
    size_t count = 3, length = frame->sizes[index] / 8;
    if (length < 3 || length > 64 || frame->sizes[index] % 8 ||
        rpc_read_words(frame, index, words, length)) return rpc_error("invalid core event record");
    if (words[0] < KeyPress || words[0] > MappingNotify ||
        !rpc_unsigned(words[1], sizeof(event->xany.serial)) ||
        !rpc_signed(words[2], sizeof(event->xany.send_event)))
        return rpc_error("invalid core event prefix");
    memset(event, 0, sizeof(*event));
    event->type = (int)words[0]; event->xany.serial = (unsigned long)words[1];
    event->xany.send_event = (int)(int64_t)words[2]; event->xany.display = display;
    switch (event->type) {
        RPC_EVENT_CASES(READ)
    case KeymapNotify:
        if (length != 34) return rpc_error("invalid key-vector length");
        for (unsigned number = 1; number < 32; number++) {
            if (words[count] > UCHAR_MAX) return rpc_error("invalid key-vector byte");
            event->xkeymap.key_vector[number] = (char)words[count++];
        }
        break;
    case ClientMessage:
        READ(xclient.window, U) READ(xclient.message_type, U) READ(xclient.format, I)
        if (event->xclient.format == 8) {
            if (length - count != 20) return rpc_error("invalid 8-bit ClientMessage length");
            for (unsigned number = 0; number < 20; number++) {
                if (words[count] > UCHAR_MAX) return rpc_error("invalid ClientMessage byte");
                event->xclient.data.b[number] = (char)words[count++];
            }
        } else if (event->xclient.format == 16) {
            if (length - count != 10) return rpc_error("invalid 16-bit ClientMessage length");
            for (unsigned number = 0; number < 10; number++) {
                if (!rpc_signed(words[count], sizeof(short))) return rpc_error("invalid ClientMessage short");
                event->xclient.data.s[number] = (short)(int64_t)words[count++];
            }
        } else if (event->xclient.format == 32) {
            if (length - count != 5) return rpc_error("invalid 32-bit ClientMessage length");
            for (unsigned number = 0; number < 5; number++) {
                if (!rpc_signed(words[count], sizeof(long))) return rpc_error("invalid ClientMessage long");
                event->xclient.data.l[number] = (long)(int64_t)words[count++];
            }
        } else return rpc_error("invalid ClientMessage format");
        break;
    default: return rpc_error("unhandled core event");
    }
    return count == length ? 0 : rpc_error("trailing core event fields");
}

long rpc_event_mask(const XEvent *event)
{
    switch (event->type) {
    case KeyPress: return KeyPressMask;
    case KeyRelease: return KeyReleaseMask;
    case ButtonPress: return ButtonPressMask;
    case ButtonRelease: return ButtonReleaseMask;
    case MotionNotify:
        return PointerMotionMask | PointerMotionHintMask | ButtonMotionMask |
            (event->xmotion.state & Button1Mask ? Button1MotionMask : 0) |
            (event->xmotion.state & Button2Mask ? Button2MotionMask : 0) |
            (event->xmotion.state & Button3Mask ? Button3MotionMask : 0) |
            (event->xmotion.state & Button4Mask ? Button4MotionMask : 0) |
            (event->xmotion.state & Button5Mask ? Button5MotionMask : 0);
    case EnterNotify: return EnterWindowMask;
    case LeaveNotify: return LeaveWindowMask;
    case FocusIn: case FocusOut: return FocusChangeMask;
    case KeymapNotify: return KeymapStateMask;
    case Expose: case GraphicsExpose: case NoExpose: return ExposureMask;
    case VisibilityNotify: return VisibilityChangeMask;
    case CreateNotify: return SubstructureNotifyMask;
    case DestroyNotify: case UnmapNotify: case MapNotify: case ReparentNotify:
    case ConfigureNotify: case GravityNotify: case CirculateNotify:
        return StructureNotifyMask | SubstructureNotifyMask;
    case MapRequest: case ConfigureRequest: case CirculateRequest: return SubstructureRedirectMask;
    case ResizeRequest: return ResizeRedirectMask;
    case PropertyNotify: return PropertyChangeMask;
    case ColormapNotify: return ColormapChangeMask;
    default: return 0;
    }
}

#ifndef WMB_WIRE_SCRIPT_H
#define WMB_WIRE_SCRIPT_H
#include <X11/Xlib.h>
#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    FILE *file;
    unsigned int step;
} WmbWireLog;
void wmb_wire_begin(WmbWireLog *log, Display *display, FILE *file);
void wmb_wire_record(void *argument, bool output, const void *bytes, size_t size);
int wmb_wire_script(Display *display, WmbWireLog *log);
void wmb_native_capture(int fd, WmbWireLog *log);
#endif

#include "wmb_test_client.h"
#include "wmb_xlib_internal.h"
#include "wire_script.h"
#include <stdio.h>

void wmb_wire_test_client(void *argument)
{
    WmbTestResult *result = argument;
    Display *display = XOpenDisplay(NULL);
    WmbWireLog log;
    FILE *file = NULL;
    result->result = 1;
    if (!display) goto done;
    file = fopen(result->capture_path, "wb");
    if (!file) { perror("WMB wire capture"); goto close; }
    wmb_wire_begin(&log, display, file);
    ((WmbDisplay *)(void *)display->xcb)->connection->trace = wmb_wire_record;
    ((WmbDisplay *)(void *)display->xcb)->connection->trace_data = &log;
    result->result = wmb_wire_script(display, &log);
    ((WmbDisplay *)(void *)display->xcb)->connection->trace = NULL;
    if (fclose(file) != 0) result->result = 1;
close:
    XCloseDisplay(display);
done:
    printf("P5 wire capture %s\n", result->result ? "FAIL" : "PASS");
    result->done = true;
}

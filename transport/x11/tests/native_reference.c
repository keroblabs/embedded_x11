#include "wire_script.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    Display *display;
    FILE *file;
    WmbWireLog log;
    int result;
    int depth = 16;
    if (argc != 2 && argc != 3) return 2;
    if (argc == 3) {
        if (!strcmp(argv[2], "24")) depth = 24;
        else if (strcmp(argv[2], "16")) return 2;
    }
    display = XOpenDisplay(NULL);
    if (!display) { fputs("Stock Xlib reference display unavailable\n", stderr); return 1; }
    if (DefaultDepth(display, 0) != depth) {
        fprintf(stderr, "Wire reference requires depth %d\n", depth);
        XCloseDisplay(display);
        return 1;
    }
    file = fopen(argv[1], "wb");
    if (!file) { perror("wire reference"); XCloseDisplay(display); return 1; }
    wmb_wire_begin(&log, display, file);
    wmb_native_capture(ConnectionNumber(display), &log);
    result = wmb_wire_script(display, &log);
    wmb_native_capture(-1, NULL);
    if (fclose(file) != 0) result = 1;
    XCloseDisplay(display);
    return result;
}

#include "wmb_xlib_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value) do { \
    if (!(value)) { fprintf(stderr, "P5 buffer FAIL %d: %s\n", __LINE__, #value); return 1; } \
} while (0)
static bool fail;
void *__real_realloc(void *memory, size_t size);
void *__wrap_realloc(void *memory, size_t size)
{
    if (fail) { fail = false; return NULL; }
    return __real_realloc(memory, size);
}

int main(void)
{
    Display display = {0};
    Display *dpy = &display;
    WmbConnection connection = {0};
    WmbDisplay transport = {.connection = &connection};
    char *original, *allocated;
    size_t i;
    display.xcb = (struct _X11XCBPrivate *)(void *)&transport;
    original = display.buffer = malloc(16);
    CHECK(original != NULL);
    for (i = 0; i < 12; i++) original[i] = (char)i;
    display.bufptr = original + 12;
    display.bufmax = original + 16;
    display.last_req = original + 4;
    fail = true;
    CHECK(_XBufferGrow(&display, 100) < 0);
    CHECK(display.buffer == original && display.bufptr == original + 12 &&
          display.bufmax == original + 16 && display.last_req == original + 4);
    CHECK(_XBufferGrow(&display, 100) == 0);
    CHECK(display.bufptr - display.buffer == 12 && display.last_req == display.buffer + 4);
    for (i = 0; i < 12; i++) CHECK(display.buffer[i] == (char)i);
    original = display.buffer;
    connection.in_flight = true;
    CHECK(_XBufferGrow(&display, 20000) < 0 && display.buffer == original);
    connection.in_flight = false;
    BufAlloc(char *, allocated, 20000);
    CHECK(allocated == display.buffer + 12);
    for (i = 0; i < 20000; i++) CHECK(allocated[i] == 0);
    free(display.buffer);
    puts("PASS P5 buffer: growth, offsets, OOM preservation, request lease, BufAlloc");
    return 0;
}

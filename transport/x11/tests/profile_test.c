#include "Xlibint.h"
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void *xreallocarray(void *memory, size_t count, size_t size);

#define CHECK(value) do { \
    if (!(value)) { fprintf(stderr, "P6 client profile FAIL %d: %s\n", __LINE__, #value); return 1; } \
} while (0)

int wmb_xlib_profile_test(void)
{
    char name[8], small[2];
    char *memory = malloc(4);
    CHECK(memory != NULL);
    memcpy(memory, "abc", 4);
    errno = 0;
    CHECK(xreallocarray(memory, SIZE_MAX, 2) == NULL && errno == ENOMEM);
    CHECK(memcmp(memory, "abc", 4) == 0);
    free(memory);
    CHECK(_XGetHostname(name, sizeof(name)) == 3 && strcmp(name, "WMB") == 0);
    CHECK(_XGetHostname(small, sizeof(small)) == 1 && strcmp(small, "W") == 0);
    CHECK(strcmp(XDisplayName(NULL), "WMB") == 0);
    CHECK(XInitThreads() == 0);
    CHECK(XGetDefault(NULL, "application", "option") == NULL);
    CHECK(XReadBitmapFileData("unsupported", NULL, NULL, NULL, NULL, NULL) == BitmapOpenFailed);
    CHECK(XWriteBitmapFile(NULL, "unsupported", None, 0, 0, 0, 0) == BitmapOpenFailed);
    CHECK(XStringToKeysym("Escape") == XK_Escape);
    CHECK(XStringToKeysym("U20AC") == 0x010020ac);
    CHECK(strcmp(XKeysymToString(XK_Escape), "Escape") == 0);
    CHECK(XStringToKeysym("WMB_missing_keysym") == NoSymbol);
    return 0;
}

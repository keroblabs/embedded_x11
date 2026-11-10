#include <config.h>
#include <X11/fonts/fntfilst.h>
#include <stdio.h>

int wmb_font_profile_test(void)
{
    FontDirectoryPtr directory = NULL;
    if (FontFileReadDirectory("/unsupported-font-directory", &directory) != BadFontPath ||
        directory != NULL) {
        fputs("P6 font profile: unsupported directory was not refused\n", stderr);
        return 1;
    }
    return 0;
}

#include "capture.h"
#include "hal_display.h"
#include <stdio.h>
#include <string.h>

int wmb_host_capture(const char *path)
{
    const hal_framebuffer_t *framebuffer = hal_display_get_fb();
    FILE *file;
    if (!path || !framebuffer) {
        fputs("WMB capture: missing output path or framebuffer\n", stderr);
        return -1;
    }
    file = fopen(path, "wb");
    if (!file) { perror("WM capture"); return -1; }
    if (fprintf(file, "P6\n%u %u\n255\n", framebuffer->width, framebuffer->height) < 0)
        goto failed;
    for (uint32_t y = 0; y < framebuffer->height; y++)
        for (uint32_t x = 0; x < framebuffer->width; x++) {
            uint32_t value = 0;
            unsigned char rgb[3];
            memcpy(&value, (const unsigned char *)framebuffer->pixels +
                (size_t)y * framebuffer->stride + x * (framebuffer->bpp / 8),
                framebuffer->bpp / 8);
            rgb[0] = (unsigned char)(framebuffer->bpp == 16 ?
                ((value >> 11) & 31) * 255 / 31 : (value >> 16) & 255);
            rgb[1] = (unsigned char)(framebuffer->bpp == 16 ?
                ((value >> 5) & 63) * 255 / 63 : (value >> 8) & 255);
            rgb[2] = (unsigned char)(framebuffer->bpp == 16 ?
                (value & 31) * 255 / 31 : value & 255);
            if (fwrite(rgb, 1, 3, file) != 3) goto failed;
        }
    if (fclose(file) != 0) { perror("WM capture close"); return -1; }
    return 0;
failed:
    perror("WM capture write");
    if (fclose(file) != 0) perror("WM capture close");
    return -1;
}

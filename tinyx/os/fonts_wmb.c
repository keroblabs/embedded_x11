#include <config.h>
#include <X11/fonts/fntfilst.h>

extern void ErrorF(const char *format, ...);
extern _Noreturn void FatalError(const char *format, ...);

/* Built-in FPEs register separately; no disk renderer is registered here. */
void FontFileRegisterFpeFunctions(void) {}

int FontFileReadDirectory(const char *directory, FontDirectoryPtr *result)
{
    *result = NULL;
    ErrorF("WMB fonts: disk directories are unsupported: %s\n", directory);
    return BadFontPath;
}

Bool FontFileDirectoryChanged(FontDirectoryPtr directory)
{
    (void)directory;
    FatalError("WMB fonts: disk directory monitoring is unsupported\n");
}

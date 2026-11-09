# TinyX P4 reference configuration

The two `*.reference.h` files are unedited outputs from a successful upstream
`configure` run on the pristine `490f5d7:tinyx` tree. They are evidence, not
configuration headers to include in a project build.

Reference environment: GCC 13.3, Autoconf 2.71, Linux x86_64; libXfont 1.5.4
and libfontenc 1.1.8. Autotools prerequisites and the legacy font library were
installed into a temporary local prefix, not globally.

```sh
autoreconf -fi
./configure --disable-xvesa --disable-xfbdev --disable-install-setuid \
    --disable-xdmcp --disable-xdm-auth-1 --disable-dbe --disable-xf86bigfont \
    --with-default-font-path=built-ins
```

Set `ACLOCAL_PATH`, `LIBTOOLIZE`, `PKG_CONFIG_PATH`, `CPPFLAGS`, and `LDFLAGS`
to a local prerequisite prefix if those dependencies are not installed.
Capture `include/dix-config.h` and `include/kdrive-config.h` after success.
The stock configure run retains its native transports; the project does not.

## Curated configuration differences

The [DIX template](../../cmake/dix-config.h.in) and
[KDrive template](../../cmake/kdrive-config.h.in) retain the reference's core
extensions, standard C facilities, built-in font path, and KDrive selection.

| Difference | Reason |
|---|---|
| Vendor/package/OS banner | Identifies this HAL-based WMB variant rather than the reference host/distribution |
| `VENDOR_STRING` / `VENDOR_RELEASE` | Supplies the names actually consumed by `site.h` and the X setup block |
| `_XSERVER64` | Computed from `sizeof(unsigned long)`, not copied from the reference machine |
| `X_BYTE_ORDER` | Computed by CMake and defined only if the platform headers have not defined it |
| No `TCPCONN`, `UNIXCONN`, `IPv6`, native mapping/process/header feature macros | Native transport/mapping sources are excluded; runtime time comes from the HAL |
| No `MONOTONIC_CLOCK` | `GetTimeInMillis()` calls `hal_tick_ms()` in the guarded project path |
| `SMART_SCHEDULE` layout retained | DAMAGE uses its client priority fields; project OS initialization disables the native signal timer |
| No forced `NDEBUG` | Debug/Release behavior belongs to the build configuration |
| No `XORG_*` release, build address, or host filesystem prefix | Not consumed by the selected server path; no reference-host identity is carried into the image |
| `XKB`, `DBE`, `XF86BIGFONT`, `XDMCP` stay undefined | Upstream uses presence guards; defining these to zero would still enable their guarded code |
| SHM registration excluded under `WMB_DEVEL` | The old server calls SHM initialization unconditionally; the extension is not selected |

The generated KDrive configuration is force-included through the shared
`wmb_tinyx_config` target before any X protocol header. This is required for ABI
consistency: loading `_XSERVER64` after the first X header produced different
`KeySym` widths and a keymap overrun during real startup.

## Font dependency

[WmbXfont.cmake](../../cmake/WmbXfont.cmake) downloads the upstream libXfont
1.5.4 release with a pinned SHA-256. It compiles the bitmap/built-in/font utility
subset locally, without FreeType or a font-server transport. Its
[configuration](../../cmake/xfont-config.h.in) is curated from the successful
bitmap-only dependency configure run. The PCF `fixed` and `cursor` fonts are
already embedded in that release; root-window startup requires no font files.

The checked patches repair an empty-directory read-before-buffer in built-in
font setup and signed shifts while reading 32-bit PCF values. Both were found
by the real-server sanitizer gate; checks are not disabled to accommodate them.

This is the P4 bootstrap font set, not completion of D-38's larger shipping font
registry. The dependency's notices remain in its downloaded source tree.

## Frozen source inventory

[tinyx-sources.reference](tinyx-sources.reference) lists the 173 tested server
translation units: DIX 25, MI 35, FB 32, MIEXT 21, extensions 39, OS 11, KDrive/HAL
9, and project lifecycle 1. CMake compares the selected source set with this
manifest and rejects excluded native sources. Deliberate module changes require
reviewing/updating the manifest and the component inventory together.

[The startup reference](tinyx-startup.reference.cmake) independently records the
expected visual/depth/masks and extension set. The CTest startup wrapper compares
the actual log with it for the selected backend's native geometry/format.
[The RGB565 root image](tinyx-root-rgb565.png) is an 800x480 capture of the actual
framebuffer at SDL's presentation point, including the software X cursor.

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

P6 adds a separate conditional built-in-only profile patch: standard headers and
memory `BufFile` operations remain, native descriptor operations do not. Disk/
catalogue/fontenc source backends are excluded and directory access is explicitly
refused by an owned adapter. zlib remains necessary for embedded compressed
fonts; libfontenc is no longer a dependency. The historical configure environment
above is unchanged evidence, not the current build prerequisite list.

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

## P5 stock-Xlib wire references

[The depth-16](xlib-wire-golden-16.json) and
[depth-24/XRGB8888](xlib-wire-golden-32.json) fixtures were captured from system
Xlib 1.8.7 against separate stock Xvfb servers. The collector records
actual native socket writes/reads, not a reconstruction made from WMB.
The shared ten-call script is compiled once with stock headers/library and once
with the matching WMB client cohort.

```sh
cmake -S . -B build-p5 -DWMB_TINYX_BUILD_SERVER=ON -DWMB_BUILD_WIRE_REFERENCE=ON
cmake --build build-p5 --target wmb_wire_reference wmb_host
```

In a separate terminal, launch a temporary reference server:

```sh
Xvfb -displayfd 1 -screen 0 800x480x16 -nolisten tcp -noreset -ac
```

Use the printed display number as `N`:

```sh
DISPLAY=:N ./build-p5/bin/wmb_wire_reference build-p5/reference.wire 16
python3 transport/x11/tests/compare_wire.py build-p5/reference.wire \
    --record docs/reference/xlib-wire-golden-16.json
SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
    ./build-p5/bin/wmb_host --wire-test build-p5/subject.wire
python3 transport/x11/tests/compare_wire.py build-p5/subject.wire \
    --compare docs/reference/xlib-wire-golden-16.json
```

Stop that reference server afterward. Repeat with screen depth `24`, collector
argument `24`, and `xlib-wire-golden-32.json` for the other format. The collector
is a host-only verification program; it is not linked into WMB and does not
reintroduce a native transport profile.

IDs, sequence fields and **only protocol-defined padding** are normalized.
Padding is not specified by X11 and stock servers may leave it uninitialized;
comparing it would not test protocol equivalence. All defined request/reply
fields and payload bytes must match. The parser's negative tests prove that a
payload mutation is not normalized away, and truncated captures are rejected.

[The P5 client image](tinyx-client-rgb565.png) captures the actual white window,
black rectangle, text and software cursor. Client pixels are also checked in CI.
[The client source manifest](xlib-sources.reference) now freezes the 248-unit
P6 client/API/profile cohort independently of the unchanged 173-unit server
cohort. [The font manifest](xfont-sources.reference) freezes 30 units including
its owned refusal adapter. The wire fixtures are unchanged and still pass.

## P7 JWM references

[The JWM source manifest](jwm-sources.reference) freezes 52 units: 49 retained
native sources plus owned entry/configuration/timing adapters.
[The native configuration](jwm-config.reference.h) is actual Autoconf output
from the pristine imported JWM tree (`73ab9f28...:jwm`), with optional libraries
and native language support disabled. It is probe evidence, not the project's
production configuration. Only trailing whitespace on the empty `ICONV_CONST`
definition is normalized.

Reference generation used Autoconf 2.71, gettext tooling 0.21, GCC 13.3 and
system Xlib. The import identifies itself as JWM 2.4.4 but has no separately
verified official release identity. The repository's exact import/hash is the
patch baseline. The original source includes Makefile.in rather than Makefile.am;
generate configure/config.h.in without replacing its make templates.

[The actual RGB565 desktop](tinyx-jwm-rgb565.png) shows native decorations, task
list, three registered clients and uptime clock after the move/resize/menu
workload. It was captured from the real SDL presentation framebuffer, not
reconstructed from application data. Reproduce the PPM capture with
`SDL_RENDER_DRIVER=software WMB_WM_CAPTURE=build-p7/jwm.ppm build-p7/bin/wmb_host --wm-test`.

The optional independent harness links the exact same portable JWM archive to
genuine system Xlib and a temporary Xvfb. It verifies two decorated native
clients, registry launch and cleanup; native interaction/precision/cancellation
are tested separately on the subject in-process stack.
See [the JWM guide](../../jwm/wmb/README.md).

---
description: "Use when editing TinyX project code, KDrive HAL integration, WMB_DEVEL guards, or TinyX CMake targets. Preserve the reference variant and keep device policy in the HAL port."
applyTo: "tinyx/**"
---
# WMB TinyX variant

- Define `WMB_DEVEL` for all project-specific changes to existing upstream TinyX
  code, including necessary engine fixes and future Linux/threading removal.
  Use `#if defined(WMB_DEVEL)`; retain unchanged reference code in
  `#else`. Only an undefined macro selects the reference path; any definition,
  including zero or an empty value, selects WMB. Use `#if !defined(WMB_DEVEL)`
  for reference-only regions.
- Prefer existing DDX seams and the KDrive HAL port for device/input policy.
  Do not relocate or duplicate upstream engine code just to hide changes.
- Select new project-owned replacement/port files only in the project build.
  Keep platform-neutral HAL contracts independent of TinyX/project flags.
- Consume `wmb_tinyx_project` from `tinyx/wmb_devel.cmake` consistently across
  TinyX targets and conditional-header consumers; propagate it PUBLIC where
  ABI layout is exposed. Never set the selector globally or on HAL/framework
  libraries. It is enabled in Release as well as Debug, on host and device.
- Guard affected includes, declarations, structure layouts, and behavior.
  Preserve upstream license/copyright bytes and encoding.
- Validate real-code behavior, undefined reference builds, and defined
  one/zero/empty project builds. Standalone
  KDrive tests are not proof of full TinyX or embedded readiness.

See [the methodology](../../docs/47-tinyx-project-methodology.md) and
[the KDrive status](../../docs/43-kdrive-module-plan.md).

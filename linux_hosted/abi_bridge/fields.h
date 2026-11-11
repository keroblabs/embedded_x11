#ifndef WMB_RPC_FIELDS_H
#define WMB_RPC_FIELDS_H
#define RPC_GC_FIELDS(F) \
 F(GCFunction, function, I) F(GCPlaneMask, plane_mask, U) \
 F(GCForeground, foreground, U) F(GCBackground, background, U) \
 F(GCLineWidth, line_width, I) F(GCLineStyle, line_style, I) \
 F(GCCapStyle, cap_style, I) F(GCJoinStyle, join_style, I) \
 F(GCFillStyle, fill_style, I) F(GCFillRule, fill_rule, I) \
 F(GCArcMode, arc_mode, I) F(GCTile, tile, U) F(GCStipple, stipple, U) \
 F(GCTileStipXOrigin, ts_x_origin, I) F(GCTileStipYOrigin, ts_y_origin, I) \
 F(GCFont, font, U) F(GCSubwindowMode, subwindow_mode, I) \
 F(GCGraphicsExposures, graphics_exposures, I) \
 F(GCClipXOrigin, clip_x_origin, I) F(GCClipYOrigin, clip_y_origin, I) \
 F(GCClipMask, clip_mask, U) F(GCDashOffset, dash_offset, I) F(GCDashList, dashes, C)
#define RPC_CHANGES_FIELDS(F) \
 F(CWX, x, I) F(CWY, y, I) F(CWWidth, width, I) F(CWHeight, height, I) \
 F(CWBorderWidth, border_width, I) F(CWSibling, sibling, U) F(CWStackMode, stack_mode, I)
#define RPC_ATTRIBUTES_FIELDS(F) \
 F(CWBackPixmap, background_pixmap, U) F(CWBackPixel, background_pixel, U) \
 F(CWBorderPixmap, border_pixmap, U) F(CWBorderPixel, border_pixel, U) \
 F(CWBitGravity, bit_gravity, I) F(CWWinGravity, win_gravity, I) \
 F(CWBackingStore, backing_store, I) F(CWBackingPlanes, backing_planes, U) \
 F(CWBackingPixel, backing_pixel, U) F(CWSaveUnder, save_under, I) \
 F(CWEventMask, event_mask, I) F(CWDontPropagate, do_not_propagate_mask, I) \
 F(CWOverrideRedirect, override_redirect, I) F(CWColormap, colormap, U) F(CWCursor, cursor, U)
#define RPC_HINTS_FIELDS(F) \
 F(UINT64_MAX, flags, I) F(USPosition | PPosition, x, I) F(USPosition | PPosition, y, I) \
 F(USSize | PSize, width, I) F(USSize | PSize, height, I) \
 F(PMinSize, min_width, I) F(PMinSize, min_height, I) \
 F(PMaxSize, max_width, I) F(PMaxSize, max_height, I) \
 F(PResizeInc, width_inc, I) F(PResizeInc, height_inc, I) \
 F(PAspect, min_aspect.x, I) F(PAspect, min_aspect.y, I) \
 F(PAspect, max_aspect.x, I) F(PAspect, max_aspect.y, I) \
 F(PBaseSize, base_width, I) F(PBaseSize, base_height, I) F(PWinGravity, win_gravity, I)
#define RPC_VISUAL_FIELDS(F) \
 F(UINT64_MAX, visualid, U) F(UINT64_MAX, class, I) F(UINT64_MAX, red_mask, U) \
 F(UINT64_MAX, green_mask, U) F(UINT64_MAX, blue_mask, U) \
 F(UINT64_MAX, bits_per_rgb, I) F(UINT64_MAX, map_entries, I)
#define RPC_WINDOW_FIELDS(F) \
 F(UINT64_MAX, x, I) F(UINT64_MAX, y, I) F(UINT64_MAX, width, I) \
 F(UINT64_MAX, height, I) F(UINT64_MAX, border_width, I) F(UINT64_MAX, depth, I) \
 F(UINT64_MAX, root, U) F(UINT64_MAX, class, I) F(UINT64_MAX, bit_gravity, I) \
 F(UINT64_MAX, win_gravity, I) F(UINT64_MAX, backing_store, I) \
 F(UINT64_MAX, backing_planes, U) F(UINT64_MAX, backing_pixel, U) \
 F(UINT64_MAX, save_under, I) F(UINT64_MAX, colormap, U) F(UINT64_MAX, map_installed, I) \
 F(UINT64_MAX, map_state, I) F(UINT64_MAX, all_event_masks, I) \
 F(UINT64_MAX, your_event_mask, I) F(UINT64_MAX, do_not_propagate_mask, I) \
 F(UINT64_MAX, override_redirect, I)
#endif

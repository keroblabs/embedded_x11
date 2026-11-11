#ifndef WMB_RPC_EVENT_FIELDS_H
#define WMB_RPC_EVENT_FIELDS_H
#define RPC_POINTER_EVENT(F, p) \
 F(p.window,U) F(p.root,U) F(p.subwindow,U) F(p.time,U) \
 F(p.x,I) F(p.y,I) F(p.x_root,I) F(p.y_root,I) F(p.state,U)
#define RPC_EVENT_CASES(F) \
 case KeyPress: case KeyRelease: \
    RPC_POINTER_EVENT(F,xkey) F(xkey.keycode,U) F(xkey.same_screen,I) break; \
 case ButtonPress: case ButtonRelease: \
    RPC_POINTER_EVENT(F,xbutton) F(xbutton.button,U) F(xbutton.same_screen,I) break; \
 case MotionNotify: \
    RPC_POINTER_EVENT(F,xmotion) F(xmotion.is_hint,C) F(xmotion.same_screen,I) break; \
 case EnterNotify: case LeaveNotify: \
    RPC_POINTER_EVENT(F,xcrossing) F(xcrossing.mode,I) F(xcrossing.detail,I) \
    F(xcrossing.same_screen,I) F(xcrossing.focus,I) break; \
 case FocusIn: case FocusOut: \
    F(xfocus.window,U) F(xfocus.mode,I) F(xfocus.detail,I) break; \
 case Expose: F(xexpose.window,U) F(xexpose.x,I) F(xexpose.y,I) \
    F(xexpose.width,I) F(xexpose.height,I) F(xexpose.count,I) break; \
 case GraphicsExpose: F(xgraphicsexpose.drawable,U) F(xgraphicsexpose.x,I) \
    F(xgraphicsexpose.y,I) F(xgraphicsexpose.width,I) F(xgraphicsexpose.height,I) \
    F(xgraphicsexpose.count,I) F(xgraphicsexpose.major_code,I) F(xgraphicsexpose.minor_code,I) break; \
 case NoExpose: F(xnoexpose.drawable,U) F(xnoexpose.major_code,I) F(xnoexpose.minor_code,I) break; \
 case VisibilityNotify: F(xvisibility.window,U) F(xvisibility.state,I) break; \
 case CreateNotify: F(xcreatewindow.parent,U) F(xcreatewindow.window,U) \
    F(xcreatewindow.x,I) F(xcreatewindow.y,I) F(xcreatewindow.width,I) \
    F(xcreatewindow.height,I) F(xcreatewindow.border_width,I) F(xcreatewindow.override_redirect,I) break; \
 case DestroyNotify: F(xdestroywindow.event,U) F(xdestroywindow.window,U) break; \
 case UnmapNotify: F(xunmap.event,U) F(xunmap.window,U) F(xunmap.from_configure,I) break; \
 case MapNotify: F(xmap.event,U) F(xmap.window,U) F(xmap.override_redirect,I) break; \
 case MapRequest: F(xmaprequest.parent,U) F(xmaprequest.window,U) break; \
 case ReparentNotify: F(xreparent.event,U) F(xreparent.window,U) F(xreparent.parent,U) \
    F(xreparent.x,I) F(xreparent.y,I) F(xreparent.override_redirect,I) break; \
 case ConfigureNotify: F(xconfigure.event,U) F(xconfigure.window,U) F(xconfigure.x,I) \
    F(xconfigure.y,I) F(xconfigure.width,I) F(xconfigure.height,I) F(xconfigure.border_width,I) \
    F(xconfigure.above,U) F(xconfigure.override_redirect,I) break; \
 case ConfigureRequest: F(xconfigurerequest.parent,U) F(xconfigurerequest.window,U) \
    F(xconfigurerequest.x,I) F(xconfigurerequest.y,I) F(xconfigurerequest.width,I) \
    F(xconfigurerequest.height,I) F(xconfigurerequest.border_width,I) \
    F(xconfigurerequest.above,U) F(xconfigurerequest.detail,I) F(xconfigurerequest.value_mask,U) break; \
 case GravityNotify: F(xgravity.event,U) F(xgravity.window,U) F(xgravity.x,I) F(xgravity.y,I) break; \
 case ResizeRequest: F(xresizerequest.window,U) F(xresizerequest.width,I) F(xresizerequest.height,I) break; \
 case CirculateNotify: F(xcirculate.event,U) F(xcirculate.window,U) F(xcirculate.place,I) break; \
 case CirculateRequest: F(xcirculaterequest.parent,U) F(xcirculaterequest.window,U) \
    F(xcirculaterequest.place,I) break; \
 case PropertyNotify: F(xproperty.window,U) F(xproperty.atom,U) F(xproperty.time,U) \
    F(xproperty.state,I) break; \
 case SelectionClear: F(xselectionclear.window,U) F(xselectionclear.selection,U) \
    F(xselectionclear.time,U) break; \
 case SelectionRequest: F(xselectionrequest.owner,U) F(xselectionrequest.requestor,U) \
    F(xselectionrequest.selection,U) F(xselectionrequest.target,U) \
    F(xselectionrequest.property,U) F(xselectionrequest.time,U) break; \
 case SelectionNotify: F(xselection.requestor,U) F(xselection.selection,U) \
    F(xselection.target,U) F(xselection.property,U) F(xselection.time,U) break; \
 case ColormapNotify: F(xcolormap.window,U) F(xcolormap.colormap,U) F(xcolormap.new,I) \
    F(xcolormap.state,I) break; \
 case MappingNotify: F(xmapping.request,I) F(xmapping.first_keycode,I) F(xmapping.count,I) break;
#endif

"""Typed host-only RPC adapters; api.def remains the API and ownership authority."""
from pathlib import Path
from collections.abc import Callable

SCALAR_OUTPUT = {
    "int*": ("integer", "int", "rpc_signed"),
    "unsigned int*": ("unsigned_integer", "unsigned int", "rpc_unsigned"),
    "unsigned long*": ("word", "unsigned long", "rpc_unsigned"),
    "Window*": ("window_id", "Window", "rpc_unsigned"),
    "Atom*": ("atom", "Atom", "rpc_unsigned"),
}
STRUCTURES = {
    "gc_values": ("gc", "gc"),
    "changes": ("changes", "changes"),
    "attributes": ("attributes", "attributes"),
    "size_hints": ("hints", "hints"),
    "color": ("color", "color"),
    "out_color": ("color", "color"),
    "out_attributes": ("window", "window"),
    "event": ("event", "event"),
    "out_event": ("event", "event"),
}


def outputs(root: Path, definition: dict, banner: str,
            parameters: Callable[..., str]) -> dict[Path, str]:
    functions = definition["functions"]
    metadata = banner + "#ifndef WMB_RPC_METADATA_H\n#define WMB_RPC_METADATA_H\n"
    metadata += "typedef enum {\n"
    for index, entry in enumerate(functions):
        metadata += f"    RPC_SLOT_{entry['name']} = {index},\n"
    metadata += "} RpcSlot;\n"
    metadata += "static const unsigned char rpc_parameter_count[] = {\n"
    metadata += "".join(f"    {len(entry['params'])},\n" for entry in functions)
    metadata += "};\n#endif\n"
    proxy = banner
    server = banner + """static int dispatch_typed(RpcServerConnection *connection,
                          const RpcFrame *request, RpcFrame *reply)
{
    WmbApiCall call = {.slot = request->operation};
    RpcValue values[RPC_ARGUMENTS] = {0};
    void *allocated[RPC_ARGUMENTS] = {0};
    void *native_owned[RPC_ARGUMENTS + 1] = {0};
    int status = -1;
    switch (request->operation) {
"""
    local = {"XFree", "XVisualIDFromVisual", "XDisplayOfScreen", "XPutBackEvent"}
    for entry in functions:
        name, params, returns = entry["name"], entry["params"], entry["returns"]
        guard = f"#if WMB_API_VERSION >= {entry['since']}\n" if entry["since"] > 1 else ""
        proxy += guard + f"static {returns} proxy_{name}({parameters(entry)})\n{{\n"
        special = {
            "XOpenDisplay": "return rpc_proxy_open(p0);",
            "XCloseDisplay": "return rpc_proxy_close(p0);",
            "XFree": "free(p0); return 1;",
            "XVisualIDFromVisual": "if (!p0) { rpc_proxy_fatal(\"NULL Visual\"); }\n    return p0->visualid;",
            "XDisplayOfScreen": "return rpc_proxy_screen_owner(p0);",
            "XPutBackEvent": "return rpc_proxy_put_event(p0, p1);",
        }
        if name in special:
            proxy += "    " + special[name] + "\n}\n"
            if guard:
                proxy += "#endif\n"
        else:
            if params and params[0]["kind"] == "display":
                context = "rpc_proxy_display(p0)"
            elif params and params[0]["kind"] == "screen":
                context = "rpc_proxy_display(rpc_proxy_screen_owner(p0))"
            else:
                context = "rpc_proxy_control()"
            proxy += f"    RpcProxyDisplay *connection = {context};\n"
            proxy += f"    RpcFrame request = {{.operation = RPC_SLOT_{name}}}, reply = {{0}};\n"
            if returns != "void":
                proxy += f"    {returns} result;\n"
            if name == "XNextEvent":
                proxy += "    if (rpc_proxy_take_event(connection, 0, 0, -1, p1)) return 0;\n"
            elif name == "XCheckMaskEvent":
                proxy += "    if (rpc_proxy_take_event(connection, p1, 0, -2, p2)) return True;\n"
            elif name == "XCheckTypedWindowEvent":
                proxy += "    if (rpc_proxy_take_event(connection, 0, p1, p2, p3)) return True;\n"
            output_order = sorted(enumerate(params),
                                  key=lambda item: item[1]["kind"] not in ("out_i", "out_u"))
            for index, param in output_order:
                kind, var = param["kind"], f"p{index}"
                target = f"request.values[{index}]"
                if kind in ("i", "u"):
                    proxy += f"    {target} = (uint64_t){var};\n"
                elif kind in ("display", "screen", "gc"):
                    proxy += f"    {target} = rpc_proxy_token(connection, {var}, RPC_REF_{kind.upper()});\n"
                elif kind == "visual":
                    proxy += f"    {target} = {var} ? {var}->visualid : 0;\n"
                else:
                    proxy += f"    {target} = {var} != NULL;\n"
                    if not param.get("nullable", False):
                        proxy += f"    if (!{var}) rpc_proxy_fatal(\"NULL {name} argument {index}\");\n"
                    mask = f"(uint64_t)p{param['mask']}" if "mask" in param else "0"
                    count = f"rpc_proxy_count((int64_t)p{param['count']})" if "count" in param else "0"
                    if kind == "string":
                        proxy += f"    if ({var} && rpc_blob(&request, {index}, {var}, strlen({var}) + 1)) goto invalid;\n"
                    elif kind == "bytes":
                        proxy += f"    if (rpc_blob(&request, {index}, {var}, {count})) goto invalid;\n"
                    elif kind == "points":
                        proxy += f"    if (rpc_pack_points(&request, {index}, {var}, {count})) goto invalid;\n"
                    elif kind in ("pixels", "atoms"):
                        proxy += f"    if (rpc_pack_xids(&request, {index}, {var}, {count})) goto invalid;\n"
                    elif kind == "property":
                        proxy += f"    if (rpc_pack_property(&request, {index}, {var}, {count}, p{param['format']})) goto invalid;\n"
                    elif kind in ("gc_values", "changes", "attributes"):
                        codec = STRUCTURES[kind][0]
                        proxy += f"    if (rpc_pack_{codec}(&request, {index}, {var}, {mask})) goto invalid;\n"
                    elif kind == "size_hints":
                        proxy += f"    if (rpc_pack_hints(&request, {index}, {var})) goto invalid;\n"
                    elif kind == "color":
                        proxy += f"    if (rpc_pack_color(&request, {index}, {var}, false)) goto invalid;\n"
                    elif kind == "event":
                        proxy += f"    if (rpc_pack_event(&request, {index}, {var}, true)) goto invalid;\n"
                    elif not kind.startswith("out_"):
                        raise ValueError(f"unhandled proxy parameter {name}: {kind}")
            proxy += "    if (rpc_proxy_exchange(connection, &request, &reply)) goto invalid;\n"
            condition = "reply.result != 0" if entry.get("output_if_true") else "true"
            if name == "XNextEvent":
                condition = "(int64_t)reply.result >= 0"
            output_order = sorted(enumerate(params),
                                  key=lambda item: item[1]["kind"] not in ("out_i", "out_u"))
            for index, param in output_order:
                kind, var = param["kind"], f"p{index}"
                if kind.startswith("out_") or kind == "color":
                    proxy += f"    if ({condition}) {{\n"
                    if kind in ("out_i", "out_u"):
                        _, ctype, check = SCALAR_OUTPUT[param["type"]]
                        proxy += f"        if (!{check}(reply.values[{index}], sizeof(*{var}))) goto invalid;\n"
                        cast = "(int64_t)" if kind == "out_i" else ""
                        proxy += f"        *{var} = ({ctype}){cast}reply.values[{index}];\n"
                    elif kind in ("color", "out_color"):
                        proxy += f"        if (rpc_unpack_color(&reply, {index}, {var}, true)) goto invalid;\n"
                    elif kind == "out_event":
                        proxy += f"        if (rpc_unpack_event(&reply, {index}, {var}, p0)) goto invalid;\n"
                    elif kind == "out_attributes":
                        proxy += f"        if (rpc_proxy_window(connection, &reply, {index}, {var})) goto invalid;\n"
                    elif kind == "out_windows":
                        count = f"*p{param['count']}"
                        proxy += f"        *{var} = reply.values[{index}] ? rpc_unpack_xids(&reply, {index}, {count}) : NULL;\n"
                        proxy += f"        if (reply.values[{index}] && !*{var}) goto invalid;\n"
                    elif kind == "out_property":
                        count, form = f"*p{param['count']}", f"*p{param['format']}"
                        proxy += f"        *{var} = reply.values[{index}] ? rpc_unpack_property(&reply, {index}, {count}, {form}, true) : NULL;\n"
                        proxy += f"        if (reply.values[{index}] && !*{var}) goto invalid;\n"
                    else:
                        raise ValueError(f"unhandled proxy output {name}: {kind}")
                    proxy += "    }\n"
            result_kind = entry["result"]
            if result_kind in ("i", "u"):
                check = "rpc_signed" if result_kind == "i" else "rpc_unsigned"
                proxy += f"    if (!{check}(reply.result, sizeof(result))) goto invalid;\n"
                cast = "(int64_t)" if result_kind == "i" else ""
                proxy += f"    result = ({returns}){cast}reply.result;\n"
                if name in ("XPending", "XQLength"):
                    proxy += "    result += (int)connection->event_count;\n"
            elif result_kind == "screen":
                proxy += "    result = rpc_proxy_screen(connection, reply.result);\n"
            elif result_kind == "gc":
                proxy += f"    result = rpc_proxy_gc(connection, reply.result, {'true' if name == 'XCreateGC' else 'false'});\n"
            elif result_kind == "visual":
                proxy += "    result = rpc_proxy_visual(connection, &reply, RPC_RESULT_BLOB);\n"
            elif result_kind in ("borrowed_string", "owned_string"):
                cache = "true" if result_kind == "borrowed_string" else "false"
                proxy += f"    result = rpc_proxy_string(connection, &reply, {cache}, RPC_SLOT_{name});\n"
            elif result_kind != "void":
                raise ValueError(f"unhandled proxy return {name}: {result_kind}")
            if name == "XFreeGC":
                proxy += "    rpc_proxy_forget_gc(connection, p1);\n"
            if name == "XSync":
                proxy += "    if (p1) rpc_proxy_discard_events(connection);\n"
            proxy += "    rpc_clear(&request); rpc_clear(&reply);\n"
            if returns != "void":
                proxy += "    return result;\n"
            else:
                proxy += "    return;\n"
            proxy += f"invalid:\n    rpc_clear(&request); rpc_clear(&reply);\n    rpc_proxy_fatal(\"{name} marshalling or provider failure\");\n}}\n"
            if guard:
                proxy += "#endif\n"

        server += f"    case RPC_SLOT_{name}: {{\n"
        if name in local:
            server += "        rpc_error(\"client-local operation must not cross the wire\"); goto cleanup;\n    }\n"
            continue
        server += f"        if (rpc_server_shape(request, {len(params)})) goto cleanup;\n"
        for index, param in enumerate(params):
            kind, ctype = param["kind"], param["type"]
            expr = f"request->values[{index}]"
            destination = f"call.args.{name}.p{index}"
            if kind in ("i", "u"):
                check = "rpc_signed" if kind == "i" else "rpc_unsigned"
                server += f"        if (request->sizes[{index}]) {{ rpc_error(\"unexpected scalar blob\"); goto cleanup; }}\n"
                server += f"        if (!{check}({expr}, sizeof({ctype}))) {{ rpc_error(\"argument overflow\"); goto cleanup; }}\n"
                cast = "(int64_t)" if kind == "i" else ""
                server += f"        {destination} = ({ctype}){cast}{expr};\n"
            elif kind in ("display", "screen", "gc"):
                server += f"        if (request->sizes[{index}]) {{ rpc_error(\"unexpected object blob\"); goto cleanup; }}\n"
                server += f"        {destination} = rpc_server_reference(connection, RPC_REF_{kind.upper()}, {expr});\n"
                server += f"        if (!{destination}) goto cleanup;\n"
            elif kind == "visual":
                server += f"        if (request->sizes[{index}]) {{ rpc_error(\"unexpected visual blob\"); goto cleanup; }}\n"
                server += f"        values[{index}].visual.visualid = (VisualID){expr};\n"
                server += f"        {destination} = {expr} ? &values[{index}].visual : NULL;\n"
            else:
                nullable = "true" if param.get("nullable") else "false"
                server += f"        if (rpc_server_presence(request, {index}, {nullable})) goto cleanup;\n"
                mask = f"request->values[{param['mask']}]" if "mask" in param else "0"
                count = f"rpc_server_count(request->values[{param['count']}])" if "count" in param else "0"
                if kind in ("out_i", "out_u"):
                    server += f"        if (request->sizes[{index}]) {{ rpc_error(\"unexpected output blob\"); goto cleanup; }}\n"
                    member = SCALAR_OUTPUT[ctype][0]
                    server += f"        {destination} = &values[{index}].{member};\n"
                elif kind in STRUCTURES:
                    codec, member = STRUCTURES[kind]
                    server += f"        {destination} = {expr} ? &values[{index}].{member} : NULL;\n"
                    if kind in ("gc_values", "attributes", "changes"):
                        server += f"        if ({expr} && rpc_unpack_{codec}(request, {index}, &values[{index}].{member}, {mask})) goto cleanup;\n"
                        server += f"        if (!{expr} && {mask}) {{ rpc_error(\"NULL masked argument\"); goto cleanup; }}\n"
                    elif kind == "size_hints":
                        server += f"        if (rpc_unpack_hints(request, {index}, &values[{index}].hints)) goto cleanup;\n"
                    elif kind == "color":
                        server += f"        if (rpc_unpack_color(request, {index}, &values[{index}].color, false)) goto cleanup;\n"
                    elif kind == "event":
                        server += f"        if (rpc_unpack_event(request, {index}, &values[{index}].event, connection->display)) goto cleanup;\n"
                    elif kind.startswith("out_"):
                        server += f"        if (request->sizes[{index}]) {{ rpc_error(\"unexpected output blob\"); goto cleanup; }}\n"
                elif kind == "string":
                    server += f"        if (rpc_server_string(request, {index})) goto cleanup;\n"
                    server += f"        {destination} = {expr} ? (const char *)request->data[{index}] : NULL;\n"
                elif kind == "bytes":
                    server += f"        if (request->sizes[{index}] != {count}) {{ rpc_error(\"byte count mismatch\"); goto cleanup; }}\n"
                    server += f"        {destination} = {expr} ? (const char *)request->data[{index}] : NULL;\n"
                elif kind == "points":
                    server += f"        allocated[{index}] = rpc_unpack_points(request, {index}, {count});\n"
                    server += f"        if (!allocated[{index}]) goto cleanup;\n        {destination} = {expr} ? allocated[{index}] : NULL;\n"
                elif kind in ("atoms", "pixels"):
                    server += f"        allocated[{index}] = rpc_unpack_xids(request, {index}, {count});\n"
                    server += f"        if (!allocated[{index}]) goto cleanup;\n        {destination} = {expr} ? allocated[{index}] : NULL;\n"
                elif kind == "property":
                    server += f"        allocated[{index}] = rpc_unpack_property(request, {index}, {count}, (int)request->values[{param['format']}], false);\n"
                    server += f"        if (!allocated[{index}]) goto cleanup;\n        {destination} = {expr} ? allocated[{index}] : NULL;\n"
                elif kind == "out_windows":
                    server += f"        if (request->sizes[{index}]) {{ rpc_error(\"unexpected output blob\"); goto cleanup; }}\n"
                    server += f"        {destination} = &values[{index}].children;\n"
                elif kind == "out_property":
                    server += f"        if (request->sizes[{index}]) {{ rpc_error(\"unexpected output blob\"); goto cleanup; }}\n"
                    server += f"        {destination} = &values[{index}].bytes;\n"
                else:
                    raise ValueError(f"unhandled server argument {name}: {kind}")
        if name == "XOpenDisplay":
            server += "        if (connection->display) { rpc_error(\"repeated connection open\"); goto cleanup; }\n"
        if name == "XCloseDisplay":
            server += "        if (rpc_server_prepare_close(connection)) goto cleanup;\n"
        if name == "XFreeGC":
            server += "        if (rpc_server_gc_owned(connection, call.args.XFreeGC.p1)) goto cleanup;\n"
        if name == "wmbYield":
            server += "        call.result.wmbYield = wmb_gateway_table()->wmbYield(call.args.wmbYield.p0);\n"
        else:
            server += f"        status = wmb_gateway_submit(&call);\n        if (status) goto native_outputs_{name};\n"
        server += "        status = 0;\n"
        result = f"call.result.{name}"
        result_kind = entry["result"]
        if result_kind in ("i", "u"):
            server += f"        reply->result = (uint64_t){result};\n"
            if name in ("XCreateWindow", "XCreateSimpleWindow"):
                server += f"        if ({result} && rpc_server_track_window(connection, {result}, call.args.{name}.p1)) status = -1;\n"
        elif result_kind in ("display", "screen", "gc"):
            owned = "true" if name == "XCreateGC" else "false"
            if result_kind == "display":
                server += f"        connection->display = {result};\n"
            server += f"        reply->result = rpc_server_add(connection, RPC_REF_{result_kind.upper()}, {result}, {owned});\n"
            server += f"        if ({result} && !reply->result) status = -1;\n"
        elif result_kind == "visual":
            server += f"        reply->result = {result} != NULL;\n"
            server += f"        if (rpc_pack_visual(reply, RPC_RESULT_BLOB, {result})) status = -1;\n"
        elif result_kind in ("borrowed_string", "owned_string"):
            server += f"        reply->result = {result} != NULL;\n"
            server += f"        if ({result} && rpc_blob(reply, RPC_RESULT_BLOB, {result}, strlen({result}) + 1)) status = -1;\n"
        elif result_kind != "void":
            raise ValueError(f"unhandled server return {name}: {result_kind}")
        condition = f"{result} != 0" if entry.get("output_if_true") else "true"
        if name == "XNextEvent":
            condition = f"{result} >= 0"
        for index, param in enumerate(params):
            kind = param["kind"]
            if kind.startswith("out_") or kind == "color":
                server += f"        if ({condition}) {{\n"
                if kind in ("out_i", "out_u"):
                    member = SCALAR_OUTPUT[param["type"]][0]
                    server += f"            reply->values[{index}] = (uint64_t)values[{index}].{member};\n"
                elif kind in ("color", "out_color"):
                    server += f"            if (rpc_pack_color(reply, {index}, &values[{index}].color, true)) status = -1;\n"
                elif kind == "out_event":
                    server += f"            if (rpc_pack_event(reply, {index}, &values[{index}].event, false)) status = -1;\n"
                elif kind == "out_attributes":
                    server += f"            uint64_t screen = rpc_server_add(connection, RPC_REF_SCREEN, values[{index}].window.screen, false);\n"
                    server += f"            if (rpc_pack_window(reply, {index}, &values[{index}].window, screen)) status = -1;\n"
                elif kind == "out_windows":
                    count = f"values[{param['count']}].{SCALAR_OUTPUT[params[param['count']]['type']][0]}"
                    server += f"            reply->values[{index}] = values[{index}].children != NULL;\n"
                    server += f"            if (rpc_pack_xids(reply, {index}, values[{index}].children, {count})) status = -1;\n"
                elif kind == "out_property":
                    count = f"values[{param['count']}].word"
                    form = f"values[{param['format']}].integer"
                    server += f"            reply->values[{index}] = values[{index}].bytes != NULL;\n"
                    server += f"            if (values[{index}].bytes && rpc_pack_property(reply, {index}, values[{index}].bytes, {count}, {form})) status = -1;\n"
                server += "        }\n"
        if name == "XFreeGC":
            server += "        rpc_server_forget(connection, RPC_REF_GC, request->values[1]);\n"
        if name == "XDestroyWindow":
            server += "        rpc_server_destroyed_window(connection, call.args.XDestroyWindow.p1);\n"
        if name == "XCloseDisplay":
            server += "        rpc_server_closed(connection);\n"
        server += f"        goto native_outputs_{name};\n"
        server += f"native_outputs_{name}:\n"
        for index, param in enumerate(params):
            if param["kind"] in ("out_property", "out_windows"):
                member = "bytes" if param["kind"] == "out_property" else "children"
                server += f"        native_owned[{index}] = values[{index}].{member};\n"
        if result_kind == "owned_string":
            server += f"        native_owned[RPC_ARGUMENTS] = {result};\n"
        server += "        break;\n    }\n"
    proxy += "static wmb_api_t proxy_api = {\n"
    proxy += "    .magic = WMB_API_MAGIC, .version = WMB_API_VERSION,\n"
    proxy += "    .count = WMB_API_SLOT_COUNT, .layout = WMB_API_LAYOUT,\n"
    for entry in functions:
        if entry["since"] > 1:
            proxy += f"#if WMB_API_VERSION >= {entry['since']}\n"
        proxy += f"    .{entry['name']} = proxy_{entry['name']},\n"
        if entry["since"] > 1:
            proxy += "#endif\n"
    proxy += "};\n"
    server += """    default: rpc_error("unknown typed operation");
    }
cleanup:
    for (unsigned index = 0; index < RPC_ARGUMENTS; index++) free(allocated[index]);
    for (unsigned index = 0; index <= RPC_ARGUMENTS; index++)
        if (native_owned[index] && rpc_server_native_free(native_owned[index])) status = -1;
    return status;
}
"""
    return {
        root / "linux_hosted/abi_bridge/rpc_metadata.h": metadata,
        root / "linux_hosted/abi_bridge/proxy_generated.inc": proxy,
        root / "linux_hosted/abi_bridge/server_generated.inc": server,
    }

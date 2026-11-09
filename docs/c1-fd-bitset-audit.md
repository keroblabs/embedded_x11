# C1 - TinyX DIX fd-bitset audit

Baseline: `a7aa264`. Scope: every C/header file in `tinyx/dix/` and
`tinyx/include/`, using `fd_set|FD_SET|XFD_|ClientsWithInput|AllSockets|MaxClients`.
There are 15 matching lines. None performs an fd-set operation.

| File / symbols | Classification | Reason |
|---|---|---|
| `include/opaque.h`: `MaxClients` declaration | Client capacity | Bounds the dense client array, not a file descriptor |
| `include/dix.h`, `dix/globals.c`: `currentMaxClients` | Client count | Highest live client slot plus one |
| `dix/dispatch.c`: selection initialization, `Dispatch`, client close/allocation | Client indices/counts | `clientReady` contains `ClientRec.index`; request allocation and iteration use the same dense array |
| `dix/resource.c`: `FreeAllResources` | Client iteration | Walks resource tables by client slot |
| `dix/devices.c`: device close notification | Client iteration | Walks `clients[]`, not OS descriptors |
| `dix/main.c`: server-client initialization | Client count | Starts at one for internal client zero |
| `dix/dixutils.c`: sleep notifications | Client iteration | Walks client sleep records/slots |

The fd sets and `ConnectionTranslation[fd]` are confined to the excluded native
OS transport. [osdep.h](../tinyx/os/osdep.h) contains fd-set declarations and
`MAXSELECT`; the WMB branch excludes those declarations while retaining their
reference branch. `OsCommRec` is a private OS transport type, not a DIX bitset.

**Conclusion:** no genuine DIX fd-bitset dependency was found. P5 can use dense
client indices and a private in-process connection structure without treating
indices as native file descriptors. It must preserve client sleep, grabs,
priority, request suspension, and lifecycle semantics; P4 deliberately accepts
no external clients and does not claim to verify that future transport.

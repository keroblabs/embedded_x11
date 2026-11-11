#ifndef WMB_ABI_PROBE_H
#define WMB_ABI_PROBE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const void *table;
    uint32_t now_ms, cycles_requested, cycles_completed;
    uint32_t keys, motions, buttons, polls;
    uint32_t window, parent, green;
    int x, y;
    bool ready, done, arm_input, input_armed;
    int result;
} WmbAbiProbe;
void wmb_probe_v1_prepare(WmbAbiProbe *state);
void wmb_probe_v2_prepare(WmbAbiProbe *state);
int wmb_probe_v1_entry(int argc, char **argv);
int wmb_probe_v2_entry(int argc, char **argv);
int wmb_threads_start(const void *table);
int wmb_threads_done(void);
int wmb_threads_join(void);
#endif

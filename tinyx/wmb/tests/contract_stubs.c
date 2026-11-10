#include "hal_display.h"
#include "hal_input.h"
#include "hal_alloc.h"
#include "hal_time.h"
#include "fiber_port.h"
#include "intr_handler.h"
#include "wmb_app_table.h"
#include <stdio.h>
#include <stdlib.h>

static _Noreturn void unexpected_backend(void)
{
    fputs("P6 link probe unexpectedly invoked a platform backend\n", stderr);
    abort();
}

const wmb_app_t wmb_app_table[] = {{NULL, NULL, 0}};
const size_t wmb_app_table_size = 0;

int hal_display_init(void) { unexpected_backend(); }
void hal_display_shutdown(void) { unexpected_backend(); }
const hal_framebuffer_t *hal_display_get_fb(void) { unexpected_backend(); }
void hal_display_flush(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    (void)x; (void)y; (void)width; (void)height;
    unexpected_backend();
}
hal_display_status_t hal_display_pump(void) { unexpected_backend(); }
int hal_display_set_power(int on) { (void)on; unexpected_backend(); }
int hal_input_init(void) { unexpected_backend(); }
void hal_input_shutdown(void) { unexpected_backend(); }
bool hal_input_has_data(hal_input_dev_t device) { (void)device; unexpected_backend(); }
bool hal_input_read_byte(hal_input_dev_t device, uint8_t *byte)
{
    (void)device; (void)byte;
    unexpected_backend();
}
int hal_input_control(hal_input_dev_t device, hal_input_control_t control, int32_t value)
{
    (void)device; (void)control; (void)value;
    unexpected_backend();
}
void hal_input_pump(void) { unexpected_backend(); }
uint32_t hal_tick_ms(void) { return 1234; }
uint64_t hal_tick_us(void) { return 1234000; }
void *wmb_alloc(size_t size) { return malloc(size); }
void wmb_free(void *memory) { free(memory); }

void fiber_port_init(uint32_t period) { (void)period; unexpected_backend(); }
void *fiber_port_init_stack(void *stack, size_t size, fiber_func_t entry, void *argument)
{
    (void)stack; (void)size; (void)entry; (void)argument;
    unexpected_backend();
}
void fiber_port_request_switch(void) { unexpected_backend(); }
uint32_t fiber_port_irq_lock(void) { return 0; }
void fiber_port_irq_unlock(uint32_t previous) { (void)previous; }
void *fiber_port_alloc_stack(size_t size) { (void)size; unexpected_backend(); }
void fiber_port_free_stack(void *stack, size_t size)
{
    (void)stack; (void)size;
    unexpected_backend();
}
void intr_global_set_enabled(bool_t enabled) { (void)enabled; }
void intr_handler_call_crash(uint32_t code) { (void)code; unexpected_backend(); }

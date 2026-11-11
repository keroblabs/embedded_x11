#include "hal_display.h"
#include "hal_input.h"
#include "hal_time.h"
#include "fiber_port.h"
#include "intr_handler.h"
#include "runtime.h"

const char *volatile wmb_p9a_missing_port;
_Noreturn static void missing(const char *name)
{
    wmb_p9a_missing_port = name;
    for (;;) {}
}
int wmb_os_console_write(const char *text, int length)
{
    (void)text; (void)length;
    missing("console");
}
_Noreturn void wmb_os_abort(void) { missing("termination"); }
void _fini(void) { missing("CRT finalization"); }
int hal_display_init(void) { missing("display init"); }
void hal_display_shutdown(void) { missing("display shutdown"); }
const hal_framebuffer_t *hal_display_get_fb(void) { missing("framebuffer"); }
void hal_display_flush(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    (void)x; (void)y; (void)width; (void)height; missing("display flush");
}
hal_display_status_t hal_display_pump(void) { missing("display pump"); }
int hal_display_set_power(int on) { (void)on; missing("display power"); }
int hal_input_init(void) { missing("input init"); }
void hal_input_shutdown(void) { missing("input shutdown"); }
bool hal_input_has_data(hal_input_dev_t device) { (void)device; missing("input poll"); }
bool hal_input_read_byte(hal_input_dev_t device, uint8_t *byte)
{
    (void)device; (void)byte; missing("input read");
}
int hal_input_control(hal_input_dev_t device, hal_input_control_t control, int32_t value)
{
    (void)device; (void)control; (void)value; missing("input control");
}
void hal_input_pump(void) { missing("input pump"); }
uint32_t hal_tick_ms(void) { missing("millisecond clock"); }
uint64_t hal_tick_us(void) { missing("microsecond clock"); }
void fiber_port_init(uint32_t period) { (void)period; missing("fiber init"); }
void *fiber_port_init_stack(void *stack, size_t size, fiber_func_t entry, void *argument)
{
    (void)stack; (void)size; (void)entry; (void)argument; missing("fiber context");
}
void fiber_port_request_switch(void) { missing("fiber switch"); }
uint32_t fiber_port_irq_lock(void) { missing("interrupt lock"); }
void fiber_port_irq_unlock(uint32_t previous) { (void)previous; missing("interrupt unlock"); }
void *fiber_port_alloc_stack(size_t size) { (void)size; missing("stack pool"); }
void fiber_port_free_stack(void *stack, size_t size)
{
    (void)stack; (void)size; missing("stack release");
}
void intr_global_set_enabled(bool_t enabled) { (void)enabled; missing("interrupt state"); }
void intr_handler_call_crash(uint32_t code) { (void)code; missing("runtime fault"); }

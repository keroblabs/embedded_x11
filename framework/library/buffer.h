#ifndef __BUFFER_H__
#define __BUFFER_H__

//////////////////////////////////////////////////////////////////////////////////////

#include "types.h"

/* Mutual exclusion is selected at build time:
 *   USE_MULTITHREAD - t_threads mutex, one per buffer
 *   USE_FIBER       - fiber_critical_enter/exit (masks IRQs, suppresses
 *                     the preemption tick).  This is the WMB setting.
 *   neither         - no synchronisation; correct only while the
 *                     scheduler is cooperative and nothing yields inside
 *                     a buffer call.  See 90-decisions.md D-47.
 */

//////////////////////////////////////////////////////////////////////////////////////

typedef struct
{
    uint16_t pointer_in;
    uint16_t pointer_out;

    uint16_t size;
    uint16_t fill_size;

    uint8_t *buffer;

    #ifdef USE_MULTITHREAD
    void * mutex;
    #endif

} buffer_data_t;

//////////////////////////////////////////////////////////////////////////////////////

void buffer_init(buffer_data_t * buffer_data, uint8_t * buffer_ptr, uint16_t size);

void buffer_set_buffer(buffer_data_t * buffer_data, uint8_t * buffer_ptr);

uint8_t buffer_read(buffer_data_t * buffer_data);
uint8_t buffer_peek(buffer_data_t * buffer_data);
uint16_t buffer_read_buffer(buffer_data_t * buffer_data, uint8_t * out_data, uint16_t size);
void * buffer_read_pointer(buffer_data_t * buffer_data);

void buffer_write(buffer_data_t * buffer_data, uint8_t in_data);
void buffer_write_buffer(buffer_data_t * buffer_data, uint8_t * in_data, uint16_t size);
void buffer_write_pointer(buffer_data_t * buffer_data, void * p_pointer);

uint16_t buffer_get_available_size(buffer_data_t * buffer_data);
uint16_t buffer_get_size(buffer_data_t * buffer_data);
void buffer_reset(buffer_data_t * buffer_data);

bool_t buffer_is_full(buffer_data_t * buffer_data);
bool_t buffer_is_empty(buffer_data_t * buffer_data);

//////////////////////////////////////////////////////////////////////////////////////

#endif // __BUFFER_H__


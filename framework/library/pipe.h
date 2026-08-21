#ifndef __PIPE_DATA_H__
#define __PIPE_DATA_H__

////////////////////////////////////////////////////////////////////////////////////////

#include "types.h"

////////////////////////////////////////////////////////////////////////////////////////

/* A pipe is a bounded FIFO of *owned* byte buffers.  Unlike buffer.c it
 * preserves record boundaries: each entry keeps its own length, and
 * overflow discards a whole entry, never part of one.
 *
 * Ownership:
 *   pipe_write()          takes ownership; the pipe frees the buffer on
 *                         overflow and in pipe_deinit()
 *   pipe_send_buffer()    copies; the caller keeps its own buffer
 *   pipe_read()           transfers ownership out - the caller must
 *                         pipe_release_buffer() it
 *   pipe_get/pipe_get_at  borrow only; the pipe still owns the entry
 *
 * Mutual exclusion follows buffer.c: USE_FIBER selects
 * fiber_critical_enter/exit.  See 90-decisions.md D-47.
 */

#define MAX_PIPE_SIZE           32

////////////////////////////////////////////////////////////////////////////////////////

typedef struct
{
    uint16_t pointer_in;
    uint16_t pointer_out;

    uint16_t max_size;
    uint16_t count;

    uint8_t * pipe_list[MAX_PIPE_SIZE];
    uint16_t  pipe_size[MAX_PIPE_SIZE];

} pipe_data_t;

////////////////////////////////////////////////////////////////////////////////////////

void        pipe_init(pipe_data_t * pipe_data, uint16_t size);
void        pipe_deinit(pipe_data_t * pipe_data);

uint8_t   * pipe_alloc_buffer(uint16_t size);
void        pipe_release_buffer(uint8_t * buffer);

uint16_t    pipe_get_buffer_size(pipe_data_t * pipe);
uint8_t   * pipe_read(pipe_data_t * pipe, uint16_t * size);
uint8_t   * pipe_get(pipe_data_t * pipe, uint16_t * size);
uint8_t   * pipe_get_at(pipe_data_t * pipe, uint16_t index, uint16_t * size);
bool_t      pipe_write(pipe_data_t * pipe, uint8_t * buffer, uint16_t size);
bool_t      pipe_send_buffer(pipe_data_t * pipe, const uint8_t * buffer, uint16_t size);

bool_t      pipe_is_full(pipe_data_t * pipe);
bool_t      pipe_is_empty(pipe_data_t * pipe);

uint16_t    pipe_get_count(pipe_data_t * pipe);

////////////////////////////////////////////////////////////////////////////////////////

#endif // __PIPE_DATA_H__

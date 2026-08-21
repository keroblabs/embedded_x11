#include "pipe.h"

#if defined(USE_MULTITHREAD)
#include "t_threads.h"
#elif defined(USE_FIBER)
#include "fiber.h"
#endif

////////////////////////////////////////////////////////////////////////////////////////

#define PIPE_MALLOC     MMALLOC
#define PIPE_FREE       MFREE

////////////////////////////////////////////////////////////////////////////////////////

/* DI()/EI() cannot be used here: EI() enables interrupts unconditionally,
 * so a nested region would re-enable early.  See 90-decisions.md D-47.4. */
static void pipe_lock(void)
{
    #if defined(USE_MULTITHREAD)
    /* Tima OS builds mask globally; a per-pipe mutex would deadlock the
     * ISR producer. */
    DI();
    #elif defined(USE_FIBER)
    fiber_critical_enter();
    #endif
}

static void pipe_unlock(void)
{
    #if defined(USE_MULTITHREAD)
    EI();
    #elif defined(USE_FIBER)
    fiber_critical_exit();
    #endif
}

////////////////////////////////////////////////////////////////////////////////////////

uint8_t * pipe_alloc_buffer(uint16_t size)
{
    if (size == 0) return NULL;
    return (uint8_t *)PIPE_MALLOC(size);
}

void pipe_release_buffer(uint8_t * buffer)
{
    if (buffer == NULL) return;
    PIPE_FREE(buffer);
}

void pipe_init(pipe_data_t * pipe_data, uint16_t size)
{
    if (pipe_data == NULL) return;

    memset(pipe_data, 0x00, sizeof(pipe_data_t));

    if ((size == 0) || (size > MAX_PIPE_SIZE)) size = MAX_PIPE_SIZE;

    pipe_data->max_size = size;
}

void pipe_deinit(pipe_data_t * pipe_data)
{
    uint16_t index;

    if (pipe_data == NULL) return;

    pipe_lock();

    while (pipe_data->pointer_in != pipe_data->pointer_out)
    {
        index = pipe_data->pointer_out;

        pipe_release_buffer(pipe_data->pipe_list[index]);
        pipe_data->pipe_list[index] = NULL;

        pipe_data->pointer_out++;
        if (pipe_data->pointer_out == pipe_data->max_size) pipe_data->pointer_out = 0;
    }

    pipe_data->count = 0;

    pipe_unlock();
}

uint16_t pipe_get_count(pipe_data_t * pipe)
{
    uint16_t ret;

    if (pipe == NULL) return 0;

    pipe_lock();
    ret = pipe->count;
    pipe_unlock();

    return ret;
}

bool_t pipe_is_empty(pipe_data_t * pipe)
{
    return (pipe_get_count(pipe) == 0) ? TRUE : FALSE;
}

bool_t pipe_is_full(pipe_data_t * pipe)
{
    uint16_t count;

    if (pipe == NULL) return TRUE;

    pipe_lock();
    count = pipe->count;
    pipe_unlock();

    /* One slot is always left free so in == out means empty, never full. */
    return (count >= (uint16_t)(pipe->max_size - 1)) ? TRUE : FALSE;
}

uint16_t pipe_get_buffer_size(pipe_data_t * pipe)
{
    uint16_t size = 0;

    if (pipe == NULL) return 0;

    pipe_lock();

    if (pipe->count != 0)
    {
        size = pipe->pipe_size[pipe->pointer_out];
    }

    pipe_unlock();

    return size;
}

uint8_t * pipe_get_at(pipe_data_t * pipe, uint16_t index, uint16_t * size)
{
    uint8_t * ret_value = NULL;
    uint16_t pipe_index;

    if (pipe == NULL) return NULL;

    pipe_lock();

    if (index < pipe->count)
    {
        pipe_index = pipe->pointer_out + index;
        if (pipe_index >= pipe->max_size) pipe_index -= pipe->max_size;

        ret_value = pipe->pipe_list[pipe_index];

        if (size != NULL) *size = pipe->pipe_size[pipe_index];
    }

    pipe_unlock();

    return ret_value;
}

uint8_t * pipe_get(pipe_data_t * pipe, uint16_t * size)
{
    return pipe_get_at(pipe, 0, size);
}

uint8_t * pipe_read(pipe_data_t * pipe, uint16_t * size)
{
    uint8_t * ret_value = NULL;

    if (pipe == NULL) return NULL;

    pipe_lock();

    if (pipe->count != 0)
    {
        ret_value = pipe->pipe_list[pipe->pointer_out];

        if (size != NULL) *size = pipe->pipe_size[pipe->pointer_out];

        pipe->pipe_list[pipe->pointer_out] = NULL;

        pipe->pointer_out++;
        if (pipe->pointer_out == pipe->max_size) pipe->pointer_out = 0;

        pipe->count--;
    }

    pipe_unlock();

    return ret_value;
}

bool_t pipe_write(pipe_data_t * pipe, uint8_t * buffer, uint16_t size)
{
    bool_t dropped = FALSE;

    if ((pipe == NULL) || (buffer == NULL)) return FALSE;

    pipe_lock();

    /* Full: discard the oldest whole entry, never part of one. */
    if (pipe->count >= (uint16_t)(pipe->max_size - 1))
    {
        pipe_release_buffer(pipe->pipe_list[pipe->pointer_out]);
        pipe->pipe_list[pipe->pointer_out] = NULL;

        pipe->pointer_out++;
        if (pipe->pointer_out == pipe->max_size) pipe->pointer_out = 0;

        pipe->count--;
        dropped = TRUE;
    }

    pipe->pipe_list[pipe->pointer_in] = buffer;
    pipe->pipe_size[pipe->pointer_in] = size;

    pipe->pointer_in++;
    if (pipe->pointer_in == pipe->max_size) pipe->pointer_in = 0;

    pipe->count++;

    pipe_unlock();

    return (dropped == TRUE) ? FALSE : TRUE;
}

bool_t pipe_send_buffer(pipe_data_t * pipe, const uint8_t * buffer, uint16_t size)
{
    uint8_t * copy;

    if ((pipe == NULL) || (buffer == NULL) || (size == 0)) return FALSE;

    copy = pipe_alloc_buffer(size);
    if (copy == NULL) return FALSE;

    memcpy(copy, buffer, size);

    return pipe_write(pipe, copy, size);
}

#include "buffer.h"

#if defined(USE_MULTITHREAD)
#include "t_threads.h"
#elif defined(USE_FIBER)
#include "fiber.h"
#endif

////////////////////////////////////////////////////////////////////////////////////////

static void buffer_mutex_lock(buffer_data_t * buffer_data)
{
    #if defined(USE_MULTITHREAD)
    tthread_mutex_lock(buffer_data->mutex);
    #elif defined(USE_FIBER)
    /* Global, not per-buffer: the fiber critical section masks interrupts
     * and suppresses the preemption tick, which is what a ring shared
     * between a fiber and an ISR needs.  See 90-decisions.md D-47. */
    (void)buffer_data;
    fiber_critical_enter();
    #else
    (void)buffer_data;
    #endif
}

static void buffer_mutex_unlock(buffer_data_t * buffer_data)
{
    #if defined(USE_MULTITHREAD)
    tthread_mutex_unlock(buffer_data->mutex);
    #elif defined(USE_FIBER)
    (void)buffer_data;
    fiber_critical_exit();
    #else
    (void)buffer_data;
    #endif
}

////////////////////////////////////////////////////////////////////////////////////////

void buffer_init(buffer_data_t * buffer_data, uint8_t * buffer_ptr, uint16_t size)
{
    // init buffer
    buffer_data->buffer = buffer_ptr;
    buffer_data->size = size;
    buffer_data->pointer_in = 0;
    buffer_data->pointer_out = 0;

    #ifdef USE_MULTITHREAD
    buffer_data->mutex = tthread_mutex_init();
    #endif
}

void buffer_reset(buffer_data_t * buffer_data)
{
	if (buffer_data == NULL) return;
    buffer_data->pointer_in = 0;
    buffer_data->pointer_out = 0;
}

void buffer_set_buffer(buffer_data_t * buffer_data, uint8_t * buffer_ptr)
{
    buffer_data->buffer = buffer_ptr;
}

////////////////////////////////////////////////////////////////////////////////

static uint8_t buffer_read_internal(buffer_data_t * buffer_data)
{
	uint8_t ret_value = 0;

	if (buffer_data == NULL) return 0;

    // buffer is empty
	if (buffer_data->pointer_in == buffer_data->pointer_out)
    {
        return(0);
    }

    // get byte from buffer
	ret_value = buffer_data->buffer[buffer_data->pointer_out];

    // move out-pointer
	buffer_data->pointer_out++;
	if (buffer_data->pointer_out == buffer_data->size) buffer_data->pointer_out = 0;
    
	return(ret_value);
}

static void buffer_write_internal(buffer_data_t * buffer_data, uint8_t in_data)
{	
	if (buffer_data == NULL) return;
    
    // insert data into the buffer
	buffer_data->buffer[buffer_data->pointer_in] = in_data;
	buffer_data->pointer_in++;
    
    // update pointers and control overflow
	if (buffer_data->pointer_in == buffer_data->size) buffer_data->pointer_in = 0;
	if (buffer_data->pointer_in == buffer_data->pointer_out) buffer_data->pointer_out++;
	if (buffer_data->pointer_out == buffer_data->size) buffer_data->pointer_out = 0;
}

uint8_t buffer_read(buffer_data_t * buffer_data)
{
    uint8_t ret;

    buffer_mutex_lock(buffer_data);
    ret = buffer_read_internal(buffer_data);
    buffer_mutex_unlock(buffer_data);
    
    return ret;
}

void buffer_write(buffer_data_t * buffer_data, uint8_t in_data)
{
    buffer_mutex_lock(buffer_data);
    buffer_write_internal(buffer_data, in_data);
    buffer_mutex_unlock(buffer_data);
}

void buffer_write_pointer(buffer_data_t * buffer_data, void * p_pointer)
{
    uint8_t * p_value = (uint8_t *)&p_pointer;
    buffer_write_buffer(buffer_data, p_value, sizeof(void *));
}

void * buffer_read_pointer(buffer_data_t * buffer_data)
{
    void * ret = NULL;

    if (buffer_is_empty(buffer_data) == FALSE)
    {
        buffer_read_buffer(buffer_data, (uint8_t *)&ret, sizeof(void *));
    }

    return ret;
}

void buffer_write_buffer(buffer_data_t * buffer_data, uint8_t * in_data, uint16_t size)
{
	uint16_t counter;
	
    buffer_mutex_lock(buffer_data);
    
	for (counter = 0; counter < size; counter++)
	{
		buffer_write_internal(buffer_data, in_data[counter]);
	}
    
    buffer_mutex_unlock(buffer_data);
}

uint16_t buffer_read_buffer(buffer_data_t * buffer_data, uint8_t * out_data, uint16_t size)
{
	uint16_t counter;
	
    buffer_mutex_lock(buffer_data);
    
	for (counter = 0; counter < size; counter++)
	{
		// buffer is empty
		if (buffer_data->pointer_in == buffer_data->pointer_out) break;
	
		out_data[counter] = buffer_read_internal(buffer_data);
	}
	
    buffer_mutex_unlock(buffer_data);
    
	return counter;
}


uint8_t buffer_peek(buffer_data_t * buffer_data)
{
	uint8_t ret_value = 0;

	if (buffer_data == NULL) return 0;

    buffer_mutex_lock(buffer_data);

    // buffer is empty
	if (buffer_data->pointer_in == buffer_data->pointer_out)
    {
        buffer_mutex_unlock(buffer_data);
        return(0);
    }

    // get byte from buffer
	ret_value = buffer_data->buffer[buffer_data->pointer_out];
    
    buffer_mutex_unlock(buffer_data);
    
	return(ret_value);
}

uint16_t buffer_get_size(buffer_data_t * buffer_data)
{
    uint16_t size;

	if (buffer_data == NULL) return 0;
    
    buffer_mutex_lock(buffer_data);
    
    if (buffer_data->pointer_in >= buffer_data->pointer_out)
    {
        // in and out at same side
        size = buffer_data->pointer_in - buffer_data->pointer_out;
    }
    else
    {
        // in turned around
        size = (buffer_data->size - buffer_data->pointer_out) + buffer_data->pointer_in;
    }

    buffer_mutex_unlock(buffer_data);
    
    return(size);
}

uint16_t buffer_get_available_size(buffer_data_t * buffer_data)
{
    return buffer_data->size - buffer_get_size(buffer_data);
}

bool_t buffer_is_full(buffer_data_t * buffer_data)
{        
    bool_t ret = FALSE;
    
	if (buffer_data == NULL) return FALSE;
	
    buffer_mutex_lock(buffer_data);
    
    // input is just before output
	if (buffer_data->pointer_in == (buffer_data->pointer_out - 1)) ret = TRUE;

    // input is in the tail, and output is in the head
	else if ((buffer_data->pointer_in == (buffer_data->size - 1)) &&
             (buffer_data->pointer_out == 0)) ret = TRUE;

    buffer_mutex_unlock(buffer_data);

    return ret;
}

bool_t buffer_is_empty(buffer_data_t * buffer_data)
{
    bool_t ret = FALSE;
    
	if (buffer_data == NULL) return TRUE;

    buffer_mutex_lock(buffer_data);
	if (buffer_data->pointer_in == buffer_data->pointer_out) ret = TRUE;
    buffer_mutex_unlock(buffer_data);
    
    return ret;
}


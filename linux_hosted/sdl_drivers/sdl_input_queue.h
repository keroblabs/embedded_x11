#ifndef WMB_SDL_INPUT_QUEUE_H
#define WMB_SDL_INPUT_QUEUE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SDL_INPUT_QUEUE_SIZE 512u

typedef struct
{
    uint8_t bytes[SDL_INPUT_QUEUE_SIZE];
    size_t head;
    size_t size;
} sdl_input_queue_t;

static inline void sdl_input_queue_reset( sdl_input_queue_t *queue )
{
    queue->head = queue->size = 0;
}

static inline int sdl_input_queue_write( sdl_input_queue_t *queue, const uint8_t *bytes, size_t size )
{
    size_t tail, first;
    if( size > SDL_INPUT_QUEUE_SIZE - queue->size ) return -1;
    tail = (queue->head + queue->size) % SDL_INPUT_QUEUE_SIZE;
    first = SDL_INPUT_QUEUE_SIZE - tail;
    if( first > size ) first = size;
    memcpy( queue->bytes + tail, bytes, first );
    memcpy( queue->bytes, bytes + first, size - first );
    queue->size += size;
    return 0;
}

static inline uint8_t sdl_input_queue_peek( const sdl_input_queue_t *queue, size_t offset )
{
    return queue->bytes[(queue->head + offset) % SDL_INPUT_QUEUE_SIZE];
}

static inline uint8_t sdl_input_queue_read( sdl_input_queue_t *queue )
{
    uint8_t value;
    if( queue->size == 0 ) return 0;
    value = queue->bytes[queue->head];
    queue->head = (queue->head + 1u) % SDL_INPUT_QUEUE_SIZE;
    queue->size--;
    return value;
}

#endif

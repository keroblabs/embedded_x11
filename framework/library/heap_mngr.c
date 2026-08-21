#include "heap_mngr.h"

//////////////////////////////////////////////////////////////////////////////////////

typedef struct _heap_entry_t
{
    struct _heap_entry_t * p_next;
    void * p_data;
    uint32_t size;

} heap_entry_t;

//////////////////////////////////////////////////////////////////////////////////////

static heap_entry_t * p_heap_list = NULL;

//////////////////////////////////////////////////////////////////////////////////////

void * heap_mngr_malloc(uint32_t size)
{
    heap_entry_t * new_heap = NULL;
    void * p_data = wmb_alloc(size);
    if (p_data == NULL) return NULL;

    new_heap = (heap_entry_t *)wmb_alloc(sizeof(heap_entry_t));
    if (new_heap == NULL) 
    {
        wmb_free(p_data);
        return NULL;
    }
    
    memset(new_heap, 0x00, sizeof(heap_entry_t));

    new_heap->p_data = p_data;
    new_heap->size = size;
    new_heap->p_next = p_heap_list;

    p_heap_list = new_heap;

    return p_data;
}

void * heap_mngr_realloc(void * p_data, uint32_t new_size)
{
    heap_entry_t * current;

    for (current = p_heap_list; current != NULL; current = current->p_next)
    {
        if (current->p_data == p_data)
        {
            void * p_new = wmb_alloc(new_size);
            if (p_new == NULL) return NULL;

            uint32_t copy = (current->size < new_size) ? current->size : new_size;
            memcpy(p_new, p_data, copy);
            wmb_free(p_data);

            current->p_data = p_new;
            current->size   = new_size;
            return p_new;
        }
    }

    return NULL;
}

void heap_mngr_free(void * p_data)
{
    heap_entry_t * current;
    heap_entry_t * prev = NULL;

    current = p_heap_list;
    while (current)
    {
        if (current->p_data == p_data)
        {
            if (prev != NULL)
            {
                prev->p_next = current->p_next;
            }
            else
            {
                p_heap_list = current->p_next;
            }

            wmb_free(current->p_data);
            wmb_free(current);
            break;
        }

        prev = current;
        current = current->p_next;
    }
}

void heap_mngr_init(void)
{

}

void heap_mngr_deinit(void)
{
    heap_entry_t * current;
    heap_entry_t * next;

    current = p_heap_list;
    while (current)
    {
        next = current->p_next;
        wmb_free(current->p_data);
        wmb_free(current);
        current = next;
    }
    p_heap_list = NULL;
}

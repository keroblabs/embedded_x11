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
    void * p_data = malloc(size);
    if (p_data == NULL) return NULL;

    new_heap = (heap_entry_t *)malloc(sizeof(heap_entry_t));
    if (new_heap == NULL) 
    {
        free(p_data);
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
    void * p_new_data = NULL;

    current = p_heap_list;
    while (current)
    {
        if (current->p_data == p_data)
        {
            p_new_data = realloc(p_data, new_size);
            if (p_new_data != NULL)
            {
                current->p_data = p_new_data;
                current->size = new_size;
            }
            break;
        }

        current = current->p_next;
    }

    return p_new_data;
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

            free(current->p_data);
            free(current);
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
        free(current->p_data);
        free(current);
        current = next;
    }
    p_heap_list = NULL;
}

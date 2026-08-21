#include "lists.h"

#if defined(USE_FIBER)
#include "fiber.h"
#endif

//////////////////////////////////////////////////////////////////////////////////////////

#define LIST_FREE       MFREE

//////////////////////////////////////////////////////////////////////////////////////////

/* Same primitive as pipe.c and buffer.c: one global region rather than a
 * per-list mutex, so the list stays usable from a const pointer and from an
 * ISR-fed producer.  See 90-decisions.md D-47.4. */
static void list_lock(void)
{
    #if defined(USE_MULTITHREAD)
    DI();
    #elif defined(USE_FIBER)
    fiber_critical_enter();
    #endif
}

static void list_unlock(void)
{
    #if defined(USE_MULTITHREAD)
    EI();
    #elif defined(USE_FIBER)
    fiber_critical_exit();
    #endif
}

//////////////////////////////////////////////////////////////////////////////////////////

static list_node_t *find_prev_node(const list_data_t *list, const list_node_t *node)
{
    list_node_t *curr = list->head;

    while (curr != NULL)
    {
        if (curr->next == node)
        {
            return curr;
        }
        curr = curr->next;
    }
    return curr;
}

static bool_t list_unlink_internal(list_data_t *list, list_node_t *node)
{
    if (list->head == node)
    {
        list->head = node->next;
        if (list->head == NULL)
        {
            list->tail = NULL;
        }
    }
    else
    {
        list_node_t *prevnode = find_prev_node(list, node);
        if (prevnode == NULL)
        {
            return FALSE;
        }
        prevnode->next = node->next;
        if (prevnode->next == NULL)
        {
            list->tail = prevnode;
        }
    }

    node->next = NULL;
    list->count--;
    return TRUE;
}

/* Runs unlocked: the callback may allocate or reach another list, and a
 * critical section must not span either. */
static void list_delete_node(delete_cb_t delete_cb, list_node_t *node)
{
    if (node == NULL) return;

    if (delete_cb != NULL)
    {
        delete_cb(node);
    }

    LIST_FREE(node);
}

//////////////////////////////////////////////////////////////////////////////////////////

void list_init(list_data_t *list, delete_cb_t delete_cb)
{
    if (list == NULL) return;

    list->count     = 0;
    list->head      = NULL;
    list->tail      = NULL;
    list->delete_cb = delete_cb;
}

void list_clear(list_data_t *list)
{
    list_node_t *node;
    delete_cb_t  delete_cb;

    if (list == NULL) return;

    /* Detach the whole chain first, so the list is consistent and empty
     * while the nodes are released. */
    list_lock();

    node      = list->head;
    delete_cb = list->delete_cb;

    /* clear only removes nodes, maintain the callback function */
    list->count = 0;
    list->head  = NULL;
    list->tail  = NULL;

    list_unlock();

    while (node != NULL)
    {
        list_node_t *next = node->next;

        node->next = NULL;
        list_delete_node(delete_cb, node);

        node = next;
    }
}

void list_insert_head(list_data_t *list, list_node_t *node)
{
    if ((list == NULL) || (node == NULL)) return;

    list_lock();

    node->next = list->head;

    if (list->head == NULL)
    {
        list->tail = node;
    }
    list->head = node;
    list->count++;

    list_unlock();
}

void list_insert_tail(list_data_t *list, list_node_t *node)
{
    if ((list == NULL) || (node == NULL)) return;

    list_lock();

    node->next = NULL;

    if (list->head == NULL)
    {
        list->head = node;
    }
    else
    {
        list->tail->next = node;
    }

    list->tail = node;
    list->count++;

    list_unlock();
}

void list_insert_after(list_data_t *list, list_node_t *existingnode, list_node_t *node)
{
    if ((list == NULL) || (node == NULL)) return;

    if (existingnode == NULL)
    {
        list_insert_head(list, node);
        return;
    }

    list_lock();

    node->next = existingnode->next;
    existingnode->next = node;

    if (list->tail == existingnode)
    {
        list->tail = node;
    }
    list->count++;

    list_unlock();
}

bool_t list_unlink(list_data_t *list, list_node_t *node)
{
    bool_t result;

    if ((list == NULL) || (node == NULL)) return FALSE;

    list_lock();
    result = list_unlink_internal(list, node);
    list_unlock();

    return result;
}

bool_t list_remove(list_data_t *list, list_node_t *node)
{
    bool_t      result;
    delete_cb_t delete_cb;

    if ((list == NULL) || (node == NULL)) return FALSE;

    list_lock();
    result    = list_unlink_internal(list, node);
    delete_cb = list->delete_cb;
    list_unlock();

    /* A node the list does not hold is not the list's to free. */
    if (result == TRUE)
    {
        list_delete_node(delete_cb, node);
    }

    return result;
}

void list_remove_tail(list_data_t *list)
{
    list_node_t *node;
    delete_cb_t  delete_cb;

    if (list == NULL) return;

    list_lock();

    node      = list->tail;
    delete_cb = list->delete_cb;

    if (node != NULL)
    {
        (void)list_unlink_internal(list, node);
    }

    list_unlock();

    list_delete_node(delete_cb, node);
}

list_node_t *list_find(list_data_t *list, size_t offset, uint16_t size, const void *value)
{
    list_node_t *node;

    if ((list == NULL) || (value == NULL) || (size == 0)) return NULL;

    list_lock();

    node = list->head;
    while (node != NULL)
    {
        if (memcmp(value, ((const uint8_t *) node) + offset, size) == 0)
        {
            break;
        }
        node = node->next;
    }

    list_unlock();

    return node;
}

bool_t list_is_empty(const list_data_t * const list)
{
    bool_t empty;

    if (list == NULL) return TRUE;

    list_lock();
    empty = (list->head == NULL) ? TRUE : FALSE;
    list_unlock();

    return empty;
}

void * list_get_prev(const list_data_t * const list, list_node_t * curr)
{
    list_node_t *node;

    if ((list == NULL) || (curr == NULL)) return NULL;

    list_lock();
    node = find_prev_node(list, curr);
    list_unlock();

    return node;
}

void * list_get_head(const list_data_t * const list)
{
    list_node_t *node;

    if (list == NULL) return NULL;

    list_lock();
    node = list->head;
    list_unlock();

    return node;
}

void * list_get_tail(const list_data_t * const list)
{
    list_node_t *node;

    if (list == NULL) return NULL;

    list_lock();
    node = list->tail;
    list_unlock();

    return node;
}

void * list_get_next(const list_data_t * const list, list_node_t * node)
{
    list_node_t *next;

    if (list == NULL) return NULL;

    list_lock();
    next = (node == NULL) ? list->head : node->next;
    list_unlock();

    return next;
}

uint16_t list_size(const list_data_t * const list)
{
    uint16_t count;

    if (list == NULL) return 0;

    list_lock();
    count = list->count;
    list_unlock();

    return count;
}

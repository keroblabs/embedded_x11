#ifndef __LISTS_H__
#define __LISTS_H__

//////////////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include <stddef.h>

//////////////////////////////////////////////////////////////////////////////////////////

/* A list is a singly linked, counted collection of *owned* nodes.  Unlike
 * pipe.c the entries are caller-defined structures rather than byte buffers,
 * and unlike buffer.c an entry can be removed from the middle - which is what
 * an event queue needs to drop one record and keep the rest.
 *
 * Ownership:
 *   list_insert_head/tail/after  takes ownership; the node must come from
 *                                MMALLOC, because the list frees it
 *   list_remove, list_remove_tail
 *                                call delete_cb (if any) and free the node
 *   list_clear                   the same, for every node
 *   list_unlink                  gives ownership back; the node leaves with
 *                                next == NULL and the caller frees it
 *   list_find, list_get_*        borrow only; the list still owns the node
 *
 * delete_cb releases what the node *points to*; the list always frees the
 * node itself.  It runs outside the critical section, so it may allocate and
 * it may touch other lists.
 *
 * Mutual exclusion follows pipe.c: USE_FIBER selects
 * fiber_critical_enter/exit.  Every entry point is atomic on its own; a
 * compound operation (find, then remove what was found) is not, and the
 * caller must bracket it if another fiber can reach the same list.
 * See 90-decisions.md D-47.
 */

//////////////////////////////////////////////////////////////////////////////////////////

// MUST be the first element of a data structure
typedef struct _list_node_t
{
    struct _list_node_t *next;

} list_node_t;

/* First member of an entry structure:  typedef struct { LIST_HEADER; ... } entry_t; */
#define LIST_HEADER            list_node_t node

typedef void (*delete_cb_t)(list_node_t *node);

typedef struct _list_data_t
{
    uint16_t       count;
    list_node_t   *head;
    list_node_t   *tail;
    delete_cb_t    delete_cb;

} list_data_t;

//////////////////////////////////////////////////////////////////////////////////////////

#define list_find_value(list, type, typemember, size, value) (type *)list_find(list, offsetof(type, typemember), (uint16_t) size, value)

// example of search
// #define find_entry(list, value) list_find_value(list, <entry_struct_type>, <struct_member>, sizeof(<member_type>), (value))

/*
 static <entry_struct_type> *create_entry(list_data_t *list, <entry parameters to find and build>)
 {
 <entry_struct_type> *ret = find_entry(list, <searching parameter>);
 if (ret == NULL)
 {
 ret = (<entry_struct_type> *)MMALLOC(sizeof(<entry_struct_type>));
 }

 if (ret != NULL)
 {
 list_insert_tail(list, &ret->node);
 <initialise entry>
 }


 return ret;
 }
 */

//////////////////////////////////////////////////////////////////////////////////////////

void list_init(list_data_t *list, delete_cb_t delete_cb);
void list_clear(list_data_t *list);
void list_insert_head(list_data_t *list, list_node_t *node);
void list_insert_tail(list_data_t *list, list_node_t *node);

/* existingnode must already be in the list; NULL inserts at the head */
void list_insert_after(list_data_t *list, list_node_t *existingnode, list_node_t *node);
void list_remove_tail(list_data_t *list);

bool_t list_unlink(list_data_t *list, list_node_t *node);
bool_t list_remove(list_data_t *list, list_node_t *node);
bool_t list_is_empty(const list_data_t * const list);

list_node_t *list_find(list_data_t *list, size_t offset, uint16_t size, const void *value);

void * list_get_head(const list_data_t * const list);
void * list_get_tail(const list_data_t * const list);

/* node == NULL returns the head, so a walk can start from NULL */
void * list_get_next(const list_data_t * const list, list_node_t * node);
void * list_get_prev(const list_data_t * const list, list_node_t * curr);

uint16_t list_size(const list_data_t * const list);

//////////////////////////////////////////////////////////////////////////////////////////

#endif // __LISTS_H__

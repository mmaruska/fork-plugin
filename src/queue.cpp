// todo: use templates, not this Lisp-ism

#include "queue.h"
#include "debug.h"
#include <stddef.h>  /* something to define NULL */


//  fifo--------------------------------+
//   HEAD                             TAIL
//    V                                V
//   (cons   car  cdr) -> next ...... ->(car  cdr)-> NULL
//           |
//          carries a value


int
queue_lenght(const list_with_tail &queue)
{
    int len = 0;
    cons *iterator = queue.head;
    while (iterator) {
	len++;
	iterator = iterator->cdr;
    }
    return len;
}

Bool
queue_empty(const list_with_tail &queue)
{
    return (queue.head == NULL);
}

// fixme: name is kept.
void
init_queue(list_with_tail &queue, const char* name)
{
    queue.name = name;
    queue.head = queue.tail = NULL;
}

//  ref-cdr /nth-cdr
cons*
queue_skip(list_with_tail &queue, int n) // nth
{
    // N must be  < queue_lenght!!
    /* go  N ahead along the queue */
    cons* handle = queue.head;
    int i;
   
    for(i = 0; i< n; i++)
	{
	    // if (handle)
	    handle = handle->cdr;
	}
    return handle;
}

cons*
pop_from_queue(list_with_tail &queue, int empty_ok) // head
{
    cons* old;
   
    assert(empty_ok || queue.head);
   
    if (old = queue.head) {
	queue.head = old->cdr;
	if (queue.tail == old)
	    queue.tail = NULL;
	old->cdr = NULL;
    }
#if 0
    else
	// DB(("%s from an empty queue!\n", __FUNCTION__));
#endif      
	return old;
}


//  head +  tail
//  |        \
//  v         \
//  x -> .... last-> NULL

#define queue_name(queue) (queue.name?queue.name:"{null}")

// HANDLE
void
push_on_queue(list_with_tail &queue, cons *handle)
{
    assert(handle);
    assert(handle->car);
    assert(! handle->cdr);
   
    if (queue.head){ // already something there?
	DB(("%s(%s): putting at the end: %d\n", __FUNCTION__,
	    queue_name(queue),
	    detail_of(handle->car)));

	assert(! queue.tail->cdr);
	queue.tail->cdr = handle;
	queue.tail = handle;
      
	// DB(("%s: done\n", __FUNCTION__));
    } else {
	assert(! queue.tail);
      
	DB(("%s: putting at the HEAD (%s): %d\n", __FUNCTION__, queue_name(queue),detail_of(handle->car)));
	queue.head = queue.tail = handle;
	/* event */
    };
}

//  unused:
#if 0
void
append_to(list_with_tail &from, list_with_tail &to)
{
    assert (to.tail);

    to.tail->cdr = from.head;
    from.head = from.tail = NULL;
}
#endif


// slice
void
slice_queue(list_with_tail &from, list_with_tail &to)
{
    // if from is NULL, this fails!
    assert (from.tail);
   
    from.tail->cdr = to.head;
    to.head = from.head;
    // [11 Mar 05]  ... I saw similar code in sawfish/src/pixmap-cache.c
    if (!to.tail)
	to.tail = from.tail;
    from.head = from.tail = NULL;
}

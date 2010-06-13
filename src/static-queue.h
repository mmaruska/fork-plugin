#ifdef  STATIC_QUEUE
/* event_queue
 * we keep all the events, before we decide the keycode. how many ?
 * After we start suspecting (delaying the decision), we might release
 * all keys (modifiers, but also letters) pressed/down previously,

 * but then, if we press-and-release any key, we either decide:
 *   + by _time_ for the modifier,
 *   + or on the release
 *
 * so we need (# of all key )* 2   (dirichlet p.)... we can release all,
 * and press all.
 *
 * fixme: ^ false !!!
 */

/* fixme: if we press, and start verifying the 1st one, and then release
 * all the others ??
 *
 * we should not depend on the fixed verifying..... But:
 *
 * fixme:  __actually__   we trigger the fork after we press the 3rd key.
 *
 * this could be problematic, if we press  key, and modifier + another letter
 * (this 2-composition is fast).
 *      so far, never occured/tested.
 */

#define MAX_EVENTS_IN_QUEUE  256

typedef struct {
   unsigned char head, tail;
   InternalEvent events[MAX_EVENTS_IN_QUEUE]; /* fixme: bug! */
} event_queue;

#endif	/* STATIC_QUEUE */


#ifdef STATIC_QUEUE
   event_queue*  queue; /* i need a pointer, b/c i will switch it, to push everything.
                         * fixme: no, i don't switch it. B/c  the other queue (of to-be-replayed events) is sourced from this one, it is
                         * of limited lenght.  so, instead of mallocing them, we just prepend. */
#endif


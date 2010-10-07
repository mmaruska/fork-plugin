#ifndef _QUEUE_H_
#define	_QUEUE_H_ 1

#define xorg 1

extern "C" {

// #define XKB_IN_SERVER 1
#include <X11/X.h>
#include <X11/Xproto.h>
#include <X11/Xdefs.h>

#include <assert.h>
#include <stdint.h>
#include <xorg/input.h>
#include <xorg/eventstr.h>
}


typedef struct {
        InternalEvent* car;
        KeyCode forked; /* if forked to (another keycode), this is the original key */
} key_event;

queue<key_event> queue;



/* I could make a template. For now I do not! */
typedef struct cons cons;
struct cons
{
   cons* cdr;
#if KEEP_PREVIOUS 
   cons* previous;            /* hm ??? */
#endif
   InternalEvent* car;
   KeyCode forked;              /* if forked to (another keycode), this is the original key */
};



typedef struct {
#if 1                           /* DEBUG    */
   const char* name;
#endif   
   cons *head, *tail;
} list_with_tail;



extern int queue_lenght(const list_with_tail &queue); 
extern Bool queue_empty(const list_with_tail &queue) ;

extern cons* pop_from_queue(list_with_tail &queue, int empty_ok);
extern void  push_on_queue(list_with_tail &queue, cons *handle);
extern cons* queue_skip(list_with_tail &queue, int n);

/* move the content of FROM to the beginning of TO (hence make FROM empty) */
/*      from      to          to = result    from -> ()  
 *     xxxxxxx   yyyyy   ->   xxxxyyyy
 */
extern void  slice_queue(list_with_tail &from, list_with_tail &to);

extern void init_queue(list_with_tail &queue, const char* name);

#endif   

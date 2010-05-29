#ifndef _FORK_H_
#define _FORK_H_


#define MMC_PIPELINE 1


extern "C" {
/* #define MMC_PIPELINE 1 */

/* #include <stdio.h> */
/* #include <math.h> */


#include <X11/X.h>
#include <X11/Xproto.h>
#include <X11/keysym.h>


/* sdk: */
/* this is needed to typedef Bool, used in  X11/extensions/XKBstr.h */
#include <xorg/inputstr.h>


/* `probably' i use it only to print out the keysym in debugging stuff*/

   /* `configuration' .... processing requests: */
};

#include "config.h"

/* copied from xkb.c  fixme! */
#define	CHK_DEVICE(d,sp,lf) {\
    int why;\
    d = (DeviceIntPtr)lf((sp),&why);\
    if  (!dev) {\
	client->errorValue = _XkbErrCode2(why,(sp));\
	return XkbKeyboardErrorCode;\
    }\
}
#define	CHK_KBD_DEVICE(d,sp) 	CHK_DEVICE(d,sp,_XkbLookupKeyboard)


#if 0
#define device_machine(d) (machineRec*)(d->plugin->data)
#else
#define plugin_machine(p) ((machineRec*)(plugin->data))
#endif

//  #define	_XkbTypedCalloc(n,t)	((t *)Xcalloc((n)*sizeof(t)))
#define MALLOC(type)   (type *) malloc(sizeof (type))

#define MAX_KEYCODE  256   


/* we can have a (linked) list of configs! */
typedef struct fork_configuration fork_configuration_rec; 

struct fork_configuration
{
   /* static data of the machine: i.e.  `configuration' */

   /* 256 max keycode */
   KeyCode          fork_keycode[MAX_KEYCODE]; /* value for new forks. */

#if 0
   KeyCode          forkCancel[MAX_KEYCODE]; /* i -> j     j suppresses forking of i */
   KeyCode          forkClass[MAX_KEYCODE]; /*  supressing,  */
   struct {
      unsigned char   count;
      KeyCode* start;
   } fork_supress[MAX_KEYCODE];   /* max keycode! Keycode */

#endif


   Bool          fork_repeatable[MAX_KEYCODE]; /* True -> if repeat, cancel possible fork. */

   /* global, not per-key: */
   int overlap_tolerance;       /* we don't consider an overlap, until this ms. fixme: we need better. a ration between `before'/`overlap'/`after' */
   int overlap_tolerance_per_key[MAX_KEYCODE][MAX_KEYCODE]; /*  0 0 -> overlap_tolerance */


   int verification_interval;   /* after how many m-secs, we decide for the modifier.... should be around the key-repeatition rate (1st pause) */
   int verification_interval_per_key[MAX_KEYCODE][MAX_KEYCODE]; /*  0,0 = verification_interval */

   int clear_interval;
   int repeat_max;
   Bool consider_forks_for_repeat;
   int debug;                   /* was Bool before. */


   const char*  name;
   int id;
   fork_configuration_rec*   next;
};



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

#endif



/* this is the alternative: */
#include "queue.h"
#include "fork_requests.h"




/* states of the automaton: */
typedef enum {normal, suspect, verify, deactivated, activated} state_type; 


/* `machine': the dynamic `state' */

typedef struct machine
{
   volatile int lock;           /* the mouse interrupt handler should ..... err!  `volatile'
				 * useless mmc!  But i want to avoid any caching it.... SMP ??*/

   Bool timer_used; /*  :1 */
   
   unsigned char state;
   Time suspect_time;           /* time of the 1st event in the queue. */
   
   KeyCode verificator;         /*  */
   Time verificator_time;       /* press of the `verificator' */

   /* signed! */
   int time_left;

/* we cannot hold only a Bool, since when we have to reconfigure, we need the original forked keycode for the release event. */
   KeyCode          forkActive[MAX_KEYCODE];   /* max keycode! Keycode */

#ifdef STATIC_QUEUE
   event_queue*  queue; /* i need a pointer, b/c i will switch it, to push everything.
                         * fixme: no, i don't switch it. B/c  the other queue (of to-be-replayed events) is sourced from this one, it is
                         * of limited lenght.  so, instead of mallocing them, we just prepend. */
#endif


   /* fixme: i should start using OO for these queues */

   list_with_tail internal_queue; /* still undecided. these events alone don't decide the 1st
                                   * on the queue.*/
   list_with_tail input_queue;  /* not yet processed, b/c we wait for external events to resume
                                 * processing?*/
   list_with_tail output_queue; /* we have decided, but externals don't accept, so we keep them. */

#if KEEP_PREVIOUS 
   cons* previous_event;
#else
   Time time_of_last_output;
#endif

#if STATIC_LAST  
   archived_event *last_events;
   int last_head;
#else 
   list_with_tail last_events;
   int last_events_count;
#endif
   int last_events_count;
   int max_last;

   fork_configuration_rec  *config;

} machineRec;






extern fork_configuration_rec* machine_new_config(void);
extern void machine_switch_config(plugin_instance* plugin, machineRec* machine,int id);
extern int machine_set_last_events_count(machineRec* machine, int new_max);
extern void replay_events(plugin_instance* plugin, Time current_time, Bool force); 

extern int dump_last_events_to_client(plugin_instance* plugin, ClientPtr client, int n);


enum {
#if 0
   SHIFT_KEYCODE = 37,
   A_KEYCODE = 38,
   S_KEYCODE = 39,
   D_KEYCODE = 40,
   K_KEYCODE = 45,
   G_KEYCODE = 42,
   F_KEYCODE = 41,
   GROUP_2_KEYCODE = 66,
   HYPER_KEYCODE = 94,
   ALT_KEYCODE = 50,
#endif
   PAUSE_KEYCODE = 110
   //   #define KEY_Pause        /* Pause                 0x66  */  102
};



#if 0
/* see /p/xfree-4.3.99.901/work/xc/include/extensions/XKBsrv.h for the structure  XkbSrvInfoRec/*XkbSrvInfoPtr,  which contains the configuration
 * + the machine + the extended 'state' (what is currently forked, and how). That structure is per keyboard ?
 * */

#endif



extern int memory_balance;

inline 
void* mmalloc(int size)
{
   void* p = xalloc(size);
   if (p) {
      memory_balance += size;
      if (memory_balance > sizeof(machineRec) + sizeof(plugin_instance) + 2000) /* machine->max_last * sizeof() */
         ErrorF("%s: memory_balance = %d\n", __FUNCTION__, memory_balance);
   }
   return p;
}

inline
void
mxfree(void* p, int size)
{
   memory_balance -= size;
   xfree(p);
}

#endif	/* _FORK_H_ */

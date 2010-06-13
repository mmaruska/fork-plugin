#ifndef _FORK_H_
#define _FORK_H_


#define MMC_PIPELINE 1


extern "C" {
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

#define plugin_machine(p) ((machineRec*)(plugin->data))

#define MALLOC(type)   (type *) malloc(sizeof (type))
#define MAX_KEYCODE  256   


/* we can have a (linked) list of configs! */
typedef struct _fork_configuration fork_configuration; 

struct _fork_configuration
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
    int overlap_tolerance;       /* we don't consider an overlap, until this ms.
				    fixme: we need better. a ration between `before'/`overlap'/`after' */
    int overlap_tolerance_per_key[MAX_KEYCODE][MAX_KEYCODE]; /*  0 0 -> overlap_tolerance */


    int verification_interval;   /* after how many m-secs, we decide for the modifier.
				    Should be around the key-repeatition rate (1st pause) */
    int verification_interval_per_key[MAX_KEYCODE][MAX_KEYCODE]; /*  0,0 = verification_interval */

    int clear_interval;
    int repeat_max;
    Bool consider_forks_for_repeat;
    int debug;

    const char*  name;
    int id;
    fork_configuration*   next;
};


/* this is the alternative to static-queue.h */
#include "queue.h"
#include "fork_requests.h"



/* states of the automaton: */
typedef enum {
    normal,
    suspect,
    verify,
    deactivated,
    activated
} state_type;


/* `machine': the dynamic `state' */

typedef struct machine
{
    volatile int lock;           /* the mouse interrupt handler should ..... err!  `volatile'
				  * useless mmc!  But i want to avoid any caching it.... SMP ??*/
    unsigned char state;
    Time suspect_time;           /* time of the 1st event in the queue. */
   
    KeyCode verificator;
    Time verificator_time;       /* press of the `verificator' */

    int time_left;		/* signed! Time to wait since the last event to the moment
				   when the current event queue could decide more*/

    /* we cannot hold only a Bool, since when we have to reconfigure, we need the original
       forked keycode for the release event. */
    KeyCode          forkActive[MAX_KEYCODE];

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
#endif
    int last_events_count;
    int max_last;

    fork_configuration  *config;
} machineRec;



extern fork_configuration* machine_new_config(void);
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



// I want to track the memory usage, and warn when it's too high.
extern int memory_balance;

inline 
void* mmalloc(int size)
{
   void* p = xalloc(size);
   if (p) {
      memory_balance += size;
      if (memory_balance > sizeof(machineRec) + sizeof(plugin_instance) + 2000)
	/* machine->max_last * sizeof() */
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

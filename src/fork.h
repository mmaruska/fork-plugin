#ifndef _FORK_H_
#define _FORK_H_


#include <xorg-server.h>
#ifndef MMC_PIPELINE
#error "This is useful only when the xorg-server is configured with --enable-pipeline"
#endif


extern "C" {
#include <X11/X.h>
#include <X11/Xproto.h>
#include <X11/keysym.h>
#include <xorg/inputstr.h>
};

#include "config.h"
#include "queue.h"
#include "fork_requests.h"

using namespace std;
using namespace __gnu_cxx;

typedef struct {
  InternalEvent* event;
  KeyCode forked; /* if forked to (another keycode), this is the original key */
} key_event;

typedef my_queue<key_event> list_with_tail;


#define plugin_machine(p) ((machineRec*)(plugin->data))
#define MALLOC(type)   (type *) malloc(sizeof (type))
#define MAX_KEYCODE 256   	/* fixme: inherit from xorg! */
typedef int keycode_parameter_matrix[MAX_KEYCODE][MAX_KEYCODE];



/* we can have a (linked) list of configs! */
typedef struct _fork_configuration fork_configuration; 


struct _fork_configuration
{
  /* static data of the machine: i.e.  `configuration' */

  KeyCode          fork_keycode[MAX_KEYCODE];
  Bool          fork_repeatable[MAX_KEYCODE]; /* True -> if repeat, cancel possible fork. */

  /* we don't consider an overlap, until this ms.
     fixme: we need better. a ration between `before'/`overlap'/`after' */
  keycode_parameter_matrix overlap_tolerance; 

  /* after how many m-secs, we decide for the modifier.
     Should be around the key-repeatition rate (1st pause) */
  keycode_parameter_matrix verification_interval;

  int clear_interval;
  int repeat_max;
  Bool consider_forks_for_repeat;
  int debug;

  const char*  name;
  int id;
  fork_configuration*   next;
};



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


  list_with_tail internal_queue;
  /* Still undecided events: these events alone don't decide what event is the 1st on the
     queue.*/
  list_with_tail input_queue;  /* Not yet processed at all. Since we wait for external
                                * events to resume processing (Grab is active-frozen) */
  list_with_tail output_queue; /* We have decided, but externals don't accept, so we keep them. */

#if KEEP_PREVIOUS 
  key_event* previous_event;
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
extern void machine_switch_config(PluginInstance* plugin, machineRec* machine,int id);
extern int machine_set_last_events_count(machineRec* machine, int new_max);
extern void replay_events(PluginInstance* plugin, Time current_time, Bool force); 

extern int dump_last_events_to_client(PluginInstance* plugin, ClientPtr client, int n);


enum {
  PAUSE_KEYCODE = 127
};



// I want to track the memory usage, and warn when it's too high.
extern size_t memory_balance;

inline 
void* mmalloc(size_t size)
{
  void* p = malloc(size);
  if (p)
    {
      memory_balance += size;
      if (memory_balance > sizeof(machineRec) + sizeof(PluginInstance) + 2000)
        /* machine->max_last * sizeof() */
        ErrorF("%s: memory_balance = %ld\n", __FUNCTION__, memory_balance);
    }
  return p;
}

inline
void
mxfree(void* p, size_t size)
{
  memory_balance -= size;
  free(p);
}

#endif	/* _FORK_H_ */

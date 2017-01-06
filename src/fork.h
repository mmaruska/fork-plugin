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


#include <X11/Xdefs.h>
#include <stdint.h>
#include <xorg/input.h>
#include <xorg/eventstr.h>

// include/os.h
#undef xalloc

// in C++ it conflicts! (/usr/include/xorg/misc.h vs alorithm)
#undef max
#undef min
};

#include "config.h"

#include "debug.h"
#include "queue.h"

#include "fork_requests.h"
#include "history.h"

using namespace std;
using namespace __gnu_cxx;


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
  st_normal,
  st_suspect,
  st_verify,
  st_deactivated,
  st_activated
} state_type;


/* `machine': the dynamic `state' */

typedef struct machine
{
    volatile int lock;           /* the mouse interrupt handler should ..... err!  `volatile'
                                  * useless mmc!  But i want to avoid any caching it.... SMP ??*/
    unsigned char state;

    /* To allow AR for forkable keys:
     * When we press a key the second time in a row, we might avoid forking:
     * So, this is for the detector:
     *
     * This means I cannot do this trick w/ 2 keys, only 1 is the last/considered! */
    KeyCode last_released; // .- trick
    int last_released_time;

    KeyCode suspect;
    KeyCode verificator;

    // these are "registers"
    Time suspect_time;           /* time of the 1st event in the queue. */
    Time verificator_time;       /* press of the `verificator' */
    // calculated:
    Time decision_time;		/* Time to wait... so that the current event queue could decide more*/
    Time current_time;

    /* we cannot hold only a Bool, since when we have to reconfigure, we need the original
       forked keycode for the release event. */
    KeyCode          forkActive[MAX_KEYCODE];



    list_with_tail internal_queue;
    /* Still undecided events: these events alone don't decide what event is the 1st on the
       queue.*/
    list_with_tail input_queue;  /* Not yet processed at all. Since we wait for external
                                  * events to resume processing (Grab is active-frozen) */
    list_with_tail output_queue; /* We have decided, but externals don't accept, so we keep them. */

    last_events_type *last_events; // history
    int max_last;

    fork_configuration  *config;
} machineRec;



extern fork_configuration* machine_new_config(void);
extern void machine_switch_config(PluginInstance* plugin, machineRec* machine,int id);
extern int machine_set_last_events_count(machineRec* machine, int new_max);
extern void replay_events(PluginInstance* plugin, Bool force);

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

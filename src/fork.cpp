/*
 * Copyright (C) 2003-2005-2010 Michal Maruska <mmaruska@gmail.com>
 * License:  Creative Commons Attribution-ShareAlike 3.0 Unported License
 */

#define USE_LOCKING 1

/* what does the lock protect?  ... access to the  queues,state
 *  mouse signal handler cannot just make "fork", while a key event is being analyzed.
 */

#if USE_LOCKING
#define CHECK_LOCKED(m) assert(m->lock)
#define CHECK_UNLOCKED(m) assert(m->lock == 0)

// might be:        (CHECK_UNLOCKED(m),m->lock=1)
#define LOCK(m)  m->lock=1
#define UNLOCK(m)  m->lock=0
#else
#error "define locking macros!"
#endif


/* Locking is broken: but it's not used now:
 *
 *   ---> keyevent ->  xkb action -> mouse
 *                                     |
 *   prev   <----                 <---  thaw
 *        ->   process  \
 *               exits  /
 *             unlocks!
 *
 *
 *  lock is gone!! <- action */



/* This is how it works:
 * We have a `state' and 3 queues:
 *
 *  output Q  |   internal Q    | input Q
 *  waits for |
 *  thaw         Oxxxxxx        |  yyyyy
 *               ^ forked?
 *
 * We push at the end of input Q.  Then we pop from that Q and push on
 * Internal when we determine for 1 event, if forked/non-forked.
 *
 * Then we push on the output Q. At that moment, we also restart: all
 * from internal Q is returned/prepended to the input Q.
 */


#include "config.h"
#include "debug.h"

#include "configure.h"
#include "history.h"
#include "fork.h"


extern "C" {
/* I use it only to print out the keysym in debugging stuff*/
#include <xorg/xkbsrv.h>
}

/*  Functions on xEvent */
#include "event_ops.h"


/* fork_reason */
enum {
  reason_total,
  reason_overlap,
  reason_force
};

/* used only for debugging */
char const *state_description[]={
  "normal",
  "suspect",
  "verify",
  "deactivated",
  "activated"
};

const char* event_names[] = {
  "KeyPress",
  "KeyRelease",
  "ButtonPress",
  "ButtonRelease",
  "Motion",
  "Enter",
  "Leave",
  // 9
  "FocusIn",
  "FocusOut",
  "ProximityIn",
  "ProximityOut",
  // 13
  "DeviceChanged",
  "Hierarchy",
  "DGAEvent",
  // 16
  "RawKeyPress",
  "RawKeyRelease",
  "RawButtonPress",
  "RawButtonRelease",
  "RawMotion",
  "XQuartz"
};


/* memory problems:
 *     machine->previous_event
 */
size_t memory_balance = 0;



inline Bool
forkable_p(fork_configuration* config, KeyCode code)
{
   return (config->fork_keycode[code]);
}


const int BufferLength = 200;

/* the returned string is in static space. Don't free it! */
static const char*
describe_key(DeviceIntPtr keybd, InternalEvent *event)
{
    assert (event);

    static char buffer[BufferLength];
    XkbSrvInfoPtr xkbi= keybd->key->xkbInfo;
    KeyCode key = detail_of(event);
    // assert(0 <= key <= (max_key_code - min_key_code));
    char* keycode_name = xkbi->desc->names->keys[key].name;

    const KeySym *sym= XkbKeySymsPtr(xkbi->desc,key);
    if ((!sym) || (! isalpha(*(unsigned char*)sym)))
	sym = (KeySym*) " ";

    snprintf(buffer, BufferLength, "(%s) %d %4.4s -> %c %s (%u)",
	     keybd->name,
	     key, keycode_name,(char)*sym,
	     event_type_brief(event),(unsigned int)time_of(event));

    return buffer;
}


/* the returned string is in static space. don't free it! */
static const char*
describe_machine_state(machineRec* machine)
{
   static char buffer[BufferLength];

   snprintf(buffer, BufferLength, "%s[%dm%s%s",
            escape_sequence, 32 + machine->state,
            state_description[machine->state], color_reset);
   return buffer;
}


/* Push the event to the next plugin. Ownership is handed over! */
inline void
hand_over_event_to_next_plugin(InternalEvent *event, PluginInstance* plugin)
{
   PluginInstance* next = plugin->next;

#if DEBUG
   if (((machineRec*) plugin_machine(plugin))->config->debug)
      {
         DeviceIntPtr keybd = plugin->device;
         DB(("%s<<<", keysym_color));
         DB(("%s", describe_key(keybd, event)));
         DB(("%s\n", color_reset));
      }
#endif
   assert (!plugin_frozen(next));
   memory_balance -= event->any.length;
   PluginClass(next)->ProcessEvent(next, event, TRUE); // we always own the event (up to now)
}


/* The machine is locked here:
 * Push as many as possible from the OUTPUT queue to the next layer */
static void
try_to_output(PluginInstance* plugin)
{
  machineRec* machine = plugin_machine(plugin);

  CHECK_LOCKED(machine);

  list_with_tail &queue = machine->output_queue;
  PluginInstance* next = plugin->next;

  MDB(("%s: Queues: output: %d\t internal: %d\t input: %d \n", __FUNCTION__,
       queue.length (),
       machine->internal_queue.length (),
       machine->input_queue.length ()));

  while ((!plugin_frozen(next)) && (!queue.empty ())) {
      key_event* ev = queue.pop();

      machine->last_events->push_back(make_archived_events(ev));
      InternalEvent* event = ev->event;
      mxfree(ev, sizeof(key_event));

      UNLOCK(machine);
      hand_over_event_to_next_plugin(event, plugin);
      LOCK(machine);
  };
  if (!plugin_frozen(next))
  {
      // todo: we should push the time!
      Time now;
      if (!queue.empty())
      {
          now = time_of(queue.front()->event);
      } else if (machine->internal_queue.empty())
      {
          now = time_of(machine->internal_queue.front()->event);
      }
      else if (machine->input_queue.empty())
      {
          now = time_of(machine->input_queue.front()->event);
      } else {
          now = 0;
      }

      if (now)
          PluginClass(plugin->next)->ProcessTime(plugin->next, now);

      if (!queue.empty ())
          MDB(("%s: still %d events to output\n", __FUNCTION__, queue.length ()));
  }
}

// Another event has been determined. So:
// todo:  possible emit a (notification) event immediately,
// ... and push the event down the pipeline, when not frozen.
static void
output_event(key_event* handle, PluginInstance* plugin)
{
   assert(handle->event);
   //assert(! handle->cdr);

   InternalEvent *event = handle->event;
   machineRec* machine = plugin_machine(plugin);

   machine->time_of_last_output = time_of(event);
   machine->output_queue.push(handle);
   try_to_output(plugin);
};


/* event* we copy a pointer !!!  */
#define EMIT_EVENT(ev) {output_event(ev, plugin);}

/**
 * Operations on the machine
 * fixme: should it include the `self_forked' keys ?
 * `self_forked' means, that i decided to NOT fork. to mark this decision
 * (for when a repeated event arrives), i fork it to its own keycode
 */




/* The Static state = configuration.
 * This is the matrix with some Time values:
 * using the fact, that valid KeyCodes are non zero, we use
 * the 0 column for `code's global values

 * Global      xxxxxxxx unused xxxxxx
 * key-wise   per-pair per-pair ....
 * key-wise   per-pair per-pair ....
 * ....
*/

inline Time
get_value_from_matrix (keycode_parameter_matrix matrix, KeyCode code, KeyCode verificator)
{
  return (matrix[code][verificator]?
          matrix[code][verificator]:
          (matrix[code][0]?
           matrix[code][0]: matrix[0][0]));
}


inline Time
verification_interval_of(fork_configuration* config,
                         KeyCode code, KeyCode verificator)
{
  return get_value_from_matrix (config->verification_interval, code, verificator);
}


inline Time
overlap_tolerance_of(fork_configuration* config, KeyCode code, KeyCode verificator)
{
  return get_value_from_matrix (config->overlap_tolerance, code, verificator);
}


/* now the operations on the Dynamic state */

/* Return the keycode into which CODE has forked _last_ time.
   Returns code itself, if not forked. */
inline Bool
key_forked(machineRec *machine, KeyCode code)
{
  return (machine->forkActive[code]);
}


inline void
change_state(machineRec* machine, state_type new_state)
{
  machine->state = new_state;
  MDB((" --->%s[%dm%s%s\n", escape_sequence, 32 + new_state,
       state_description[new_state], color_reset));
}


/* Fork the 1st element on the internal_queue. Remove it from the queue
 * and push to the output_queue.
 *
 * todo: Should I have a link back from machine to the plugin? Here useful!
 * todo:  do away with the `forked_key' argument --- it's useless!
 */
inline void
activate_fork(machineRec *machine, PluginInstance* plugin)
{
    list_with_tail &queue = machine->internal_queue;
    assert(!queue.empty());

    key_event* ev = queue.pop();

    KeyCode forked_key = detail_of(ev->event);

    /* change the keycode, but remember the original: */
    ev->forked =  forked_key;
    machine->forkActive[forked_key] =
        ev->event->device_event.detail.key = machine->config->fork_keycode[forked_key];

    change_state(machine, st_activated);
    EMIT_EVENT(ev);

    MDB(("%s suspected: %d-> forked to: %d,  internal queue is long: %d, %s\n", __FUNCTION__,
         forked_key,
         machine->config->fork_keycode[forked_key],
         machine->internal_queue.length (),
         describe_machine_state(machine)));
}


/*
 * Called by mouse button press processing.
 * Make all the forkable (pressed)  forked! (i.e. confirm them all)
 *
 * If in Suspect or Verify state, force the fork. (todo: should be configurable)
 */
static void
step_fork_automaton_by_force(machineRec *machine, PluginInstance* plugin)
{
   if (machine->state == st_normal) {
      return;
   }
   if (machine->state == st_deactivated) {
      ErrorF("%s: BUG.\n", __FUNCTION__);
      return;
   }

   if (machine->internal_queue.empty())
      return;

   /* so, the state is one of: verify, suspect or activated. */
   list_with_tail& queue = machine->internal_queue;

   MDB(("%s%s%s state: %s, queue: %d .... FORCE\n",
        fork_color, __FUNCTION__, color_reset,
        describe_machine_state(machine),
        queue.length ()));

   machine->time_left = 0;
   activate_fork(machine, plugin);
}

static void
do_enqueue_event(machineRec *machine, key_event *ev)
{
    machine->internal_queue.push(ev);
    MDB(("enqueue_event: time left: %d\n", machine->time_left));
}

// so the ev proves, that the current event is not forked.
static void
do_confirm_non_fork(machineRec *machine, key_event *ev, PluginInstance* plugin)
{
   assert(machine->time_left == 0);
   change_state(machine, st_deactivated);
   machine->internal_queue.push(ev); //  this  will be re-processed!!


   key_event* non_forked_event = machine->internal_queue.pop();
   MDB(("this is not a fork! %d\n", detail_of(non_forked_event->event)));
   EMIT_EVENT(non_forked_event);
}

// so EV confirms fork of the current event.
static void
do_confirm_fork(machineRec *machine, key_event *ev, PluginInstance* plugin)
{
   if (machine->time_left < 0)
       machine->time_left = 0;

   /* fixme: ev is the just-read event. But that is surely not the head
      of queue (which is confirmed to fork) */
   DB(("confirm:\n"));
   activate_fork(machine, plugin);
   machine->internal_queue.push(ev);
}

/*
  returns:
  state  (in the `machine')
  output_event   possibly, othewise 0
  time_left   ... for another timer.
*/

#define time_difference_less(start,end,difference)   (end < (start + difference))
#define time_difference_more(start,end,difference)   (end > (start + difference))

static void
step_fork_automaton_by_time(machineRec *machine, PluginInstance* plugin, Time current_time)
{
  if ((machine->state == st_normal) || (machine->state == st_deactivated)) {
    DB(("%s: unexpected: %s!", __FUNCTION__, describe_machine_state(machine)));
    return;
  };

  list_with_tail &queue = machine->internal_queue;
  if (queue.empty()){
    DB(("%s: the internal queue is empty. Most likely the last event caused freeze,"
        "and replay_events didn't do any work\thowever, this timer is BUG\n", __FUNCTION__));
    /* fixme! now what?  */
  }

  // confirm fork:
  int reason;
  KeyCode suspected = detail_of(queue.front()->event);

  MDB(("%s%s%s state: %s, queue: %d, time: %u key: %d\n",
       fork_color, __FUNCTION__, color_reset,
       describe_machine_state (machine),
       queue.length (), (int)current_time, suspected));

  /* First, i try the simple (fork-by-one-keys).
   * If that works, -> fork! Otherwise, i try w/ 2-key forking, overlapping.
   */
  int verification_interval = verification_interval_of(machine->config, suspected,
                                                       machine->verificator);
  // fixme the  follower,
  // not verificator, but for now
  machine->time_left = verification_interval - (current_time - machine->suspect_time);
  MDB(("time: verification_interval = %dms elapsed so far =%dms  ->", verification_interval,
       (int)(current_time - machine->suspect_time)));

  if (machine->time_left <= 0){
    /* time_difference_more(machine->suspect_time, time_of(event),
     * verification_interval)) */
    reason = reason_total;
    goto confirm_fork;
  };

  /* To test 2 keys overlap, we need the 2nd key: a verificator! */
  if (machine->state == st_verify) {
    // verify overlap
    int overlap_tolerance = overlap_tolerance_of(machine->config, suspected,
                                                 machine->verificator);
    int another_time_left =  overlap_tolerance -  (current_time - machine->verificator_time);

    if (another_time_left < machine->time_left)
      machine->time_left = another_time_left;

    MDB(("time: overlay interval = %dms elapsed so far =%dms  ->", overlap_tolerance,
         (int) (current_time - machine->verificator_time)));
    if (machine->time_left <= 0) {
      /* verification_interval)  time_difference_more(machine->verificator_time, current_time,
         overlap_tolerance)*/
      reason = reason_overlap;
      goto confirm_fork;
    }
    // so, now we are surely in the replay_mode. All we need is to
    // get an estimate on time still needed:
  }


  /* So, we were woken too early. */
  assert (machine->time_left > 0);
  /* MDB */
  DB(("*** %s: returning with some more time-to-wait: %d (prematurely waken)\n", __FUNCTION__,
      machine->time_left));
  return;

 confirm_fork:
  machine->time_left = 0;
  activate_fork(machine, plugin);
  return;
}


inline Time
time_of_previous_event(machineRec *machine, key_event *ev)
{
    // fixme: look at the history
    return machine->time_of_last_output;
}


#define MOUSE_EMULATION_ON(xkb) (xkb->ctrls->enabled_ctrls & XkbMouseKeysMask)


/** apply_event_to_{STATE} */

static void
apply_event_to_normal(machineRec *machine, key_event *ev, PluginInstance* plugin)
{
    DeviceIntPtr keybd = plugin->device;
    XkbSrvInfoPtr xkbi= keybd->key->xkbInfo;

    InternalEvent* event = ev->event;
    KeyCode key = detail_of(event);
    Time simulated_time = time_of(event);

    fork_configuration* config = machine->config;
    XkbDescPtr xkb = xkbi->desc;

    assert(machine->internal_queue.empty());

    // if this key might start a fork....
    if (press_p(event) && (forkable_p(config, key))
#define CLEAR_INTERVAL 0
#if CLEAR_INTERVAL
        // this is not used!
	// fixme:  the clear interval should just hint, not preclude!
	&& !time_difference_less(
	    time_of_previous_event(machine, ev),
	    time_of(event),
	    config->clear_interval)
#endif
        /* fixme: is this w/ 1-event precision? (i.e. is the xkb-> updated synchronously) */
        /* todo:  does it have a mouse-related action? */
	&& !(MOUSE_EMULATION_ON(xkb)))
    {
        /* Either suspect, or detect .- trick to suppress fork */

        /* .- trick: by depressing/re-pressing the key rapidly, fork is disabled,
         * and AR is invoked */
#if DEBUG
        if ( !key_forked(machine, key) && (machine->last_released == key ))
        {
            MDB (("can we invoke autorepeat? %d  upper bound %d ms\n",
                  (int)(simulated_time - machine->last_released_time), config->repeat_max));
        }
#endif
        /* So, unless we see the .- trick, we do suspect: */
        if (!key_forked(machine, key) &&
            ((machine->last_released != key ) ||
            /*todo: time_difference_more(machine->last_released_time,simulated_time, config->repeat_max) */
            (int)(simulated_time - machine->last_released_time) > config->repeat_max))
        {
             change_state(machine, st_suspect);
             machine->suspect_time = time_of(event);
             machine->time_left = verification_interval_of(machine->config, key, 0);
             do_enqueue_event(machine, ev);
             return;
        } else {
        // .- trick: (fixme: or self-forked)
        MDB(("re-pressed very quickly\n"));
        machine->forkActive[key] = key; // fixme: why??
        EMIT_EVENT(ev);
        return;
        };
    }
    else if (release_p(event) && (key_forked(machine, key)))
    {
	MDB(("releasing forked key\n"));
	// fixme:  we should see if the fork was `used'.
	if (config->consider_forks_for_repeat){
            // C-f   f long becomes fork. now we wanted to repeat it....
            machine->last_released = detail_of(event);
            machine->last_released_time = time_of(event);
        } else {
            // imagine mouse-button during the short 1st press. Then
            // the 2nd press ..... should not relate the the 1st one.
            machine->last_released = 0;
            machine->last_released_time = 0;
        }
	/* we finally release a (self-)forked key. Rewrite back the keycode.
	 *
	 * fixme: do i do this in other machine states?
	 */
	event->device_event.detail.key = machine->forkActive[key];

	// this is the state (of the keyboard, not the machine).... better to
	// say of the machine!!!
	machine->forkActive[key] = 0;
	EMIT_EVENT(ev);
    }
    else
    {
	if (release_p (event))
	{
           machine->last_released = detail_of(event);
           machine->last_released_time = time_of(event);
        }
#if CLEAR_INTERVAL
	if (time_difference_less(time_of_previous_event(machine, ev),
            time_of(event), config->clear_interval))
	{
            DB(("%d < %d = clear interval\n",
                (int)(time_of(event) -
                time_of_previous_event(machine, ev)),
                config->clear_interval));
        };
#endif
	// pass along the un-forkable event.
	EMIT_EVENT(ev);
    }
}


/*  First (press)
 *  Second    <-- we are here.
 */
static void
apply_event_to_suspect(machineRec *machine, key_event *ev, PluginInstance* plugin)
{
  InternalEvent* event = ev->event;
  Time simulated_time = time_of(event);
  KeyCode key = detail_of(event);

  list_with_tail &queue = machine->internal_queue;
  KeyCode suspected = detail_of(queue.front()->event);

  DeviceIntPtr keybd = plugin->device;

  /* Here, we can
   * o refuse .... if suspected/forkable is released quickly,
   * o fork (definitively),  ... for _time_
   * o start verifying, or wait, or confirm (timeout)
   * todo: I should repeat a bi-depressed forkable.
   */
  assert(!queue.empty() && machine->state == st_suspect);

  // todo: check the ranges (long vs. int)
  int verification_interval = verification_interval_of(machine->config, suspected, 0);
  machine->time_left = verification_interval - (simulated_time - machine->suspect_time);

  if (machine->time_left <= 0) {
    /* time_difference_more(machine->suspect_time, time_of(event), verification_interval)) */

    MDB(("time: VERIFIED! verification_interval = %dms, elapsed so far =%dms  ->",
         verification_interval,  (int)(simulated_time - machine->suspect_time)));
    do_confirm_fork(machine, ev, plugin);
    return;
  } else
      MDB(("suspect: elapsed: %dms   -> needed %dms (left: %d)\n",
           (int) (simulated_time - machine->suspect_time), verification_interval, machine->time_left));

  /* So, we now have a second key, since the duration of 1 key was not enough. */
  if (release_p(event))
  {
      MDB(("suspect/release: suspected = %d, time diff: %d\n", suspected,
           (int)(simulated_time  -  machine->suspect_time)));
      if (key == suspected){
          machine->time_left = 0; // i've used it for calculations ... so
          do_confirm_non_fork(machine, ev, plugin);
          return;
          /* fixme:  here we confirm, that it was not a user error.....
             bad synchro. i.e. the suspected key was just released  */
      } else {
          /* something released, but not verificating, b/c we are in `suspect', not `confirm'  */
          do_enqueue_event(machine, ev); // the `key'
          return;
      };
  } else {
      if (!press_p (event))
      {
          DB(("!!! should be pressKey, but is .. %s on %s",
              event_names[event->any.type - 2 ],
              keybd->name));
          do_enqueue_event(machine,ev);
          return;
      }

      if (key == suspected) {
          /* How could this happen? Auto-repeat on the lower/hw level?
           * And that AR interval is shorter than the fork-verification */
          if (machine->config->fork_repeatable[key]) {
              MDB(("The suspected key is configured to repeat, so ...\n"));
              machine->forkActive[suspected] = suspected;
              machine->time_left = 0;
              do_confirm_non_fork(machine, ev, plugin);
              return;
          } else {
              // fixme: this keycode is repeating, but we still don't know what to do.
              // ..... `discard' the event???
              // fixme: but we should recalc the time_left !!
              return;
          }
      } else {
          // another key pressed
          machine->verificator_time = time_of(event);
          machine->verificator = key; /* if already we had one -> we are not in this state!
                                         if the verificator becomes a modifier ?? fixme:*/
          change_state(machine,st_verify);
          int overlap = overlap_tolerance_of(machine->config, suspected, machine->verificator);
          // fixme: time_left can change now:
          machine->time_left = verification_interval_of(machine->config, suspected,
                                                        machine->verificator)
              -  (simulated_time - machine->suspect_time);

          if (machine->time_left > overlap){
              machine->time_left = overlap; // we start now. so _entire_ overlap-tolerance
          }
          do_enqueue_event(machine, ev);
          return;
      };
  }
}

/*
 * first
 * second
 * third  < we are here now.
 * ???? how long?
 * second Released.
 * So, already 2 keys have been pressed, and still no decision.
 * Now we have the 3rd key.
 *  We wait only for time, and for the release of the key */
static void
apply_event_to_verify(machineRec *machine, key_event *ev, PluginInstance* plugin)
{
    InternalEvent* event = ev->event;
    Time simulated_time = time_of(event);
    KeyCode key = detail_of(event);

    list_with_tail &queue = machine->internal_queue;
    KeyCode suspected = detail_of(queue.front()->event);

    /* We pressed the forkable key, and another one (which could possibly
       use the modifier). Now, either the forkable key was intended
       to be `released' before the press of the other key (and we have an
       error due to mis-synchronization), or in fact, the forkable
       was actually `used' as a modifier.

       This should not be fork:
       I----I
           E--E
       This should be a fork:
       I-----I
           E--E
       Motivation:  we want to press the modifier for short time (simultaneously
       pressing other keys). But sometimes writing quickly, we
       press before we release the previous letter. We handle this, ignoring
       a short overlay. I.e. we wait for the verification key
       to be down in parallel for at least X ms.
       There might be a matrix of values! How to train it?
    */

    /* As before, in the suspect case, we check the 1-key timeout ? But this time,
       we have the 2 key, and we can have a more specific parameter:  Some keys
       are slow to release, when we press a specific one afterwards. So in this case fork slower!
    */
    int verification_interval = verification_interval_of(machine->config, suspected, machine->verificator);

    machine->time_left = verification_interval - (simulated_time - machine->suspect_time);

    if (machine->time_left <= 0) {
        do_confirm_fork(machine, ev, plugin);
        return;
    };

    /* now, check the overlap of the 2 first keys */
    int overlap_tolerance = overlap_tolerance_of(machine->config, suspected, machine->verificator);
    int another_time_left =  overlap_tolerance -  (simulated_time - machine->verificator_time);
    if (another_time_left < machine->time_left)
        machine->time_left = another_time_left;

    if (machine->time_left <= 0) {
        do_confirm_fork(machine, ev, plugin);
        return;
    };

    MDB(("suspected = %d, this: %d, verificator %d. Times: verification: %d, overlap %d, "
         "still needed: %d (ms)\n", suspected, key,
         machine->verificator,
         verification_interval,  overlap_tolerance, machine->time_left));

    if (release_p(event) && (key == suspected)){ // fixme: is release_p(event) useless?
        MDB(("fork-key released on time: %dms is a tolerated error (< %d)\n",
             (int)(simulated_time -  machine->verificator_time), overlap_tolerance));

        machine->time_left = 0; // i've used it for calculations ... so
        do_confirm_non_fork(machine, ev, plugin);
        return;

    } else if (release_p(event) && (machine->verificator == key)){
        /* limit of tolerance of the error */

        // if (time_difference_more(machine->verificator_time, time, overlap_tolerance)){
        machine->verificator = 0; // fixme: no state change??
        // we _should_ take the next possible verificator ?
        // false: we have to wait, maybe the key is indeed a modifier. This verifier is not enough, though
        do_enqueue_event(machine, ev);
        return;
    } else {               // fixme: a (repeated) press of the verificator ?
        // fixme: we pressed another key: but we should tell XKB to repeat it !
        do_enqueue_event(machine, ev);
        return;
    };
}


/* apply event EV to (state, internal-queue, time).
 * This can append to the OUTPUT-queue
 * sets: `time_left'
 *
 * input:
 *   internal-queue  <+      input-queue
 *                   ev
 * output:
 *   either the ev  is pushed on internal_queue, or to the output-queue
 *   the head of internal_queue may be pushed to the output-queue as well.
 */
static void
step_fork_automaton_by_key(machineRec *machine, key_event *ev, PluginInstance* plugin)
{
    assert (ev);

    DeviceIntPtr keybd = plugin->device;
    XkbSrvInfoPtr xkbi= keybd->key->xkbInfo;

    InternalEvent* event = ev->event;
    KeyCode key = detail_of(event);

    /* please, 1st change the state, then enqueue, and then EMIT_EVENT.
     * fixme: should be a function then  !!!*/

    list_with_tail &queue = machine->internal_queue;

    machine->time_left = 0;


#if DDX_REPEATS_KEYS || 1
    /* `quick_ignore': I want to ignore _quickly_ the repeated forked modifiers. Normal
       modifier are ignored before put in the X input pipe/queue This is only if the
       lower level (keyboard driver) passes through the auto-repeat events. */

    if ((key_forked(machine, key)) && press_p(event)
        && (key != machine->forkActive[key])) // not `self_forked'
    {
        DB(("%s: the key is forked, ignoring\n", __FUNCTION__));
        mxfree(ev->event, ev->event->any.length);
        mxfree(ev, sizeof(key_event));
        return;
    }
#endif

    // A currently forked keycode cannot be (suddenly) pressed 2nd time. But any pressed
    // key cannot be pressed once more:
    // assert (release_p(event) || (key < MAX_KEYCODE && machine->forkActive[key] == 0));

#if DEBUG
    /* describe the (state, key) */
    KeySym *sym = XkbKeySymsPtr(xkbi->desc,key);
    if ((!sym) || (! isalpha(* (unsigned char*) sym)))
        sym = (KeySym*) " ";
    MDB(("%s%s%s state: %s, queue: %d, event: %d %s%c %s %s\n",
         info_color,__FUNCTION__,color_reset,
         describe_machine_state(machine),
         queue.length (),
         key, key_color, (char)*sym, color_reset, event_type_brief(event)));
#endif

    switch (machine->state) {
        case st_normal:
            apply_event_to_normal(machine, ev, plugin);
            return;
        case st_suspect:
        {
            apply_event_to_suspect(machine, ev, plugin);
            return;
        }
        case st_verify:
        {
            apply_event_to_verify(machine, ev, plugin);
            return;
        }
        default:
            DB(("----------unexpected state---------\n"));
    }
}


#define final_p(state)  ((state == st_deactivated) || (state == st_activated))

/* Take from input_queue, + the current_time + force   -> run the machine.
 * After that you have to:   cancel the timer!!!
 */
static void
try_to_play(PluginInstance* plugin, Time current_time, Bool force) // force == FALSE for now!
{
  machineRec *machine = plugin_machine(plugin);

  list_with_tail &input_queue = machine->input_queue;

  MDB(("%s: next %s: internal %d, input: %d\n", __FUNCTION__,
       (plugin_frozen(plugin->next)?"frozen":"NOT frozen"),
       machine->internal_queue.length (),
       input_queue.length ()));

  // fixme:  no need to malloc & copy:  just use the machine's one.
  //         you pick from left-overs and free 1 position, and possibly
  //         re-insert to the queue.
  while (!plugin_frozen(plugin->next)) { // && (left_overs.head)

    if (final_p(machine->state)){        /* forked or normal:  final state */

      /* then, reset the machine */
      MDB(("== Resetting the fork machine (internal %d, input %d)\n",
           machine->internal_queue.length (),
           input_queue.length ()));

      change_state(machine,st_normal);
      machine->verificator = 0;

      if (!(machine->internal_queue.empty())) {
        // Slice with a reversed semantic:
        // A.slice(B) --> ()  (AB)
        // traditional is (AB) ()
        machine->internal_queue.slice(input_queue);
        machine->internal_queue.swap(input_queue);

        MDB(("now in input_queue: %d\n", input_queue.length ()));
      }
    };

    key_event* ev = NULL;
    if (! input_queue.empty())
      {
        ev = input_queue.pop();
        // do we have some other events to push?
        // if (empty(input_queue)) break; /* `FINISH' */
        // MDB(("== replaying from the queue %d events\t", replay_size));
        /* and feed it. */
        //  increase_modulo_DO(input_queue.head, MAX_EVENTS_IN_QUEUE);
        MDB(("--calling step_fork_automaton_by_key\n"));
        step_fork_automaton_by_key(machine, ev, plugin);
      } else {
      // at the end ... add the final time event:
      if (current_time && (machine->state != st_normal)){
        step_fork_automaton_by_time (machine, plugin,  current_time);
        if (machine->time_left)
          break; /* `FINISH' */
      } else if (force && (machine->state != st_normal)){
        step_fork_automaton_by_force (machine, plugin);
      } else break; /* `FINISH' */
      // didn't help -> break:
      // error:  consider the time !!
    }
  }
  /* assert(!plugin_frozen(plugin->next)   ---> queue_empty(machine->input_queue)) */
}


/* note: used only in configure.c!
 * Resets the machine, so as to reconsider the events on the
 * `internal' queue.
 * Apparently the criteria/configuration has changed!
 * Reasonably this is in response to a key event. So we are in Final state.
 */
void
replay_events(PluginInstance* plugin, Time current_time, Bool force)
{
    machineRec* machine= plugin_machine(plugin);
    CHECK_LOCKED(machine);

    MDB(("%s\n", __FUNCTION__));

    if (!machine->internal_queue.empty())
    {
        machine->internal_queue.slice (machine->input_queue);
        machine->internal_queue.swap(machine->input_queue);
    }

    machine->state = st_normal;
    // todo: what else?
    // last_released & last_released_time no more available.
    machine->last_released = 0;
    machine->time_left = 0;

    try_to_play(plugin, current_time, force);
}


/*
 *  react to some `hot_keys':
 *  Pause  Pause  -> dump
 */
int                      // return, if config-mode continues.
filter_config_key(PluginInstance* plugin,const InternalEvent *event)
{
    static KeyCode key_to_fork = 0;         //  what key we want to configure

    if (press_p(event))
        switch (detail_of(event)) {
            case 110:
            {
                machineRec* machine = plugin_machine(plugin);
                LOCK(machine);
                dump_last_events(plugin);
                UNLOCK(machine);
                break;
            }

            case 19:
            {
                machineRec* machine = plugin_machine(plugin);
                LOCK(machine);
                machine_switch_config(plugin, machine,0); // current ->toggle ?
                UNLOCK(machine);

                /* fixme: but this is default! */
                machine->forkActive[detail_of(event)] = 0; /* ignore the release as well. */
                break;
            }
            case 10:
            {
                machineRec* machine = plugin_machine(plugin);

                LOCK(machine);
                machine_switch_config(plugin, machine,1); // current ->toggle ?
                UNLOCK(machine);
                machine->forkActive[detail_of(event)] = 0;
                break;
            }
            default:            /* todo: remove this: */
            {
                if (key_to_fork == 0){
                    key_to_fork = detail_of(event);
                } else {
                    machineRec* machine = plugin_machine(plugin);
                    machine->config->fork_keycode[key_to_fork] = detail_of(event);
                    key_to_fork = 0;
                }
            }};
    // should we update the XKB `down' array, to signal that the key is up/down?
    return -1;
}


inline
int                             // return:  0  nothing  -1  skip it
filter_config_key_maybe(PluginInstance* plugin,const InternalEvent *event)
{
    static unsigned char config_mode = 0; // While the Pause key is down.
    static Time last_press_time = 0;

    if (config_mode)
    {
        static int latch = 0;
        // [21/10/04]  I noticed, that some (non-plain ps/2) keyboard generate
        // the release event at the same time as press.
        // So, to overcome this limitation, I detect this short-lasting `down' &
        // take the `next' event as in `config_mode'   (latch)

        if ((detail_of(event) == PAUSE_KEYCODE) && release_p(event)) { //  fake ?
            if ( (time_of(event) - last_press_time) < 30) // fixme: configurable!
            {
                ErrorF("the key seems buggy, tolerating %d: %d! .. & latching config mode\n",
                       time_of(event), (int)(time_of(event) - last_press_time));
                latch = 1;
                return -1;
            }
            config_mode = 0;
            // fixme: key_to_fork = 0;
            ErrorF("dumping (%s) %d: %d!\n",
                   plugin->device->name,
                   time_of(event), (int)(time_of(event) - last_press_time));
            // todo: send a message to listening clients.
            dump_last_events(plugin);
        }
        else {
            last_press_time = 0;
            if (latch) {
                config_mode = latch = 0;
            };
            config_mode = filter_config_key (plugin, event);
        }
    }
    // `Dump'
    if ((detail_of(event) == PAUSE_KEYCODE) && press_p(event))
        /* wait for the next and act ? but start w/ printing the last events: */
    {
        last_press_time = time_of(event);
        ErrorF("entering config_mode & discarding the event: %u!\n", last_press_time);
        config_mode = 1;

        /* fixme: should I update the ->down bitarray? */
        return -1;
    } else
        last_press_time = 0;
    return 0;
}


static void
set_wakeup_time(PluginInstance* plugin, Time now)
{
   machineRec* machine = plugin_machine(plugin);
   CHECK_LOCKED(machine);
   plugin->wakeup_time = (machine->time_left) ? machine->time_left + now:
       (machine->internal_queue.empty())? plugin->next->wakeup_time:0;

   MDB(("%s %s wakeup_time = %u, next wants: %u\n", FORK_PLUGIN_NAME, __FUNCTION__,
	(int)plugin->wakeup_time, (int)plugin->next->wakeup_time));
}


static key_event*
create_handle_for_event(InternalEvent *event, bool owner)
{
   InternalEvent* qe;
   if (owner)
       qe = event;
   else {
     qe = (InternalEvent*)malloc(event->any.length);
     if (!qe)
         {
            // if we are out-of-memory, we probably cannot even process ErrorF, can we?
            ErrorF("%s: out-of-memory\n", __FUNCTION__);
            return NULL;
         }
   }
   // the handle is deallocated in `try_to_output'
   key_event* ev = (key_event*)malloc(sizeof(key_event));
   if (!ev) {
      /* This message should be static string. otherwise it fails as well? */
      ErrorF("%s: out-of-memory, dropping\n", __FUNCTION__);
      if (!owner)
         mxfree (qe, event->any.length);
      return NULL;
   };

   memcpy(qe, event, event->any.length);
   DB(("+++ accepted new event: %s\n",
       event_names[event->any.type - 2 ]));

   ev->event = qe;
   ev->forked = 0;
   return ev;
}



/*  This is the handler for all key events.  Here we delay pushing them forward.
    it's a trampoline for the automaton.
    Should it return some Time?
 */
static void
ProcessEvent(PluginInstance* plugin, InternalEvent *event, Bool owner)
{
    DeviceIntPtr keybd = plugin->device;
    Time now = time_of(event);

#define ACT_ON_ONLY_PRESS_RELEASE 0 // no, RawPress should be stored in the same queues,
                                    // to keep the sequence.

#if ACT_ON_ONLY_PRESS_RELEASE
    if (!(press_p(event) || release_p(event)))
    {
        PluginInstance* next = plugin->next;
#if DEBUG
        if (((machineRec*) plugin_machine(plugin))->config->debug)
        {
            DB(("%s<<< skipping %d ... %s on %s", keysym_color,
                event->any.type, event_names[event->any.type - 2 ],
                keybd->name));
            //describe_key(keybd, event);
            DB(("%s\n", color_reset));
        }
#endif
                // assert (!plugin_frozen(next));
        PluginClass(next)->ProcessEvent(next, event, owner);
        return;
    };
#endif  // ONLY_PRESS_RELEASE



    if (filter_config_key_maybe(plugin, event) < 0)
    {
        if (owner)
            xfree(event);
        // fixme: I should at least push the time of (plugin->next)!
        return;
    };
    machineRec* machine = plugin_machine(plugin);

    CHECK_UNLOCKED(machine);
    LOCK(machine);           // fixme: mouse must not interrupt us.

    key_event* ev = create_handle_for_event(event, owner);
    if (!ev)			// memory problems
        return;

#if DEBUG
    if (((machineRec*) plugin_machine(plugin))->config->debug) {
        DB(("%s>>> ", key_io_color));
        DB(("%s", describe_key(keybd, ev->event)));
        DB(("%s\n", color_reset));
    }
#endif

    machine->input_queue.push(ev);
    try_to_play(plugin, 0, FALSE);

    set_wakeup_time(plugin, now);
    UNLOCK(machine);
};

// this is an internal call.
static void
step_in_time_locked(PluginInstance* plugin, Time now)
{
    machineRec* machine = plugin_machine(plugin);
    MDB(("%s:\n", __FUNCTION__));

    /* is this necessary?   I think not: if the next plugin was frozen,
     * and now it's not, then it must have warned us that it thawed */
    try_to_output(plugin);

    /* push the time ! */
    try_to_play(plugin, now, FALSE);

    /* i should take the minimum of time and the time of the 1st event in the
       (output) internal queue */
    if (machine->internal_queue.empty() && machine->input_queue.empty()
        && !plugin_frozen(plugin->next))
    {
        UNLOCK(machine);
        /* might this be invoked several times?  */
        PluginClass(plugin->next)->ProcessTime(plugin->next, now);
        UNLOCK(machine);
    }
    // todo: we could push the time before the first event in internal queue!
    set_wakeup_time(plugin, now);
}

// external API
static void
step_in_time(PluginInstance* plugin, Time now)
{
   machineRec* machine = plugin_machine(plugin);
   MDB(("%s:\n", __FUNCTION__));
   LOCK(machine);
   step_in_time_locked(plugin, now);
   UNLOCK(machine);
};


/* Called from AllowEvents, after all events from following plugins have been pushed: . */
static void
fork_thaw_notify(PluginInstance* plugin, Time now)
{
  machineRec* machine = plugin_machine(plugin);
  MDB(("%s @ time %u\n", __FUNCTION__, (int)now));

  LOCK(machine);
  try_to_output(plugin);
  // is this correct?

  if (!plugin_frozen(plugin->next) && PluginClass(plugin->prev)->NotifyThaw)
    {
      /* thaw the previous! */
      set_wakeup_time(plugin, now);
      UNLOCK(machine);
      MDB(("%s -- sending thaw Notify upwards!\n", __FUNCTION__));
      /* fixme:  Tail-recursion! */
      PluginClass(plugin->prev)->NotifyThaw(plugin->prev, now);
      /* I could move now to the time of our event. */
      /* step_in_time_locked(plugin, now); */
    } else {
    MDB(("%s -- NOT sending thaw Notify upwards %s!\n", __FUNCTION__,
         plugin_frozen(plugin)?"next is frozen":"prev has not NotifyThaw"));
    UNLOCK(machine);
  }
}


/* For now this is called to many times, for different events.! */
static void
mouse_call_back(CallbackListPtr *, PluginInstance* plugin,
                DeviceEventInfoRec* dei)
{
   InternalEvent *event = dei->event;
   machineRec* machine = plugin_machine(plugin);

   if (event->any.type == ET_Motion)
      {
         if (machine->lock)
            ErrorF("%s running, while the machine is locked!\n", __FUNCTION__);
         /* else */
         LOCK(machine);
         step_fork_automaton_by_force(plugin_machine(plugin), plugin);
         UNLOCK(machine);
      }
}


/* We have to make a (new) automaton: allocate default config,
 * register hooks to other devices,
 *
 * returns: erorr of Success. Should attach stuff by side effect ! */
static PluginInstance*
make_machine(DeviceIntPtr keybd, DevicePluginRec* plugin_class)
{
   DB(("%s\n", __FUNCTION__));
   assert (strcmp(plugin_class->name, FORK_PLUGIN_NAME) == 0);

   PluginInstance* plugin = MALLOC(PluginInstance);
   plugin->pclass = plugin_class;
   plugin->device = keybd;

   machineRec* forking_machine = NULL;

   // I create 2 config sets.  1 w/o forking.
   // They are numbered:  0 is the no-op.
   fork_configuration* config_no_fork = machine_new_config(); // configuration number 0
   config_no_fork->debug = 0;   // should be settable somehow.
   if (!config_no_fork)
      {
         return NULL;
      }

   fork_configuration* config = machine_new_config();
   if (!config)
      {
         free (config_no_fork);
         return NULL;
      }

   config->next = config_no_fork;
   // config->id = config_no_fork->id + 1;
   // so we start w/ config 1. 0 is empty and should not be modifiable

   ErrorF("%s: constructing the machine %d (official release: %s)\n",
	  __FUNCTION__, PLUGIN_VERSION, VERSION_STRING);

   forking_machine =  (machineRec* )mmalloc(sizeof(machineRec));
   bzero(forking_machine, sizeof (machineRec));

   if (! forking_machine){
      ErrorF("%s: malloc failed (for forking_machine)\n",__FUNCTION__);
      // free all the previous ....!
      return NULL;              // BadAlloc
   }


   // now, if something goes wrong, we have to free it!!
   forking_machine->internal_queue.set_name("internal");
   forking_machine->input_queue.set_name("input");
   forking_machine->output_queue.set_name("output");


   forking_machine->max_last = 100;
   forking_machine->last_events = new last_events_type(forking_machine->max_last);

   forking_machine->state = st_normal;
   forking_machine->last_released = 0;

   UNLOCK(forking_machine);
   forking_machine->time_left = 0;

   for (int i=0;i<256;i++){                   // keycode 0 is unused!
      forking_machine->forkActive[i] = 0; /* 0 = not active */
   };

   config->debug = 1;
   forking_machine->config = config;

   forking_machine->time_of_last_output = 0;

   plugin->data = (void*) forking_machine;
   ErrorF("%s: returning %d\n", __FUNCTION__, Success);

   AddCallback(&DeviceEventCallback, (CallbackProcPtr) mouse_call_back, (void*) plugin);

   plugin_class->ref_count++;
   return plugin;
};


/* fixme!
   This is a wrong API: there is no guarantee we can do this.
   The pipeline can get frozen, and we have to wait on thaw.
   So, it's better to have a callback. */
static int
stop_and_exhaust_machine(PluginInstance* plugin)
{
  machineRec* machine = plugin_machine(plugin);
  LOCK(machine);
  MDB(("%s: what to do?\n", __FUNCTION__));
  // free all the stuff, and then:
  xkb_remove_plugin (plugin);
  return 1;
}


static int
destroy_machine(PluginInstance* plugin)
{
   machineRec* machine = plugin_machine(plugin);
   LOCK(machine);

   delete machine->last_events;
   DeleteCallback(&DeviceEventCallback, (CallbackProcPtr) mouse_call_back,
                  (void*) plugin);
   MDB(("%s: what to do?\n", __FUNCTION__));
   return 1;
}



// This macro helps with providing
// initial value of struct- the member name is along the value.
#if __GNUC__
#define _B(name, value) value
#else
#define _B(name, value) name : value
#endif

static pointer /*DevicePluginRec* */
fork_plug(pointer	options,
          int		*errmaj,
          int		*errmin)
{
  DB(("%s\n", __FUNCTION__));
  static struct _DevicePluginRec plugin_class =
    {
      // slot name,     value
      _B(name, FORK_PLUGIN_NAME),
      _B(instantiate, make_machine),
      _B(ProcessEvent, ProcessEvent),
      _B(ProcessTime, step_in_time),
      _B(NotifyThaw, fork_thaw_notify),
      _B(config,    machine_configure),
      _B(getconfig, machine_configure_get),
      _B(client_command, machine_command),
      _B(module, NULL),
      _B(ref_count, 0),
      _B(stop,       stop_and_exhaust_machine),
      _B(terminate,  destroy_machine)
    };
  plugin_class.ref_count = 0;
  xkb_add_plugin(&plugin_class);

  return &plugin_class;
}


extern "C" {

void __attribute__((constructor)) on_init()
{
    ErrorF("%s:\n", __FUNCTION__); /* impossible */
    fork_plug(NULL,NULL,NULL);
}
}

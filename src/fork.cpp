/*
 * Copyright (C) 2003-2005-2010 Michal Maruska <mmaruska@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


/* fixme:  When we change the VT, we should release all forked stuff!
   Simply push e EOF event. */

/* note: all functions should be *.so local (static), accessible only via
   slots in exported structure! */

/* possible bug:
   time 0 could be a valid time?
   CurrentTime is 0.

   timeouts (per key, or global) must not be changed while we use them.
*/

#define USE_LOCKING 1
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


/* performance enhancement:
 *  the 0 config could  be just  a NULL pointer. It consumes too much memory, maybe! */


/* mouse stuff is run in a signal handler!
   any entry in here must  start w/  machine->lock = 1; and end w/ machine->lock = 0;

   bug: mouse (FORCE) vs replaying ..... un freeze  & reconfig i should replay even
   output_queue!
*/


#include "config.h"
#include "debug.h"
#include "fork.h"


extern "C" {

#include <X11/X.h>
#include <X11/Xproto.h>
#include <X11/keysym.h>

/* `probably' I use it only to print out the keysym in debugging stuff*/
#include <xorg/xkbsrv.h>
#include <xorg/eventstr.h>

#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <xkbfile.h>
/* `configuration' .... processing requests: */
}


// fork_reason
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

/*  Functions on xEvent */

inline Bool
release_p(const InternalEvent* event)
{
   assert(event);
   return (event->any.type == ET_KeyRelease);
}

inline Bool
press_p(const InternalEvent* event)
{
   assert(event);
   return (event->any.type == ET_KeyPress);
}

inline const char*
event_type_brief(InternalEvent *event)
{
    return (press_p(event)?"down":
	    (release_p(event)?"up":"??"));
}

inline Bool
forkable_p(fork_configuration* config, KeyCode code)
{
   return (config->fork_keycode[code]);
}

inline Time
time_of(const InternalEvent* event)
{
   return event->any.time;
}

inline
KeyCode
detail_of(const InternalEvent* event)
{
   assert(event);
   return event->device_event.detail.key;
}

// static implicitly
const int BufferLenght = 200;

/* the returned string is in static space. don't free it! */
static const char*
describe_key(DeviceIntPtr keybd, InternalEvent *event)
{
    assert (event);

    static char buffer[BufferLenght];
    XkbSrvInfoPtr xkbi= keybd->key->xkbInfo;
    KeyCode key = detail_of(event);
    char* keycode_name = xkbi->desc->names->keys[key].name;
    // assert(0 <= key <= (max_key_code - min_key_code));

    const KeySym *sym= XkbKeySymsPtr(xkbi->desc,key);
    if ((!sym) || (! isalpha(*(unsigned char*)sym)))
	sym = (KeySym*) " ";

    snprintf(buffer, BufferLenght, "(%s) %d %4.4s -> %c %s (%u)",
	     keybd->name,
	     key, keycode_name,(char)*sym,
	     event_type_brief(event),(unsigned int)time_of(event));

    return buffer;
}


/* the returned string is in static space. don't free it! */
static const char*
describe_machine_state(machineRec* machine)
{
   static char buffer[BufferLenght];

   snprintf(buffer, BufferLenght, "%s[%dm%s%s",
            escape_sequence, 32 + machine->state,
            state_description[machine->state], color_reset);
   return buffer;
}


/* push the event to the next plugin. ownership is transfered! */
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
   PluginClass(next)->ProcessEvent(next, event, TRUE); // we give away the ownership
}



/* The machine is locked here:
 * push as many as possible from the OUTPUT queue to the next layer */
static void
try_to_output(PluginInstance* plugin)
{
   machineRec* machine = plugin_machine(plugin);
#if USE_LOCKING
   assert(machine->lock);
#endif

   list_with_tail &queue = machine->output_queue;
   PluginInstance* next = plugin->next;

   MDB(("%s: Queues: output: %d\t internal: %d\t input: %d \n", __FUNCTION__,
        queue_lenght(queue),
        queue_lenght(machine->internal_queue),
        queue_lenght(machine->input_queue)));

   while ((!plugin_frozen(next)) && (queue.head)) {
       cons* old = pop_from_queue(queue, 0);

       /* fixme: ownership! */
#if STATIC_LAST
       machine->last_events[machine->last_head].key = detail_of(old->car);
       machine->last_events[machine->last_head].time = time_of(old->car);
       machine->last_events[machine->last_head].press = press_p(old->car);
       machine->last_events[machine->last_head].forked = old->forked;

       machine->last_head = (machine->last_head + 1) % machine->max_last;
#else
       push_on_queue(machine->last_events, old);
       if (++(machine->last_events_count) > machine->max_last)
	   /* fixme: this will overflow ! */
	   {
	       /* ok if empty? */
	       old = pop_from_queue(machine->last_events, 0);
	       mxfree(old, sizeof(cons));
	       /* fixme:  is it the handle or event? */
	   }
#endif
       {
	 InternalEvent* event = old->car;
	 mxfree(old, sizeof(cons));

	 machine->lock = 0;
	 hand_over_event_to_next_plugin(event, plugin);
	 machine->lock = 1;
       }
   };
   if (!plugin_frozen(next) && (queue_lenght(queue) > 0)){
      MDB(("%s: still %d events to output\n", __FUNCTION__, queue_lenght(queue)));
   }
}

static void
output_event(cons* handle, PluginInstance* plugin, const char* comment)
{
   // fixme: we should store the info on the really pressed keycode (simply for debugging)
   // this is problematic, however !!
   assert(handle->car);
   assert(! handle->cdr);

   InternalEvent *event = handle->car;
   machineRec* machine = plugin_machine(plugin);

#if (! KEEP_PREVIOUS)
   machine->time_of_last_output = time_of(event);
#endif


#if 0
   // first inform the central client ..
   send_message_about_event(plugin->device, handle);
   // this could be  wrong?  if we don't `run' lazily, we could accumulate events, just to
   // `replay' them later!
#endif

   push_on_queue(machine->output_queue, handle);
   try_to_output(plugin);
};

/* event* we copy a pointer !!!  */
#define emit_event(handle) {output_event(handle, plugin, "");}



/**
 * Operations on the machine
 * fixme: should it include the `self_forked' keys ?
 * `self_forked' means, that i decided to NOT fork. to mark this decision
 * (for when a repeated event arrives), i fork it to its own keycode
 */


/* return the keycode into which CODE has forked (last time). returns code itself, if not forked. */
inline Bool
forked_to(machineRec *machine, KeyCode code)
{
   return (machine->forkActive[code]);
}


/* fixme: should I make the machine a C++ object (class). with its methods? */
inline void
change_state(machineRec* machine, state_type new_state)
{
   machine->state = new_state;
   MDB((" --->%s[%dm%s%s\n", escape_sequence, 32 + new_state,
	state_description[new_state], color_reset));
}



inline Time
verification_interval_of(fork_configuration* config, KeyCode code, KeyCode verificator)
{

   return (config->verification_interval_per_key[code][verificator]?
           config->verification_interval_per_key[code][verificator]:
           (config->verification_interval_per_key[code][0]?
            config->verification_interval_per_key[code][0]:
            config->verification_interval)); /* fixme: `uniformity' use [0][0] */
}


inline Time
overlap_tolerance_of(fork_configuration* config, KeyCode code, KeyCode verificator)
{
   return (config->overlap_tolerance_per_key[code][verificator] ?
           config->overlap_tolerance_per_key[code][verificator]:
           (config->overlap_tolerance_per_key[code][0]?
            config->overlap_tolerance_per_key[code][0] :
            config->overlap_tolerance)) ;
}



/* fork the 1st element on queue (the internal_queue). Remove it from the queue
 * and push to the output_queue.
 *
 * todo: Should i have a link back from machine to the plugin? Here useful!
 * todo:  do away with the `forked_key' argument --- it's useless!
 * */
inline void
activate_fork(machineRec *machine, list_with_tail &queue, PluginInstance* plugin,
	      KeyCode forked_key)
{
   assert(queue.head);
   assert(detail_of(queue.head->car) == forked_key);
   //assert(queue == machine->internal_queue);

   cons* handle = pop_from_queue(queue, 0);

   /* change the keycode, but remember the original: */
   handle->forked = forked_key;
   machine->forkActive[forked_key] = /* todo:  set_detail_of */
       handle->car->device_event.detail.key = machine->config->fork_keycode[forked_key];

   change_state(machine,activated);
   output_event(handle, plugin, __FUNCTION__);

   MDB(("%s suspected: %d-> forked to: %d,  internal queue is long: %d, %s\n", __FUNCTION__,
        forked_key,
        machine->config->fork_keycode[forked_key], // fixme: -1
        queue_lenght(machine->internal_queue),
        describe_machine_state(machine)));
}


/*
 *
 *  called by mouse movement. AND ?  by try_to_play force <- replay_events   which is never!
 * note: was , Bool force
 */
static void
step_fork_automaton_by_force(machineRec *machine, PluginInstance* plugin) /* fixme: output ?? */
{
   /* make all the forkable  forked:   Confirm: */
   if (machine->state == normal) {
      return;      /* impossible */
   }

   if (machine->state == deactivated) {
      ErrorF("%s: ??????? \n", __FUNCTION__); /* impossible */
      return;
   }

   if (queue_empty(machine->internal_queue))
      return;

   /* so, the state is either  verify, suspect or activated. */
   list_with_tail& queue = machine->internal_queue;
   KeyCode suspected = detail_of(queue.head->car);

   MDB(("%s%s%s state: %s, queue: %d .... FORCE\n",
        fork_color, __FUNCTION__, color_reset,
        describe_machine_state(machine),
        queue_lenght(queue)));

   machine->time_left = 0;
   activate_fork(machine, queue, plugin, suspected);
   return;
}



static void
do_enqueue_event(machineRec *machine, cons *handle)
{
   // either press, or release of another key.
   // i have to disable the _last_ press.
   // fixme  key or xkbi->repeatKey ??

   push_on_queue(machine->internal_queue, handle);
   MDB(("enqueue_event: time left: %d\n", machine->time_left));         // we don't emit here...
   return;
}

static void
do_confirm_non_fork(machineRec *machine, cons *handle, PluginInstance* plugin)
{
   assert(machine->time_left == 0);
   change_state(machine,deactivated);
   /* produce the 2 events.... */
   push_on_queue(machine->internal_queue, handle); //  this  will be re-processed!!

   cons* non_forked_handle = pop_from_queue(machine->internal_queue, 0);
   MDB(("this is not a fork! %d\n",
	detail_of(non_forked_handle->car)));
   emit_event(non_forked_handle);
   return;
}

// so HANDLE confirms fork of the suspect- first event on the internale_queue.
static void
do_confirm_fork(machineRec *machine, cons *handle, PluginInstance* plugin)
{
   if (machine->time_left < 0)
       machine->time_left = 0;

   /* fixme: handle is the just-read event. But that is surely not the head
      of queue (which is confirmed to fork) */

   activate_fork(machine, machine->internal_queue, plugin,
		 detail_of(machine->internal_queue.head->car));
   push_on_queue(machine->internal_queue, handle);
   DB(("confirm:\n"));
   return;
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
   if ((machine->state == normal) || (machine->state == deactivated)) {
      ErrorF("%s: unexpected: %s!", __FUNCTION__, describe_machine_state(machine));
      // impossible
      return;
   };

   list_with_tail &queue = machine->internal_queue;
   if (! queue.head){
      DB(("%s: the internal queue is empty. Most likely the last event caused freeze,"
          "and replay_events didn't do any work\thowever, this timer is BUG\n", __FUNCTION__));
      /* fixme! now what?  */
   }

   // confirm fork:
   int reason;
   KeyCode suspected = detail_of(queue.head->car);

   MDB(("%s%s%s state: %s, queue: %d, time: %u key: %d\n",
        fork_color, __FUNCTION__, color_reset,
        describe_machine_state (machine),
        queue_lenght(queue), (int)current_time, suspected));

   /* First, i try the simple (fork-by-one-keys).
    * If that works, -> fork! Otherwise, i try w/ 2-key forking, overlapping.
    */
   {
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
   }

   /* To test 2 keys overlap, we need the 2nd key: a verificator! */
   if (machine->state == verify) {
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
   activate_fork(machine, queue, plugin, suspected);
   return;
}


inline Time
time_of_previous_event(machineRec *machine, cons *handle)
{
#if KEEP_PREVIOUS
    return time_of(handle->previous->car);
#else
    return machine->time_of_last_output;
#endif
}




#define suspected_p(k)     (k == suspected)

static void
apply_event_to_verify(machineRec *machine, cons *handle, PluginInstance* plugin)
{
    InternalEvent* event = handle->car;
    Time simulated_time = time_of(event);
    KeyCode key = detail_of(event);

    list_with_tail &queue = machine->internal_queue;
    KeyCode suspected = detail_of(queue.head->car);

    /*
      first
      second
      third  < we are here now.
      ???? how long?
      second Released.
      So, already 2 keys have been pressed, and still no decision.
      Now we have the 3rd key.
      We wait only for time, and for the release of the key */

    /* We pressed the forkable key, and another one (which could possibly
       use the modifier). Now, either the forkable key was intended
       to be `released' before the press of the other key (and we have an
       error due to mis-synchronization), or in fact, the forkable
       was actuallly `used' as a modifier.
       motivation:  we want to press the modifier for short time (simultaneously
       pressing other keys). But sometimes writing quickly, we
       press before we release the previous letter. We handle this, ignoring
       a short overlay. E.i. we wait for the verification key
       to be pressed at least ...ms in parallel.
    */

    /* if we release quickly either the suspected, or the verifying key, then ....  otherwise ? */
    /* release timeout !!!  */
    /* fixme: if the suspected key is another modifier? and we press other keys,
       what happens? so far, we don't care about the `verificator' */

    /* as before, in the suspect case, we check the 1-key timeout ? But this time,
       we have the 2 key, and we can have a more specific parameter:  Some keys
       are slow to release, when we press a specific one afterwards. So in this case fork slower!
    */
    int verification_interval = verification_interval_of(machine->config, suspected, machine->verificator);

    machine->time_left = verification_interval - (simulated_time - machine->suspect_time);

    if (machine->time_left <= 0) {
	do_confirm_fork(machine, handle, plugin);
	return;
    };

    /* now, check the overlap of the 2 first keys */
    int overlap_tolerance = overlap_tolerance_of(machine->config, suspected, machine->verificator);
    int another_time_left =  overlap_tolerance -  (simulated_time - machine->verificator_time);
    if (another_time_left < machine->time_left)
	machine->time_left = another_time_left;

    if (machine->time_left <= 0) {
	do_confirm_fork(machine, handle, plugin);
	return;
    };

    MDB(("suspected = %d, this: %d, verificator %d. Times: verification: %d, overlap %d, "
	 "still needed: %d (ms)\n", suspected, key,
	 machine->verificator,
	 verification_interval,  overlap_tolerance, machine->time_left));

    if (release_p(event) && suspected_p(key)){ // fixme: is release_p(event) useless?
	MDB(("fork-key released on time: %dms is a tolerated error (< %d)\n",
	     (int)(simulated_time -  machine->verificator_time), overlap_tolerance));

	machine->time_left = 0; // i've used it for calculations ... so
	do_confirm_non_fork(machine, handle, plugin);
	return;

    } else if (release_p(event) && (machine->verificator == key)){
	/* limit of tolerance of the error */


	// if (time_difference_more(machine->verificator_time, time, overlap_tolerance)){
	machine->verificator = 0; // fixme: no state change??
	// we _should_ take the next possible verificator ?

	// false: we have to wait, maybe the key is indeed a modifier. This verifier is not enough, though
	do_enqueue_event(machine, handle);
	return;
    } else {               // fixme: a (repeated) press of the verificator ?
	// fixme: we pressed another key: but we should tell XKB to repeat it !
	do_enqueue_event(machine, handle);
	return;
    };
}


static void
apply_event_to_suspect(machineRec *machine, cons *handle, PluginInstance* plugin)
{
    InternalEvent* event = handle->car;
    Time simulated_time = time_of(event);
    KeyCode key = detail_of(event);

    list_with_tail &queue = machine->internal_queue;
    KeyCode suspected = detail_of(queue.head->car);

    DeviceIntPtr keybd = plugin->device;

    /* here, we can
     * o refuse .... if suspected/forkable is released quickly,
     * o fork (definitively),  ... for _time_
     * o start verifying, or wait, or confirm (timeout)
     * todo: i should repeat a bi-depressed forkable.
     * */
    assert(queue.head);


    // fixme: this should not happen, b/c otherwise the timer would _have_ been run
    int verification_interval = verification_interval_of(machine->config, suspected, 0);
    machine->time_left = verification_interval - (simulated_time - machine->suspect_time);
    // time_of(event)

    MDB(("suspect: elapsed: %dms   -> needed %dms (left: %d)\n",
	 (int) (simulated_time - machine->suspect_time), verification_interval, machine->time_left));


    /* Time is enough: */
    if (machine->time_left <= 0) {
	/* time_difference_more(machine->suspect_time, time_of(event), verification_interval)) */
	// we fork.
	MDB(("time: VERIFIED! verification_interval = %dms, elapsed so far =%dms  ->",
	     verification_interval,  (int)(simulated_time - machine->suspect_time)));
	do_confirm_fork(machine, handle, plugin);
	return;
    };


    /* So, we now have a second key, since the duration of 1 key was not enough. */
    if (release_p(event))
	{
	    MDB(("suspect/release: suspected = %d, time diff: %d\n", suspected,
		 (int)(simulated_time  -  machine->suspect_time)));
	    if (suspected_p(key)){
		machine->time_left = 0; // i've used it for calculations ... so
		do_confirm_non_fork(machine, handle, plugin);
		return;
		/* fixme:  here we confirm, that it was not a user error.....
		   bad synchro. i.e. the suspected key was just released  */
	    } else {
		/* something released, but not verificating, b/c we are in `suspect', not `confirm'  */
		do_enqueue_event(machine, handle); // the `key'
		return;
	    };
	}
    else
	{
	    if (!press_p (event))
		{
		    DB(("!!! should be pressKey, but is .. %s on %s",
			event_names[event->any.type - 2 ],
			keybd->name));
		    do_enqueue_event(machine,handle);
		    return;
		}

	    if (suspected_p(key)) {
		/* fixme:   How could this happen?  auto-repeat on the lower level? */
		/* ignore;       repetition */
		MDB(("---------- testing fork_repeatable ---------\n"));
		if (machine->config->fork_repeatable[key]) {
		    MDB(("The suspected key is configured to repeat, so ...\n")); // fixme !!!
		    // `NO_FORK'
		    // fixme:
		    machine->forkActive[suspected] = suspected; // ????
		    machine->time_left = 0; // i've used it for calculations ... so
		    do_confirm_non_fork(machine, handle, plugin);
		    return;
		} else{

		    // fixme: this code is repeating, but we still don't know what to do.
		    // ..... `discard' the event???
		    // fixme: but we should recalc the time_left !!

		    return;
		    // goto confirm_fork;
		}

	    } else {
		/* another release */
		machine->verificator_time = time_of(event);
		machine->verificator = key; /* if already we have one -> we are not in this state!
					       if the verificator becomes a modifier ?? fixme:*/
		change_state(machine,verify);
		int overlap = overlap_tolerance_of(machine->config, suspected, machine->verificator);
#if 0
		time = min (machine->time_left, overlap);
#else
		// fixme: time_left can change now:
		machine->time_left = verification_interval_of(machine->config, suspected, machine->verificator)
		    -  (simulated_time - machine->suspect_time);

		if (machine->time_left > overlap){
		    machine->time_left = overlap; // we start now. so _entire_ overlap-tolerance
		}
#endif
		do_enqueue_event(machine, handle);
		return;
	    };
	}
    return;
}

static void
apply_event_to_normal(machineRec *machine, cons *handle, PluginInstance* plugin)
{
    DeviceIntPtr keybd = plugin->device;
    XkbSrvInfoPtr xkbi= keybd->key->xkbInfo;

    InternalEvent* event = handle->car;
    KeyCode key = detail_of(event);
    Time simulated_time = time_of(event);

    fork_configuration* config = machine->config;    // todo: reference, not pointer.

    /* "closure":  When we press a key the second time in a row, we might avoid forking:
     * So, this is for the detector:
     *
     * This means i cannot do this trick w/ 2 keys, only 1 is the last/considered! */
    /* fixme: Should this be canceled when we play-by-time? or by force ? */
    static KeyCode last_released = 0;
    static int last_released_time = 0; // fixme: does it work w/ _by_force _by_time ?


    // assert (queue->head == queue->tail); /* fixme:  */
    // queue->head = queue->tail = 0;  /* fixme: why ?? */


    // i want to suppress some, and go for repetition.
    /*  class of `supression':    keys which have a repeating function: movement !!!
     *  8 classes:  a (^..^) v (..^.) ... v (). form.
     *  could be 256 ! classes.
     *  */

    XkbDescPtr xkb = xkbi->desc;

    // if (this key might start a fork)
    if (press_p(event) && (forkable_p(config, key))
	// fixme:  the clear interval should just hint, not preclude!
	&& !time_difference_less(
	    time_of_previous_event(machine, handle),
	    time_of(event),
	    config->clear_interval)
	&& !(xkb->ctrls->enabled_ctrls & XkbMouseKeysMask)) /* fixme: is it w/ 1-event precision? */
    {   // does it have a mouse-related action?
	/* i want to activate repetition: by depressing the key: */
#if DEBUG
	if ( !forked_to(machine, key) && (last_released == key ))
	    MDB (("can we invoke autorepeat? %d  upper bound %d ms\n",
		  (int)(simulated_time - last_released_time), config->repeat_max));
#endif
	/* press_p  & forked .... that's already skipped by the `quick_ignore'
	   hm, not really. if it is `self_forked' !!! */
	if (!forked_to(machine, key) &&
	    ((last_released != key ) ||
	     (int)(simulated_time - last_released_time) > config->repeat_max)
	    //time_difference_more(last_released_time,simulated_time, config->repeat_max))
	    )
	{
	    // _supressed         fixme: repeated events ?

	    /* fixme: unless the state is incompatible !!! */
	    change_state(machine, suspect);
	    machine->suspect_time = time_of(event);
	    machine->time_left = verification_interval_of(machine->config, key, 0);
	    //  bug: suspected
	    do_enqueue_event(machine, handle);
	    return;
	} else {
	    // self-forked or  not yet forked, but re-pressed very quickly.
	    //      -> fixme: we should mark it s self-forked
	    MDB(("%s\n", forked_to(machine, key)?"self-forked":"re-pressed very quickly"));

	    machine->forkActive[key] = key; // fixme: why??
	    // should be:
	    // goto confirm_non_fork;  note: that is not for 'normal' state!
	    // but we
	    emit_event(handle); /* time_left = 0! */
	    return;
	};
    }
    else if (release_p (event) && (forked_to(machine, key)))
    {
	MDB(("releasing forked key\n"));
	// fixme:  we should see if the fork was `used'.
	if (config->consider_forks_for_repeat){
	    // C-f   f long becomes fork. now we wanted to repeat it....
	    last_released = detail_of(event);
	    last_released_time = time_of(event);
	}
	/* fixme:
	   else {
	   last_released = 0;
	   last_released_time = 0;
	   }
	*/

	/* we finally release a (self-)forked key. Rewrite back the keycode.
	 *
	 * fixme: do i do this in other machine states?
	 */
	event->device_event.detail.key = machine->forkActive[key];

	//  this is the state (of the keyboard, not the machine).... better to say of the machine!!!
	machine->forkActive[key] = 0;
	emit_event(handle);
    }
    else
    {
	if (release_p (event))
	{ // fixme: redundant?
	    last_released = detail_of(event);
	    last_released_time = time_of(event);
	}
#if DEBUG
	if (time_difference_less(time_of_previous_event(machine, handle),
				 time_of(event), config->clear_interval))
	{
	    DB(("%d < %d = clear interval\n",
		(int)(time_of(event) -
		      time_of_previous_event(machine, handle)),
		config->clear_interval));
	};
#endif // DEBUG


	// un-forkable event:  pass along
	emit_event(handle);
    }
    return;
}

/* apply EVENT to (STATE, internal-QUEUE, TIME).
 * This can append to the OUTPUT-queue
 * sets: `time_left'
 *
 * input:
 *   internal-queue  ^      input-queue
 *                   handle
 * output:
 *   either the handle  is pushed on internal_queue, or to the output-queue
 *   the head of internal_queue may be pushed to the output-queue as well.
 *
 *see:  emit_event
 *      push_on_queue
 */
static void
step_fork_automaton_by_key(machineRec *machine, cons *handle, PluginInstance* plugin)
{
   assert (handle);

   DeviceIntPtr keybd = plugin->device;
   XkbSrvInfoPtr xkbi= keybd->key->xkbInfo;

   InternalEvent* event = handle->car;
   KeyCode key = detail_of(event);

   /* please, 1st change the state, then enqueue, and then emit_event.
    * fixme: should be a function then  !!!*/

   list_with_tail &queue = machine->internal_queue;

   // default values:
   machine->time_left = 0;

   /* this is used in various cases, but in _common_ in the goto: `confirm_fork' */
#if DDX_REPEATS_KEYS || 1
   /* `quick_ignore': I want to ignore _quickly_  the repeated forked modifiers.
      Normal modifier are ignored before put in the X input pipe/queue
      This is only if the lower level (keyboard driver) passes through the  auto-repeat events. */
   if ((forked_to(machine, key)) && press_p(event) && (key != machine->forkActive[key]))
       { // not `self_forked'
	   DB(("%s: the key is forked, ignoring\n", __FUNCTION__));
	   /* fixme: why is this safe? */
	   mxfree(handle->car, handle->car->any.length);
	   mxfree(handle, sizeof(cons));
	   return;
       }
#endif
   // fixme:
   // assert (release_p(event) || (key < MAX_KEYCODE && machine->forkActive[key] == 0));

#if DEBUG
   /* describe all the (state x key) -> ? */
   KeySym *sym = XkbKeySymsPtr(xkbi->desc,key);
   if ((!sym) || (! isalpha(* (unsigned char*) sym)))
      sym = (KeySym*) " ";
   MDB(("%s%s%s state: %s, queue: %d, event: %d %s%c %s %s\n",
        info_color,__FUNCTION__,color_reset,
        describe_machine_state(machine),
        queue_lenght(queue),
        key, key_color, (char)*sym, color_reset, event_type_brief(event)));
#endif

   switch (machine->state) {
   case normal:
     /* this is the only state, where the key is the first in the internal_queue. */
       apply_event_to_normal(machine, handle, plugin);
       return;
     /* in other states the key is not the first one, and will not be output! */
   case suspect:
     {     // 2.
	 apply_event_to_suspect(machine, handle, plugin);
	 return;
     }
   case verify:
     {
	 apply_event_to_verify(machine, handle, plugin);
	 return;
     }
   default:
     DB(("----------unexpected state---------\n"));
   }
   //   ErrorF("\n");
   return;
}

// output to stderr.
static void
dump_event(KeyCode key, KeyCode fork, bool press, Time event_time, XkbDescPtr xkb,
	   XkbSrvInfoPtr xkbi, Time prev_time)
{
#if 0
    char* ksname = xkb->names->keys[key].name;
    ErrorF("%d %d %.4s\n",i, key, ksname);

    // 0.1   keysym bound to the key:
    KeySym* sym= XkbKeySymsPtr(xkbi->desc,key); // mmc: is this enough ?
    char* sname = NULL;

    if (sym){
	sname = XkbKeysymText(*sym,XkbCFile); // doesn't work inside server !!

	// my ascii hack
	if (! isalpha(* (unsigned char*) sym)){
	    sym = (KeySym*) " ";
	} else {
	    static char keysymname[15];
	    sprintf(keysymname, "%c", (*sym));
	    sname = keysymname;
	};
    };

    /*  Format:
	keycode
	press/release
	[  57 4 18500973        112
	] 33   18502021        1048
    */

    ErrorF("%s %d (%d)" ,(press?" ]":"[ "),
	   (int)key, (int) fork);
    ErrorF(" %.4s (%5.5s) %d\t%d\n",
	   ksname, sname,
	   event_time,
	   event_time - prev_time);

#endif
}


/* dump on the stderr of the X server. */
/* todo: if i had  machine-> plugin pointer.... */
static void
dump_last_events(PluginInstance* plugin)
{
   machineRec* machine = plugin_machine(plugin);

   // stuff needed in the `while'  cycle:
   DeviceIntPtr keybd = plugin->device;
   XkbSrvInfoPtr xkbi= keybd->key->xkbInfo;
   XkbDescPtr xkb = xkbi->desc;

   MDB(("%s start\n",__FUNCTION__));

#if STATIC_LAST
   int i;

   Time previous_time = 0;
   for(i = 0; i < machine->max_last; i++)
      {
         int index = (i + machine->last_head ) % machine->max_last;

         archived_event* event = machine->last_events + index;

         dump_event(event->key,
                    event->forked,
                    event->press,
                    event->time,
                    xkb, xkbi, previous_time);
         previous_time = event->time;
      }
#else
   /*    if (!handle) */
   if (queue_empty(machine->last_events))
      return;

   cons* handle = machine->last_events.head;

   ErrorF("%d %d\n", machine->max_last, machine->last_events_count);
   InternalEvent* event = handle->car;
   Time previous_time = time_of(event);


   int i = 0;
   while (handle)               // go through the LIST
      {
         /* c++ */
         InternalEvent* event = handle->car; /* bug! */
         handle = handle->cdr;

         dump_event(detail_of(event),
                    handle->forked,
                    (event->u.u.type==KeyPress),
                    time_of(event),
                    xkb, xkbi, previous_time);
         previous_time = time_of(event);
         // increase_ring_index(i, 1);
      };
#endif

   ErrorF("%s end\n",__FUNCTION__);
}



#define final_p(state)  ((state == deactivated) || (state == activated))



/* take from input_queue, + the current_time + force   -> run the machine.
 * after that you have to:   cancel the timer!!!
 */
static void
try_to_play(PluginInstance* plugin, Time current_time, Bool force)
{
   machineRec *machine = plugin_machine(plugin);

   list_with_tail &input_queue = machine->input_queue;

   MDB(("%s: next %s: internal %d, input: %d\n", __FUNCTION__,
	(plugin_frozen(plugin->next)?"frozen":"NOT frozen"),
	queue_lenght(machine->internal_queue),
	queue_lenght(input_queue)));

   // fixme:  no need to malloc & copy:  just use the machine's one.
   //         you pick from left-overs and free 1 position, and possibly re-insert to the queue.
   while (!plugin_frozen(plugin->next)) { // && (left_overs.head)

      if (final_p(machine->state)){        /* forked or normal:  final state */

         /* then, reset the machine */
         MDB(("== Resetting the fork machine\n"));
         //machine->state = normal;
	 change_state(machine,normal);
         machine->verificator = 0;

         if (!queue_empty(machine->internal_queue)) {
	     // this resets/empties the internal queue.
            move_queue_to(machine->internal_queue, input_queue);
            MDB(("now in input_queue: %d\n", queue_lenght(input_queue)));
         }
         /* fixme: [19 ott 05]   */
         // mmc: 2010   assert (input_queue == machine->input_queue);
      };


      cons* handle = pop_from_queue(input_queue, 1);

      if (handle) {
         // do we have some other events to push?
         // if (empty(input_queue)) break; /* `FINISH' */
         // MDB(("== replaying from the queue %d events\t", replay_size));
         /* and feed it. */
         //  increase_modulo_DO(input_queue.head, MAX_EVENTS_IN_QUEUE);
         MDB(("--calling step_fork_automaton_by_key\n"));
         step_fork_automaton_by_key(machine, handle, plugin); /* , time_of(handle->car) */
      } else {
	  // at the end ... add the final time event:
	  if (current_time && (machine->state != normal)){
	      step_fork_automaton_by_time (machine, plugin,  current_time);
	      if (machine->time_left)
		  break; /* `FINISH' */
	  } else if (force && (machine->state != normal)){
	      step_fork_automaton_by_force (machine, plugin);
	  } else break; /* `FINISH' */
	  // didn't help -> break:
	  // error:  consider the time !!
      }
   }
}

/* used only in configure.c ! */
// replay the events on the `internal' queue
void
replay_events(PluginInstance* plugin, Time current_time, Bool force)
   // the 1st event from the queue has just been commited.
{
   machineRec* machine= plugin_machine(plugin);
   assert (machine->lock);

   MDB(("%s\n", __FUNCTION__));

   // internal queue empty -> nothing to replay:
#if 0
   if (! machine->internal_queue.head) {
      assert (! machine->input_queue.head);
      // machine->queue->head = machine->queue->tail = 0; // why?
      machine->state = normal;
      assert (machine->time_left == 0);  // why?
      return;
   }
#endif


   assert(final_p(machine->state));

   /* forked or normal:  final state */
   /* ok, let' init the replaying tool:  */
   /* fixme: we should have a global allocated queue, move the things out of the machine, there */



   //     internal       left_overs
   //    _|XXX|------>|XX replay_size  XX|........
   //                 ^
   //                 replay_head
   //    XXX are events     .... is free space

   if (!queue_empty(machine->internal_queue))
       // prepend and empty. (transfer)
      move_queue_to(machine->internal_queue, machine->input_queue);

   machine->state = normal;

   try_to_play(plugin, current_time, force);
}



/*
 *  react to some `hot_keys':              this extends  xf86PostKbdEvent
 */
inline
int                             // return:  0  nothing  -1  skip it
filter_config_key(PluginInstance* plugin,const InternalEvent *event)
{
   static unsigned char config_mode = 0;
   // != 0   while the Pause key is down, i.e. we are configuring keys:
   static KeyCode key_to_fork = 0;         //  what key we want to configure?
   static Time last_press_time = 0;
   static int latch = 0;

   if (config_mode){
      ErrorF("config_mode\n");
      //   Pause  Pause  -> dump
      //   Pause a shit   ->


      // [21 ott 04]  i admit, that some (non-plain ps/2) keyboard generate the release event
      // at the same time as press
      // So, to overcome this limitation, i detect this short-lasting `down' & take the `next'
      // event as in `config_mode'

      if ((detail_of(event) == PAUSE_KEYCODE) && release_p(event)){ //  fake ?
         if ( (time_of(event) - last_press_time) < 30) // fixme: configurable!
            {
               ErrorF("the key seems buggy, tolerating %lu: %d! .. & latching config mode\n",
		      time_of(event), (int)(time_of(event) - last_press_time));
               latch = 1;
               return -1;
            }
         config_mode = 0;
         key_to_fork = 0;       // forget ...
         ErrorF("dumping %lu: %d!\n", time_of(event), (int)(time_of(event) - last_press_time));
         if (1)                   // nothing configured
            // todo: send a message to listening clients.
            dump_last_events(plugin);
      } else {
         last_press_time = 0;

         if (latch) {
            config_mode = latch = 0;
         };

         if (press_p(event))
	     switch (detail_of(event)) {
            case 110:
            {
               machineRec* machine = plugin_machine(plugin);
               machine->lock = 1;
               dump_last_events(plugin);
               machine->lock = 0;
               break;
            }

            case 19:
            {
               machineRec* machine = plugin_machine(plugin);
               machine->lock = 1;
               machine_switch_config(plugin, machine,0); // current ->toggle ?
               machine->lock = 0;

               /* fixme: but this is default! */
               machine->forkActive[detail_of(event)] = 0; /* ignore the release as well. */
               break;
            }

            // 1
            case 10:
            {
               machineRec* machine = plugin_machine(plugin);

               machine->lock = 1;
               machine_switch_config(plugin, machine,1); // current ->toggle ?
               machine->lock = 0;
               machine->forkActive[detail_of(event)] = 0;
               break;
            }

            default:            /* todo: remove this! */
            {
               if (key_to_fork == 0){
                  key_to_fork = detail_of(event);
               } else {
                  machineRec* machine = plugin_machine(plugin);
                  machine->config->fork_keycode[key_to_fork] = detail_of(event);
                  key_to_fork = 0;
               }
            }};
#if 0
         register BYTE   *kptr;
         KeyCode key = detail_of(event);
         int             bit;      // why not BYTE ??
         kptr = &keybd->key->down[key >> 3];
         bit = 1 << (key & 7);

         if release_p(event)
                        *kptr &= ~bit;          // clear
         else *kptr |= bit;
#endif
         return -1;
      }
   }

   // `Dump'
   if ((detail_of(event) == PAUSE_KEYCODE) && press_p(event))
       /* wait for the next and act ? but start w/ printing the last events: */
      {
         last_press_time = time_of(event);
         ErrorF("entering config_mode & discarding the event: %lu!\n", last_press_time);
         config_mode = 1;
         /* fixme: should I update the ->down bitarray? */
         return -1;
      } else
         last_press_time = 0;
   return 0;
}


// update plugin->wakeup_time
static void
set_wakeup_time(PluginInstance* plugin, Time now)
{
   machineRec* machine = plugin_machine(plugin);
#if USE_LOCKING
   assert(machine->lock);
#endif
   plugin->wakeup_time = (machine->time_left) ? machine->time_left + now:
      (queue_empty(machine->internal_queue))? plugin->next->wakeup_time:0;

   MDB(("%s %s wakeup_time = %u, next wants: %u\n", FORK_PLUGIN_NAME, __FUNCTION__,
	(int)plugin->wakeup_time, (int)plugin->next->wakeup_time));
}


static cons*
create_handle_for_event(InternalEvent *event, bool owner)
{
  // possibly make a copy of the event!
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
   /* fixme: where do i xfree()  the handle?? */
   // fixme: i should have a pool
   cons* handle = (cons*)malloc(sizeof(cons));
   if (!handle) {
      /* This message should be static string. otherwise it fails as well? */
      ErrorF("%s: out-of-memory, dropping \n", __FUNCTION__);
      if (!owner)
         mxfree (qe, event->any.length);
      return NULL;
   };

   memcpy(qe, event, event->any.length);

   DB(("+++ accepted new event: %s\n",
       event_names[event->any.type - 2 ]));

   handle->car = qe;
   handle->cdr = NULL;
   handle->forked = 0;
   return handle;
}


/*  This is the handler for all key events.  Here we delay pushing them forward.
    it's a trampoline for the automaton.
 */
static void
ProcessEvent(PluginInstance* plugin, InternalEvent *event, Bool owner)
{
   DeviceIntPtr keybd = plugin->device;
   Time now = time_of(event);

#if 0
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
	   // memory_balance -= event->any.length;
	   PluginClass(next)->ProcessEvent(next, event, owner);
	   return;
       };
#endif

   if (filter_config_key(plugin, event) < 0)
      {
         if (owner)
            xfree(event);
         // fixme: i should at least push the time of ->next!
         return;
      };

   // fixme, these returns worry me:  we could:
   // *  use the time
   machineRec* machine = plugin_machine(plugin);
#if USE_LOCKING
   assert(machine->lock == 0);
   machine->lock = 1;           // fixme: mouse must not interrupt us.
#endif

   cons* handle = create_handle_for_event(event, owner);
   if (!handle)			// memory problems
     return;

#if KEEP_PREVIOUS
   // we have a pointer to the previous event:
   // so after we have processed an event, we have to keep it. (not free() it)
   // But we keep some events anyway. but then we must not use this pointer.
   // or nullify when free-ing the destination.
   handle->previous = machine->previous_event;
   machine->previous_event = handle;
   /* mmc: [11 Mar 05] seems unused! */
#endif


#if DEBUG
   if (((machineRec*) plugin_machine(plugin))->config->debug) {
      DB(("%s>>> ", key_io_color));
      DB(("%s", describe_key(keybd, handle->car)));
      DB(("%s\n", color_reset));
   }
#endif

   push_on_queue(machine->input_queue, handle);
   try_to_play(plugin, 0, FALSE);


   // fixme: we should take NOW better?
   set_wakeup_time(plugin, now);
   // if internal & output queue is empty
#if USE_LOCKING
   machine->lock = 0;
#endif
};


static void
step_in_time_locked(PluginInstance* plugin, Time now)
{
   machineRec* machine = plugin_machine(plugin);
   MDB(("%s:\n", __FUNCTION__));
   /* is this necessary?   I think not: if the next plugin was frozen, and now it's not, then
    * it must have warned us that it thawed */
   try_to_output(plugin);
   /* push the time ! */
   try_to_play(plugin, now, FALSE); /* dont_force */
   /* !plugin_frozen(plugin->next)   ---> queue_empty(machine->input_queue) */

   /* i should take the minimum of time and the time of the 1st event in the (output) internal queue */
   if (queue_empty(machine->internal_queue) && queue_empty(machine->input_queue)
       && !plugin_frozen(plugin->next))
      {
	machine->lock = 0;
	/* might this be invoked several times?  */
	PluginClass(plugin->next)->ProcessTime(plugin->next, now);
	machine->lock = 1;
      }
   // todo: we could push the time before the first event in internal queue!
   set_wakeup_time(plugin, now);
}


static void
step_in_time(PluginInstance* plugin, Time now)
{
   machineRec* machine = plugin_machine(plugin);
   MDB(("%s:\n", __FUNCTION__));
   machine->lock = 1;
   step_in_time_locked(plugin, now);
   machine->lock = 0;
};


/* called from AllowEvents, after all events from the queue have been pushed: . */
static void
fork_thaw_notify(PluginInstance* plugin, Time now)
{
   machineRec* machine = plugin_machine(plugin);
   MDB(("%s @ time %u\n", __FUNCTION__, (int)now));

   machine->lock = 1;
   /* try_to_output(plugin); */
   step_in_time_locked(plugin, now);

   if (!plugin_frozen(plugin->next) && PluginClass(plugin->prev)->NotifyThaw)
      {
         /* thaw the previous! */
         set_wakeup_time(plugin, now);
         machine->lock = 0;
         MDB(("%s -- sending thaw Notify upwards!\n", __FUNCTION__));
         /* fixme:  Tail-recursion! */
         PluginClass(plugin->prev)->NotifyThaw(plugin->prev, now);
	 /* i could move now to the time of our event. */
      } else {
         MDB(("%s -- NOT sending thaw Notify upwards %s!\n", __FUNCTION__,
	      plugin_frozen(plugin)?"next is frozen":"prev has not NotifyThaw"));
         machine->lock = 0;
      }
}


/* For now this is called to many times, for different events.! */
static void
mouse_call_back (CallbackListPtr *, PluginInstance* plugin,
		 DeviceEventInfoRec* dei)
{
   InternalEvent *event = dei->event;
#if USE_LOCKING
   machineRec* machine = plugin_machine(plugin);
   assert(machine->lock == 0);
   machine->lock = 1;
#endif
   if (event->any.type == ET_Motion)
      {
         if (plugin_machine(plugin) -> lock)
            ErrorF("%s running, while the machine is locked!\n", __FUNCTION__);
         /* else */
         step_fork_automaton_by_force(plugin_machine(plugin), plugin);
      }
#if USE_LOCKING
   machine->lock = 0;
#endif
}


/* we have to make a (new) automaton: allocate default config,
 * register hooks to other devices?
 * prepare timer?
 *
 * returns: erorr of Success. Should attach stuff by side effect ! */
static PluginInstance*
make_machine(DeviceIntPtr keybd, DevicePluginRec* plugin_class)
{
   DB(("%s\n", __FUNCTION__));
   assert (strcmp(plugin_class->name,FORK_PLUGIN_NAME) == 0);



   // is there any other exported malloc-ing macro?
   PluginInstance* plugin = (PluginInstance*) malloc(sizeof(PluginInstance));
   plugin->pclass = plugin_class;
   plugin->device = keybd;

   machineRec* forking_machine;

   /*   ErrorF("%s: got %d sizeof: %d %d\n", __FUNCTION__, (int) keybd->plugin->handle,
    *   sizeof(DeviceIntRec_plugin), sizeof(DeviceIntRec)); */

   // i create 2 config sets.  1 w/o forking.
   // They are numbered:  0  is the no-op.
   //
   fork_configuration* config_no_fork = machine_new_config(); // configuration number 0
   config_no_fork->debug = 0;   // should be settable somehow.
   if (!config_no_fork)
      {
         return NULL;              // BadAlloc
      }

   fork_configuration* config = machine_new_config();
   if (!config)
      {
         free (config_no_fork);
         /* free (forking_machine); */
         return NULL;              // BadAlloc
      }

   config->next = config_no_fork;
   // config->id = config_no_fork->id + 1;
   // so we start w/ config 1. 0 is empty and should not be modifyable


   // i should use  cout <<  to avoid the segfault if something is not a string.
   ErrorF("%s: constructing the machine %d (official release: %s)\n",
	  __FUNCTION__, PLUGIN_VERSION, VERSION_STRING); // , VERSION

   /* state: */
   forking_machine =  (machineRec* )mmalloc(sizeof(machineRec));
   bzero(forking_machine, sizeof (machineRec));

   if (! forking_machine){
      ErrorF("%s: malloc failed (for forking_machine)\n",__FUNCTION__);
      return NULL;              // BadAlloc
   }
   // now, if something goes wrong, we have to free it!!


   /* make a `queue':  */
   init_queue(forking_machine->internal_queue, "internal");
   init_queue(forking_machine->input_queue, "input");
   init_queue(forking_machine->output_queue, "output");

   forking_machine->max_last = 100;
#if STATIC_LAST
   forking_machine->last_events = (archived_event*) mmalloc(sizeof(archived_event) * forking_machine->max_last);
   bzero (forking_machine->last_events, sizeof(archived_event) * forking_machine->max_last);
   forking_machine->last_head = 0;
#else
   forking_machine->last_events_count = 0;
   init_queue(forking_machine->last_events, "last");
#endif


   forking_machine->state = normal;

   forking_machine->lock = 0;
   forking_machine->time_left = 0;


   int i;
   for (i=0;i<256;i++){         /* 1 ? */
      forking_machine->forkActive[i] = 0; /* 0 = not active */
   };

   forking_machine->config = config;

#if KEEP_PREVIOUS
   forking_machine->previous_event = (cons*)mmalloc(sizeof(cons));
   // bug:
   forking_machine->previous_event->car = (InternalEvent*) mmalloc(sizeof(InternalEvent));
   forking_machine->previous_event->car->u.keyButtonPointer.time = 0; // let's hope:
#else
   forking_machine->time_of_last_output = 0;
#endif

   plugin->data = (void*) forking_machine;
   ErrorF("%s: returning %d\n", __FUNCTION__, Success);

#if 1
   AddCallback(&DeviceEventCallback, (CallbackProcPtr) mouse_call_back, (void*) plugin);
#endif


   plugin_class->ref_count++;
   return plugin;
};


static int
stop_and_exhaust_machine(PluginInstance* plugin)
{
   /* we have to push all releases at least. and all forked ... */

   machineRec* machine = plugin_machine(plugin);
   machine->lock = 1;

   MDB(("%s: what to do?\n", __FUNCTION__));
   // free all the stuff, and then:
   xkb_remove_plugin (plugin);
   return 1;

}


static int
destroy_machine(PluginInstance* plugin)
{
   machineRec* machine = plugin_machine(plugin);
   machine->lock = 1;

   DeleteCallback(&DeviceEventCallback, (CallbackProcPtr) mouse_call_back, (void*) plugin);

   MDB(("%s: what to do?\n", __FUNCTION__));
   return 1;
}




/* fixme:  where is the documentation: fork_requests.h ? */
static int
machine_configure_twins (machineRec* machine, int type, KeyCode key, KeyCode twin, int value, Bool set)
{
   switch (type) {

   case fork_configure_total_limit:
      if (set)
         machine->config->verification_interval_per_key[key][twin] = value;
      else
         return machine->config->verification_interval_per_key[key][twin];

      break;
   case fork_configure_overlap_limit:
      if (set)
         machine->config->overlap_tolerance_per_key[key][twin] = value;
      else return machine->config->overlap_tolerance_per_key[key][twin];
      break;
   }
   return 0;
}



static int
machine_configure_key (machineRec* machine, int type, KeyCode key, int value, Bool set)
{
   MDB(("%s: keycode %d -> value %d, function %d\n", __FUNCTION__, key, value, type));

   switch (type)
      {
      case fork_configure_key_fork:
         if (set)
            machine->config->fork_keycode[key] = value;
         else return machine->config->fork_keycode[key];
         break;
      case fork_configure_key_fork_repeat:
         if (set)
            machine->config->fork_repeatable[key] = value;
         else return machine->config->fork_repeatable[key];
         break;
      }
   return 0;
}


static int
machine_configure_global (PluginInstance* plugin, machineRec* machine, int type, int value, Bool set)
{
   switch (type){
   case fork_configure_overlap_limit:
      if (set)
         machine->config->verification_interval = value;
      else
         return machine->config->verification_interval;
      break;

   case fork_configure_total_limit:
      if (set)
         machine->config->verification_interval = value;
      else return machine->config->verification_interval;
      break;

   case fork_configure_clear_interval:
      if (set)
         machine->config->clear_interval = value;
      else return machine->config->clear_interval;
      break;

   case fork_configure_repeat_limit:
      if (set)
         machine->config->repeat_max = value;
      else return machine->config->repeat_max;
      break;


   case fork_configure_repeat_consider_forks:
      if (set)
         machine->config->consider_forks_for_repeat = value;
      return machine->config->consider_forks_for_repeat;
      break;


   case fork_configure_last_events:
      if (set)
         machine_set_last_events_count(machine, value);
      else
         return machine->max_last;
      break;

   case fork_configure_debug:
      if (set)
         {
            //  here we force, rather than using MDB !
            DB(("fork_configure_debug set: %d -> %d\n", machine->config->debug, value));
            machine->config->debug = value;
         }
      else
         {
            MDB(("fork_configure_debug get: %d\n", machine->config->debug));
            return machine->config->debug; // (Bool) ?True:FALSE
         }

      break;

   case fork_server_dump_keys:
      dump_last_events(plugin);
      break;

      // mmc: this is special:
   case fork_configure_switch:
      assert (set);

      MDB(("fork_configure_switch: %d\n", value));
      machine_switch_config(plugin, machine, value);
      return 0;
   }

   return 0;
}


// todo: make it inline functions
#define subtype_n_args(t)   (t & 3)
#define type_subtype(t)     (t >> 2)


/* Return a value requested, or 0 on error.*/
static int
machine_configure_get(PluginInstance* plugin, int values[5], int return_config[3])
{
   assert (strcmp (PLUGIN_NAME(plugin), FORK_PLUGIN_NAME) == 0);

   machineRec* machine = plugin_machine(plugin);

   int type = values[0];

   /* fixme: why is type int?  shouldn't CARD8 be enough?
      <int type>
      <int keycode or time value>
      <keycode or time value>
      <timevalue>

      type: local & global
   */

   MDB(("%s: %d operands, command %d: %d %d\n", __FUNCTION__, subtype_n_args(type), type_subtype(type),
        values[1], values[2]));

   switch (subtype_n_args(type)){
   case 0:
           return_config[0]= machine_configure_global(plugin, machine, type_subtype(type), 0, 0);
           break;
   case 1:
           return_config[0]= machine_configure_key(machine, type_subtype(type), values[1], 0, 0);
           break;
   case 2:
           return_config[0]= machine_configure_twins(machine, type_subtype(type), values[1], values[2], 0, 0);
           break;
   case 3:
           return 0;
   }
   return 0;
}


/* Scan the DATA (of given lenght), and translate into configuration commands, and execute on plugin's machine */
static int
machine_configure(PluginInstance* plugin, int values[5])
{
   assert (strcmp (PLUGIN_NAME(plugin), FORK_PLUGIN_NAME) == 0);

   machineRec* machine = plugin_machine(plugin);

   int type = values[0];
   MDB(("%s: %d operands, command %d: %d %d %d\n", __FUNCTION__, subtype_n_args(type), type_subtype(type),
        values[1], values[2],values[3]));

   /* fixme: why is type int?  shouldn't CARD8 be enough?
      <int type>
      <int keycode or time value>
      <keycode or time value>
      <timevalue>

      type: local & global
   */

   switch (subtype_n_args(type)) {
   case 0:
      machine_configure_global(plugin, machine, type_subtype(type), values[1], 1);
      break;

   case 1:
      machine_configure_key(machine, type_subtype(type), values[1], values[2], 1);
      break;

   case 2:
      machine_configure_twins(machine, type_subtype(type), values[1], values[2], values[3], 1);
   case 3:
      // special requests ....
      break;
   }
   /* return client->noClientException; */
   return 0;
}


/*todo: int*/
static void
machine_command(ClientPtr client, PluginInstance* plugin, int cmd, int data1,
		int data2, int data3, int data4)
{
   DB(("%s cmd %d, data %d ...\n", __FUNCTION__, cmd, data1));
   switch (cmd)
      {
      case fork_client_dump_keys:
         /* DB(("%s %d %.3s\n", __FUNCTION__, len, data)); */
         dump_last_events_to_client(plugin, client, data1);
         break;
      default:
         DB(("%s Unknown command!\n", __FUNCTION__));
         break;
         /* What XReply to send?? */
      }
}



static pointer /*DevicePluginRec* */
fork_plug(pointer	options,
          int		*errmaj,
          int		*errmin)
{
    DB(("%s\n", __FUNCTION__));
    static struct _DevicePluginRec plugin_class =
	{
#if __GNUC__
	  name :        FORK_PLUGIN_NAME,
	  instantiate : make_machine,
	  ProcessEvent : ProcessEvent,
	  ProcessTime :  step_in_time,
	  NotifyThaw : fork_thaw_notify,

	  config :    machine_configure,
	  getconfig : machine_configure_get,
	  client_command : machine_command,
	  module: NULL,
	  ref_count: 0,
	  stop :       stop_and_exhaust_machine,
	  terminate :  destroy_machine
#else
	  FORK_PLUGIN_NAME,
	  make_machine,
	  ProcessEvent,
	  step_in_time,
	  fork_thaw_notify,
	  // module ?
	  machine_configure,
	  machine_configure_get,

	  machine_command,

	  // stop ? exhaust
	  NULL, 0,
	  stop_and_exhaust_machine,
	  destroy_machine,
#endif
	};

    plugin_class.ref_count = 0;
    xkb_add_plugin(&plugin_class);

    return &plugin_class;
}

extern "C" {

void __attribute__((constructor))  on_init()
{
    ErrorF("%s:\n", __FUNCTION__); /* impossible */
    fork_plug(NULL,NULL,NULL);
}
}

#include "config.h"
#include "debug.h"

#include "history.h"
#include "fork.h"
#include "fork_requests.h"

extern "C" {
#include <X11/X.h>
#include <X11/Xproto.h>
#include <X11/keysym.h>

/* `probably' I use it only to print out the keysym in debugging stuff*/
#include <xorg/xkbsrv.h>
#include <xorg/eventstr.h>
}



// fix-size-deque


/* We want to keep a history:
   We might have a circular-vector to keep the events
*/

// reallocate 
int
machine_set_last_events_count(machineRec* machine, int new_max) // fixme:  lock ??
{
   DB(("%s: allocating %d events\n",__FUNCTION__, new_max));

#if STATIC_LAST
   /*   XXXXXXXX. YYYYYYYYYYYYY
    * ->  YYYYYYYYYYYYY XXXXXXXXX.
    *
    *   */
   archived_event* newl = (archived_event*) mmalloc(sizeof(archived_event) * new_max);
   
   
   if (machine->max_last <= new_max)
      {
         /* growing: */

         int above_len = (machine->max_last - machine->last_head);

         DB(("growing: above %d\n", above_len));
         /* YYYYY  */
         memcpy(newl, machine->last_events + machine->last_head,
                above_len * sizeof(archived_event));

         DB(("growing: below: %d\n", machine->last_head));
         /*   XXXXXX -> */
         memcpy(newl + above_len, machine->last_events,
                machine->last_head * sizeof(archived_event));

         DB(("bzero new stuff: %d\n", (new_max - machine->max_last)));
         bzero(newl + machine->max_last, (new_max - machine->max_last) *
               sizeof(archived_event));

         machine->last_head = machine->max_last;
      }
   else
      {
         /* todo: */
         /* Shrink!       XXXXXX  YYYYY  ->  XXXX  or  YY XXXXXX*/

         if (machine->last_head >= new_max){
            memcpy(newl, machine->last_events + (machine->last_head - new_max),
                   new_max * sizeof(archived_event));
         } else {
            memcpy(newl + (new_max - machine->last_head),
                   /* ___ XXXXXXXXXX */
                   machine->last_events,
                   machine->last_head * sizeof(archived_event));


            /*  */
            memcpy(newl,
                   /* YYY __________ */
                   machine->last_events + (machine->max_last -
                                           (new_max - machine->last_head)),

                   (new_max - machine->last_head) * sizeof(archived_event));
         } 
         machine->last_head = 0;
      }

   mxfree(machine->last_events, machine->max_last * sizeof(archived_event));

   machine->last_events = newl;
   machine->max_last = new_max;
   
#else
   if (machine->max_last <= new_max)
      {
         if (machine->last_events_count > machine->max_last)
            machine->last_events_count = machine->max_last;
         machine->max_last = new_max;
      }
   else
      {
         int i;
         // we have to free the ...
         for (i= 1; i < (machine->max_last -  new_max); i++)
            {
               key_event* old = pop_from_queue(machine->last_events, 0); // q.head = old->cdr;
               xfree(old);
            }

         DB(("%s: truncating after %d events\n",__FUNCTION__, machine->last_events_count));
         machine->last_events_count = machine->max_last = new_max;;
      }
#endif
   return 0;
}


/* ---------------------
 * returns the message to send as Xreply, len is filled w/ the lenght.
 * lenght <0 -> error!
 *
 * or ?? just invoke `plugin_send_reply'(ClientPtr client, char* message, int lenght)
 * --------------------
 */
int
dump_last_events_to_client(PluginInstance* plugin, ClientPtr client, int n)
{

   machineRec* machine = plugin_machine(plugin);

   int queue_count = machine->max_last;           // I don't need to count them! last_events_count

#if 0
   if (queue_count > machine->max_last)
      queue_count = machine->max_last;
#endif
   
   // how many in the store?
   // upper bound
   if (n > queue_count) {
      n =  queue_count;
   };


      
   // allocate the appendix buffer:
   int appendix_len = sizeof(fork_events_reply) + (n * sizeof(archived_event));
   /* no alignment! */

   /* fork_events_reply; */

#if 0 /* useless? */
   int remainder = appendix_len  % 4; 
   appendix_len += (remainder?(4 - remainder):0);
/* endi */
#endif


   char *start;
   fork_events_reply* buf;
   
   start = (char *)alloca(appendix_len);
   buf = (fork_events_reply*) start;

   buf->count = n;              /* fixme: BYTE SWAP if needed! */
   
#if STATIC_LAST
   int i;
   for (i = 0; i < n; i++)
      {
         /* todo: i should memcpy an entire block. But i'm going backwards !  */
         int index = (machine->last_head -1 - i) % machine->max_last;
         if (index < 0)
            index += machine->max_last;
         
            
         DB(("%d/(%d): %d\t%d (%lu)\n", i, index, machine->last_events[index].key,
             machine->last_events[index].forked,
             machine->last_events[index].time));

         memcpy (buf->e + i, machine->last_events + index, sizeof(archived_event));
      }
#else
   key_event* handle = queue_skip(machine->last_events, (queue_count - n));
   DB(("%s requested: %d  provided: %d\n",__FUNCTION__, stuff->count, n));
 
   // n, no need to test for NULL cdr
   while (handle)
      {
         memcpy ((char*)buf, handle->car, sizeof(xEvent));
         buf += sizeof(xEvent);
         handle = handle->cdr;
      }

#endif /* STATIC_LAST */
   DB(("sending %d events: + %d!\n", n, appendix_len));

   int r =  xkb_plugin_send_reply(client, plugin, start, appendix_len);
   if (r == 0)
      return client->noClientException;
   return r;
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
void
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
   if (machine->last_events.empty())
      return;

   key_event* handle = machine->last_events.front();

   ErrorF("%d %d\n", machine->max_last, machine->last_events_count);
   InternalEvent* event = handle->event;
   Time previous_time = time_of(event);


   // todo: replace with  map for_each ...
#if 0
   int i = 0;
   while (handle)               // go through the LIST
      {
         /* c++ */
         InternalEvent* event = handle->event; /* bug! */
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
#endif

   ErrorF("%s end\n",__FUNCTION__);
}

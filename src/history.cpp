/*
   We want to keep a history (most recent key events):
*/

#include "config.h"
#include "debug.h"

#include "history.h"
#include "fork.h"
#include "fork_requests.h"


extern "C" {
#include <xorg-server.h>
#include <xorg/xkbsrv.h>


#include <X11/X.h>
#include <X11/Xproto.h>
#include <X11/keysym.h>

/* `probably' I use it only to print out the keysym in debugging stuff*/
#include <xorg/eventstr.h>
}

#include "event_ops.h"

#define TIME_FMT  "u"
#define SIZE_FMT  "lu"


archived_event*
make_archived_events (key_event* ev)
{
  archived_event* event = MALLOC(archived_event);

  event->key = detail_of(ev->event);
  event->time = time_of(ev->event);
  event->press = press_p(ev->event);
  event->forked = ev->forked;

  return event;
}



int
machine_set_last_events_count(machineRec* machine, int new_max) // fixme:  lock ??
{
  DB(("%s: allocating %d events\n",__FUNCTION__, new_max));

  if (machine->max_last > new_max)
    {
      // shrink. to be done in the circular.h!
    }
  else
    {
      machine->last_events->reserve(new_max);
    }

  machine->max_last = new_max;
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

#if 0
   // fixme: we need to increase an iterator .. pointer .... to the C array!
   last_events.for_each(
                        begin(),
                        end(),
                        function);
#endif

   DB(("sending %d events: + %d!\n", n, appendix_len));

   int r =  xkb_plugin_send_reply(client, plugin, start, appendix_len);
   if (r == 0)
      return client->noClientException;
   return r;
}


// prints in the Xorg.n.log
static void
dump_event(KeyCode key, KeyCode fork, bool press, Time event_time, XkbDescPtr xkb,
	   XkbSrvInfoPtr xkbi, Time prev_time)
{
    char* ksname = xkb->names->keys[key].name;
    ErrorF("%d %.4s\n", key, ksname);

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
    ErrorF(" %.4s (%5.5s) %" TIME_FMT "\t%" TIME_FMT "\n",
	   ksname, sname,
	   event_time,
	   event_time - prev_time);
}


// Closure
class event_dumper
{
private:
  DeviceIntPtr keybd;
  XkbSrvInfoPtr xkbi;
  XkbDescPtr xkb;

  int index;
  Time previous_time;

public:
  void operator() (archived_event*& event)
  {
    dump_event(event->key,
               event->forked,
               event->press,
               event->time,
               xkb, xkbi, previous_time);
    previous_time = event->time;
  };


  event_dumper(PluginInstance* plugin, int i = 0) : index(i), previous_time(0)
  {
    keybd = plugin->device;
    xkbi = keybd->key->xkbInfo;
    xkb = xkbi->desc;
  };
};


void
dump_last_events(PluginInstance* plugin)
{
  machineRec* machine = plugin_machine(plugin);
  ErrorF("%s(%s) %" SIZE_FMT "\n",__FUNCTION__, plugin->device->name,
         machine->last_events->size());

  event_dumper function(plugin);
  for_each(machine->last_events->begin(),
           machine->last_events->end(),
           function);
}

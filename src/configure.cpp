
#include "fork.h"

#define DEBUG 1
#include "debug.h"

#include "fork_requests.h"

/* something to define NULL */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <xorg/misc.h>

//   
//  pointer->  |  next|-> |   next| ->
//    ^            ^
//    |    or      |
//  return a _pointer_ to the pointer  on a searched-for item.

static fork_configuration**
find_before_n(machineRec* machine, int n)
{
   fork_configuration** config_p = &(machine->config);
   
   while (((*config_p)->next) && ((*config_p)->id != n))
      {
         ErrorF("%s skipping over %d\n", __FUNCTION__, (*config_p)->id);
         config_p = &((*config_p) -> next);
      }
   return ((*config_p)->id == n)? config_p: NULL;      // ??? &(config->next);
}


void
machine_switch_config(PluginInstance* plugin, machineRec* machine,int id)
{

   ErrorF("%s %d\n", __FUNCTION__, id);

   fork_configuration** config_p = find_before_n(machine, id);
   /* (device_machine(dev))->config; */

   ErrorF("%s found\n", __FUNCTION__);

   // fixme:   `move_to_top'   find an element in a linked list, and move it to the head.
   /*if (config->id == id)   
     no need for replay!
   */


   if ((config_p) && (*config_p) && (*config_p != machine->config))           // found
      {
         // change it:
         //   |machine|  -> 1 k.... n->-> n -> n+1

         //   |machine|  -> n 1 k2....n-1 -> n+1

         DB(("switching configs %d -> %d\n", machine->config->id, id));

         fork_configuration* new_current = *config_p;


         //fixme:  this sequence works at the beginning too!!!

         // remove from the list:
         *config_p = new_current->next; //   n-1 -> n + 1

         // reinsert at the beginning:
         new_current->next = machine->config; //    n -> 1
         machine->config = new_current; //     -> n

         DB(("switching configs %d -> %d\n", machine->config->id, id));
         replay_events(plugin, 0, FALSE);
      } else
         {
            ErrorF("config remains %d\n", machine->config->id);
         }
   // ->debug = (stuff->value?True:False); // (Bool)
}


static int config_counter = 0;


// nothing active (forkable) in this configuration
fork_configuration*
machine_new_config(void) 
{
   fork_configuration* config;      
   
   /* `configuration' */
   ErrorF("resetting the configuration to defaults\n");
   config = MALLOC(fork_configuration);

   
   if (! config){
      ErrorF("%s: malloc failed (for config)\n", __FUNCTION__);
      /* fixme: should free the machine!!! */
      /* in C++ exception which calls all destructors (of the objects on the stack ?? */
      return NULL;
   }
   
   config->verification_interval = 200; /* ms: could be XkbDfltRepeatDelay */
   config->overlap_tolerance = 100;
   config->repeat_max = 80;
   config->consider_forks_for_repeat = TRUE;
   config->debug = 1;        //  2
   config->clear_interval = 0;
   
   {
      int i;
      
      for (i=0;i<256;i++){         /* 1 ? */

         // local timings:  0 = use global timing
         int j;
         for (j=0;j<256;j++){         /* 1 ? */
            config->overlap_tolerance_per_key[i][j] = 0;
            config->verification_interval_per_key[i][j] = 0;
         };


         config->fork_keycode[i] = 0;
         /*  config->forkCancel[i] = 0; */

         config->fork_repeatable[i] = FALSE; /* repetition is supported by default (not ignored)  True False*/
      }
      ErrorF("fork: init arrays .... done\n");
   }

   config->name = "default";
   config->id = config_counter++;
   config->next = NULL;
   return config;
}


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
         memcpy(newl + above_len, machine->last_events, machine->last_head * sizeof(archived_event));

         DB(("bzero new stuff: %d\n", (new_max - machine->max_last)));
         bzero(newl + machine->max_last, (new_max - machine->max_last) * sizeof(archived_event));

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
               cons* old = pop_from_queue(machine->last_events, 0); // q.head = old->cdr;
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

   int queue_count = machine->max_last;           // i don't need to count them! last_events_count

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
   int appendix_len = sizeof(fork_events_reply) + (n * sizeof(archived_event)); /* no alignment! */

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
   cons* handle = queue_skip(machine->last_events, (queue_count - n));
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



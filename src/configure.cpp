
// in this file: processing X11 requests to configure the plugin.

#include "config.h"
#include "debug.h"

#include "configure.h"
#include "fork.h"
#include "fork_requests.h"
#include "history.h"

/* something to define NULL */
extern "C"
{
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <xorg/misc.h>
}

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
        replay_events(plugin, FALSE);
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

   config->repeat_max = 80;
   config->consider_forks_for_repeat = TRUE;
   config->debug = 1;        //  2
   config->clear_interval = 0;

   // use bzero!
   for (int i=0;i<256;i++) {
       // local timings:  0 = use global timing
       for (int j=0;j<256;j++){         /* 1 ? */
           config->overlap_tolerance[i][j] = 0;
           config->verification_interval[i][j] = 0;
       };

       config->fork_keycode[i] = 0;
       /*  config->forkCancel[i] = 0; */
       config->fork_repeatable[i] = FALSE;
       /* repetition is supported by default (not ignored)  True False*/
   }
   /* ms: could be XkbDfltRepeatDelay */

   config->verification_interval[0][0] = 200;
   config->overlap_tolerance[0][0] = 100;
   ErrorF("fork: init arrays .... done\n");


   config->name = "default";
   config->id = config_counter++;
   config->next = NULL;
   return config;
}





/* fixme:  where is the documentation: fork_requests.h ? */
static int
machine_configure_twins (machineRec* machine, int type, KeyCode key, KeyCode twin,
                         int value, Bool set)
{
   switch (type) {

   case fork_configure_total_limit:
      if (set)
         machine->config->verification_interval[key][twin] = value;
      else
         return machine->config->verification_interval[key][twin];

      break;
   case fork_configure_overlap_limit:
      if (set)
         machine->config->overlap_tolerance[key][twin] = value;
      else return machine->config->overlap_tolerance[key][twin];
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
machine_configure_global (PluginInstance* plugin, machineRec* machine, int type,
                          int value, Bool set)
{
   switch (type){
   case fork_configure_overlap_limit:
      if (set)
         machine->config->verification_interval[0][0] = value;
      else
         return machine->config->verification_interval[0][0];
      break;

   case fork_configure_total_limit:
      if (set)
         machine->config->verification_interval[0][0] = value;
      else return machine->config->verification_interval[0][0];
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
            DB(("fork_configure_debug set: %d -> %d\n", machine->config->debug,
                value));
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
int
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

   MDB(("%s: %d operands, command %d: %d %d\n", __FUNCTION__, subtype_n_args(type),
        type_subtype(type), values[1], values[2]));

   switch (subtype_n_args(type)){
   case 0:
           return_config[0]= machine_configure_global(plugin, machine,
                                                      type_subtype(type), 0, 0);
           break;
   case 1:
           return_config[0]= machine_configure_key(machine, type_subtype(type),
                                                   values[1], 0, 0);
           break;
   case 2:
           return_config[0]= machine_configure_twins(machine, type_subtype(type),
                                                     values[1], values[2], 0, 0);
           break;
   case 3:
           return 0;
   }
   return 0;
}


/* Scan the DATA (of given length), and translate into configuration commands,
   and execute on plugin's machine */
int
machine_configure(PluginInstance* plugin, int values[5])
{
   assert (strcmp (PLUGIN_NAME(plugin), FORK_PLUGIN_NAME) == 0);

   machineRec* machine = plugin_machine(plugin);

   int type = values[0];
   MDB(("%s: %d operands, command %d: %d %d %d\n", __FUNCTION__,
        subtype_n_args(type), type_subtype(type),
        values[1], values[2],values[3]));

   switch (subtype_n_args(type)) {
   case 0:
      machine_configure_global(plugin, machine, type_subtype(type), values[1], 1);
      break;

   case 1:
      machine_configure_key(machine, type_subtype(type), values[1], values[2], 1);
      break;

   case 2:
      machine_configure_twins(machine, type_subtype(type), values[1], values[2],
                              values[3], 1);
   case 3:
      // special requests ....
      break;
   }
   /* return client->noClientException; */
   return 0;
}


/*todo: int*/
void
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

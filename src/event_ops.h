// 

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

// (printable) Name of the event
inline const char*
event_type_brief(InternalEvent *event)
{
    return (press_p(event)?"down":
	    (release_p(event)?"up":"??"));
}

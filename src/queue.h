#ifndef _QUEUE_H_
#define	_QUEUE_H_ 1


#include "debug.h"

#include <ext/slist>
#include <iterator>
#include <iostream>
#include <string>
#include <algorithm>



extern "C" {
   // #define XKB_IN_SERVER 1
#include <X11/X.h>
#include <X11/Xproto.h>
#include <X11/Xdefs.h>

#include <assert.h>
#include <stdint.h>
#include <xorg/input.h>
#include <xorg/eventstr.h>
}


using namespace std;
using namespace __gnu_cxx;


template <typename T>
class my_queue
{
  private:
   slist<T*> list;
   const char* m_name;     // string
   typename slist<T*>::iterator last_node;

  public:
   const char* get_name() 
   {
      return m_name;// .c_str();
   }

   // take ownership
   void set_name(const char* name)
   {
      m_name = name; //string(name);
   }

   int length ();

   bool empty () const;
   // const_reference
   const T* front () const;
   T* pop();     // list_with_tail &queue, int empty_ok)
   void push(T* element); //list_with_tail &queue, cons *handle);
   void push (const T& value);
   // T nth(int n) const;    // list_with_tail &queue,

   /* move the content of FROM to the beginning of TO (hence make FROM empty) */
   /*      from      to          to = result    from -> ()
    *     xxxxxxx   yyyyy   ->   xxxxyyyy
    */
   // extern void  slice_queue(list_with_tail &from, list_with_tail &to);
   void slice (my_queue<T>&);
   //extern void init_queue(list_with_tail &queue, const char* name);

   // c-tor:
   my_queue<T>(const char* name) : m_name(name)
   {
      last_node = list.end();
   };
   void swap (my_queue<T>& peer)
   {
      list.swap(peer.list);
#if 0
      slist<T*>::iterator tmp;
      tmp=last_node;
      last_node=peer.last_node;
      peer.last_node = tmp;
#else
      iter_swap(last_node,peer.last_node); // std::swap()
#endif
      DB(("SWAP\n"));
      DB(("%s now has %d\n", get_name(), length()));
      DB(("%s now has %d\n", peer.get_name(), peer.length()));
   }
};



template<typename T>
int my_queue<T>::length ()
{
   typename slist<T*>::iterator i = list.begin();
   //cons *iterator = queue.head;
   // list.begin();
   int len = 0;
   while (i != list.end()) {
      len++;
      i++;// = iterator->cdr;
   }
   return len;
};

template<typename T>
bool my_queue<T>::empty () const
{
   return (list.empty());
}



template<typename T>
void my_queue<T>::push (T* value)
{
   DB(("%s: %s: now %d + 1\n", __FUNCTION__, get_name(), length()));

   // empty()
   // cerr << "push: new pointer: " << *value << "\n";
   if (!empty ()) {                // last_node != list.end()
      last_node = list.insert_after(last_node, value);
   } else {
      list.push_front(value);
      last_node = list.begin();
   }
}

template<typename T>
void my_queue<T>::push (const T& value)
{

   T* clone = new T(value);
   //cerr << "push: new value: " << value << "\n";
   push(clone);
}


template<typename T>
T* my_queue<T>::pop ()
{

   T* pointer = list.front();
   list.pop_front();
   return pointer;
}



template<typename T>
const T* my_queue<T>::front () const
{
   return list.front();
}


template<typename T>
void my_queue<T>::slice (my_queue<T> &suffix)
{
   DB(("%s: %s: appending/moving all from %s:\n", __FUNCTION__, get_name(),
       suffix.get_name()));
   // if (empty())
    
   list.splice_after(last_node,
                     //suffix.list.begin(),
                     //suffix.last_node
                     suffix.list);
   last_node=suffix.last_node;

   DB(("%s now has %d\n", get_name(), length()));
   DB(("%s now has %d\n", suffix.get_name(), suffix.length()));

   //suffix.list = 0;
   // return list.front();
}



#endif

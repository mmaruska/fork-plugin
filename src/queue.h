#ifndef _QUEUE_H_
#define	_QUEUE_H_ 1


// #include "debug.h"

#include <ext/slist>
#include <iterator>
#include <iostream>
#include <string>
#include <algorithm>


using namespace std;
using namespace __gnu_cxx;


/* LIFO
   + slice operation,

   Memory/ownerhip:
   the implementing container(list) handles pointers to objects.
   The objects are owned by the application!
   if a Reference is push()-ed, we make a Clone! But we never delete the objects.
   pop() returns the pointer!
*/
template <typename T>
class my_queue
{
private:
    slist<T*> list;
    const char* m_name;     // for debug string
    typename slist<T*>::iterator last_node;

public:
    const char* get_name()
        {
            return m_name?:"(unknown)";// .c_str();
        }
    // take ownership
    void set_name(const char* name)
        {
            m_name = name;
        }

    int length() const;

    bool empty() const;

    const T* front () const;
    T* pop();                    // top_and_pop()

    void push(T* element);
    void push(const T& value);   // we clone the value!

    /* move the content of appendix to the END of this queue
     *      this      appendix    this        appendix
     *     xxxxxxx   yyyyy   ->   xxxxyyyy       (empty)
     */
    void slice (my_queue<T>& appendix);

    ~my_queue()
        {
            if (m_name)
            {
                m_name = NULL;
            }
        }
    my_queue<T>(const char* name = NULL) : m_name(name)
        {
            DB(("constructor\n"));
            last_node = list.end();
        };

    void swap (my_queue<T>& peer)
        {
            typename slist<T*>::iterator temp;
            temp = last_node;

            list.swap(peer.list);
            
            // iter_swap(last_node,peer.last_node);
            if (list.empty())
                last_node = list.begin();
            else {
                last_node = peer.last_node;
            }

            if (peer.list.empty())
                peer.last_node = peer.list.begin();
            else
                peer.last_node = temp;
        }
};



template<typename T>
int my_queue<T>::length () const
{
    return list.size();
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
    if (!empty ()) {
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
    push(clone);
}


template<typename T>
T* my_queue<T>::pop ()
{
    T* pointer = list.front();

    list.pop_front();
    // invalidate iterators
    if (list.empty())
        last_node = list.begin();
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

    if (! suffix.list.empty())
    {
        list.splice_after(last_node,
                          suffix.list);
        last_node=suffix.last_node;
    }
    DB(("%s now has %d\n", get_name(), length()));
    DB(("%s now has %d\n", suffix.get_name(), suffix.length()));
}


#endif

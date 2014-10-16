/*************************************************************************\
* Copyright (c) 2014 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#ifndef CBLIST_H
#define CBLIST_H

#include <list>
#include <map>
#include <typeinfo>

#include <errlog.h>

template<typename T>
class CBList
{
public:
    typedef void (*func_t)(void*, T*);
private:
    typedef std::pair<void*,func_t> value_type;
    typedef std::list<value_type> list_t;
    list_t thelist;
public:
    CBList():thelist(){};
    void add(func_t fn, void* user){thelist.push_back(std::make_pair(user,fn));}
    void del(func_t fn, void* user){thelist.remove(std::make_pair(user,fn));}

    void operator()(T* obj) {
        typename list_t::const_iterator it, end=thelist.end();
        for(it=thelist.begin(); it!=end; ++it) {
            try {
                (*it->second)(it->first, obj);
            } catch(std::exception& e) {
                errlogPrintf("Exception in CBList<%s>(%p) '%s'\n",
                             typeid(this).name(), (void*)obj, e.what());
            }
        }
    }
};

#endif // CBLIST_H

// -*- C++ -*-
//
// Copyright 1997-2000, University of Notre Dame.
// Authors: Jeremy G. Siek, Jeffery M. Squyres, Michael P. McNally, and
//          Andrew Lumsdaine
// 
// This file is part of the Notre Dame C++ bindings for MPI.
// 
// You should have received a copy of the License Agreement for the Notre
// Dame C++ bindings for MPI along with the software; see the file
// LICENSE.  If not, contact Office of Research, University of Notre
// Dame, Notre Dame, IN 46556.
// 
// Permission to modify the code and to distribute modified code is
// granted, provided the text of this NOTICE is retained, a notice that
// the code was modified is included with the above COPYRIGHT NOTICE and
// with the COPYRIGHT NOTICE in the LICENSE file, and that the LICENSE
// file is distributed with the modified code.
// 
// LICENSOR MAKES NO REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED.
// By way of example, but not limitation, Licensor MAKES NO
// REPRESENTATIONS OR WARRANTIES OF MERCHANTABILITY OR FITNESS FOR ANY
// PARTICULAR PURPOSE OR THAT THE USE OF THE LICENSED SOFTWARE COMPONENTS
// OR DOCUMENTATION WILL NOT INFRINGE ANY PATENTS, COPYRIGHTS, TRADEMARKS
// OR OTHER RIGHTS.
// 
// Additional copyrights may follow.

#ifndef MPI2CPP_MAP_H_
#define MPI2CPP_MAP_H_

#include "mpi2c++/mpi2c++_list.h"
typedef List Container;

template <class TYPE1, class TYPE2>
class Map {
  Container c;
public:

  typedef TYPE1 key_t;
  typedef TYPE2 value_t;
  typedef List::iter iter;

  struct Pair {
    Pair(key_t f, value_t s) : first(f), second(s) {}
    Pair() : first((key_t) 0), second((value_t) 0) { }
    key_t first;
    value_t second;
  };

  Map() { }

  ~Map() {
    for (iter i = c.begin(); i != c.end(); i++) {
      delete (Pair*)(*i);
    }
  }
  
  Pair* begin();
  Pair* end();
  
  value_t& operator[](key_t key)
  {
    value_t* found = (value_t*)0;
    for (iter i = c.begin(); i != c.end(); i++) {
      if (((Pair*)*i)->first == key)
	found = &((Pair*)*i)->second;
    }
    if (! found) {
      iter tmp = c.insert(c.begin(), new Pair(key, (value_t) 0));
      found = &((Pair*)*tmp)->second;
    }
    return *found;
  }

  void erase(key_t key)
  {
    for (iter i = c.begin(); i != c.end(); i++) {
      if (((Pair*)*i)->first == key) {
	delete (Pair*)*i;
	c.erase(i); break;
      }
    }
  }
};

#endif





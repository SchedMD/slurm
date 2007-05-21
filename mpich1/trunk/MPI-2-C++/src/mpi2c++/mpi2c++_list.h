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

#ifndef MPI2CPP_LIST_H_
#define MPI2CPP_LIST_H_

#include "mpi2c++/mpi2c++_config.h"

class List {
public:
  typedef void* Data;
  class iter;

  class Link {
    friend class List;
    friend class iter;
    Data data;
    Link *next;
    Link *prev;
    Link() { }
    Link(Data d, Link* p, Link* n) : data(d), next(n), prev(p) { }
  };

  class iter {
    friend class List;
    Link* node;
  public:
    iter(Link* n) : node(n) { }
    iter& operator++() { node = node->next; return *this; }
    iter operator++(int) { iter tmp = *this; ++(*this); return tmp; }
    Data& operator*() const { return node->data; }
    MPI2CPP_BOOL_T operator==(const iter& x) const { return (MPI2CPP_BOOL_T)(node == x.node); }
    MPI2CPP_BOOL_T operator!=(const iter& x) const { return (MPI2CPP_BOOL_T)(node != x.node); }
  };
  
  List() { _end.prev = &_end; _end.next = &_end; }
  virtual ~List() {
    for (iter i = begin(); i != end(); ) {
      Link* garbage = i.node; i++;
      delete garbage;
    }
  }
  virtual iter begin() { return _end.next; }
  virtual iter end() { return &_end; }
  virtual iter insert(iter p, Data d) {
    iter pos(p);
    Link* n = new Link(d, pos.node->prev, pos.node);
    pos.node->prev->next = n;
    pos.node->prev = n;
    return n;
  }
  void erase(iter pos) {
    pos.node->prev->next = pos.node->next;
    pos.node->next->prev = pos.node->prev;
    delete pos.node;
  }

protected:
  Link _end;
};

#endif





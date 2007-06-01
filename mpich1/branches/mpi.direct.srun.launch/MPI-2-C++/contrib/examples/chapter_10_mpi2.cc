// -*- c++ -*-
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
// 

#include <iostream.h>
#include <mpi++.h>

void example10_2(void);
void example10_3(void);
void example10_4(void);
void example10_5(void);

// Example 10.1 - Please note foocomm is not foo_comm as on Page 273
// so it will not conflict with later uses of foo_comm.

class foocomm : public MPI::Intracomm {
public:
  void Send(const void* buf, int count, const MPI::Datatype& type,
	    int dest, int tag) const
  {
    // Class library functionality
    MPI::Intracomm::Send(buf, count, type, dest, tag);
    // More class library functionality
  }
};

int
main(int argc, char *argv[])
{
  MPI::Init(argc, argv);

  example10_2();
  
  example10_3();
  
  example10_4();

  example10_5();

  MPI::Finalize();
  return 0;
}
  
// Example 10.2

void
example10_2()
{
  MPI::Intracomm bar;
  if (bar == MPI::COMM_NULL)
    cout << "bar is MPI::COMM_NULL" << endl;
}

// Example 10.3

void
example10_3()
{
  MPI::Intracomm foo_comm, bar_comm, baz_comm;
  foo_comm = MPI::COMM_WORLD;
  bar_comm = MPI::COMM_WORLD.Dup();
  baz_comm = bar_comm;
  cout << "Is foocomm equal barcomm? " << (foo_comm == bar_comm) << endl;
  cout << "Is bazcomm equal barcomm? " << (baz_comm == bar_comm) << endl;
}

// Example 10.4 Erroneous Code

void
example10_4()
{
  MPI::Intracomm intra = MPI::COMM_WORLD.Dup();

  // This will show up as an unused variable if compiled with full
  // warnings on, but we keep it here a) because it's in Chapter 10,
  // and b) to show that it works and compiles properly

  MPI::Cartcomm cart(intra);
}

// Example 10.5

void
example10_5()
{
  MPI::Intercomm comm;
  comm = MPI::COMM_NULL;            // assign with COMM_NULL
  if (comm == MPI::COMM_NULL)       // true
    cout << "comm is NULL" << endl;
  if (MPI::COMM_NULL == comm)       // note -- a different function
    cout << "comm is still NULL" << endl;
}

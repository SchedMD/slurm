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

#ifndef _MPI2CPP_TEST_H_
#define _MPI2CPP_TEST_H_

// WDG - At least on Solaris, iostream MUST come before stdio (because
// stdio loads string, and iostream and string don't mix well
#include <iostream.h>
extern "C" {
#include <stdio.h>
#include <string.h>
}

#include <math.h>
#include <stdlib.h>
#include "mpi++.h"


// Version number

extern const int version[2];


// All the testing functions

void allgather();
void allreduce();
void alltoall();
void attr();
void badbuf();
void barrier();
void bcast();
void bcast_struct();
void bottom();
void bsend();
void buffer();
void cancel();
void cartcomm();
void commdup();
void commfree();
void compare();
void dims();
void dup_test();
void errhandler();
void gather();
void getcount();
void getel();
void graphcomm();
void group();
void groupfree();
void initialized1();
void initialized2();
void intercomm1();
void interf();
void iprobe();
void isend();
void lbub();
void lbub2();
void loop();
void op_test();
void pack_test();
void pcontrol();
void pptransp();
void probe();
void procname();
void range();
void rank_size();
void reduce();
void reduce_scatter();
void request1();
void rsend();
void rsend2();
void scan();
void scatter();
void send();
void sendrecv();
void sendrecv_rep();
void split();
void ssend();
void start();
void startall();
void status_test();
void strangest1();
void struct_gatherv();
void structsr();
void structsr2();
void test1();
void test3();
void testall();
void testany();
void testsome();
void topo();
void transp();
void transp2();
void transp3();
void transpa();
void waitall();
void waitany();
void waitsome();
void wildcard();
void wtime();

// Helper functions
// mpi2c++_test.cc

extern int my_rank;
extern int comm_size;
extern int to;
extern int from;
extern const double Epsilon;
extern MPI2CPP_BOOL_T CANCEL_WORKS;
extern MPI2CPP_BOOL_T TIGHTLY_COUPLED;

void do_work(int top = -1);


// messages.cc
void Testing(const char *msg);
void Pass(const char *msg = "PASS");
void Sync(const char *msg = 0);
void Postpone(const char *class_name);
void Done(const char *msg = 0);
void Fail(const char *msg = "FAIL");
void Abort(const char *msg = 0);


// stack.cc

void Push(const char *msg);
const char *Pop();


// signal.cc

void signal_init();


// General helper functions

inline void Test(int c, char *msg = 0) { (c) ? Pass() : Fail(msg); };
inline void Midtest(int c, char *msg = 0) { (c) ? Sync(msg) : Fail(msg); };
inline MPI2CPP_BOOL_T doublecmp(double a, double b) { return (MPI2CPP_BOOL_T) (fabs(a - b) < Epsilon); };


// Skip test flags

typedef enum _skip_flags {
  SKIP_MPICH120,
  SKIP_IBM21014, SKIP_IBM21015, SKIP_IBM21016, SKIP_IBM21017, SKIP_IBM21018,
    SKIP_IBM2_3_0_0,
  SKIP_LAM63, SKIP_LAM64,
  SKIP_SGI20, SKIP_SGI30, SKIP_SGI31, SKIP_SGI32,
  SKIP_HPUX0102, 
  SKIP_CRAY1104, 
  SKIP_NO_THROW,
  SKIP_MAX
} SKIP_FLAGS;
extern MPI2CPP_BOOL_T flags[SKIP_MAX];


#endif // _MPI2CPP_TEST_H_

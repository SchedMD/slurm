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
// The vast majority of this awesome file came from Jeff Squyres,
// Perpetual Obsessive Notre Dame Student Craving Utter Madness, 
// and Brian McCandless, another of the LSC crew, under the guidance
// of Herr Doctor Boss Andrew Lumsdaine. My thanks for making my
// life a whole lot easier.

#include "mpi2c++_test.h"

//
// Dummy op function
// Do a ((sum) + (len * size))
// Only works for int's.  Doh!

//
// note that if  _MPIPP_USENAMESPACE_ is defined, this must be defined
// as extern "C" (because User_function is extern "C" in that case)

static void
My_sum(const void *invec, void *inoutvec, int len, const MPI::Datatype& thetype)
{
  char msg[150];
  int i;
  int *src = (int *) invec;
  int *dest = (int *) inoutvec;

  for (i = 0; i < len; i++) {
    if (thetype != MPI::INT) {
      sprintf(msg, "NODE %d - 0) ERROR in My_sum, thetype != MPI::INT", my_rank);
      Fail(msg);
    }
    else {
      dest[i] += src[i] + len;
    }
  }
}


//
// MPI::Op Test
//
void
op_test()
{
  char msg[150];
  int i;
  int len;
  int recv[2];
  int send[2];
  int check[2];
  MPI::Op op1;

  len = 2;

  Testing("Init");
  {
    op1.Init(My_sum, MPI2CPP_TRUE);
    
    send[0]= my_rank;
    send[1]= my_rank * 3;
    recv[0] = recv[1] = -1;

    MPI::COMM_WORLD.Allreduce(send, recv, len, MPI::INT, op1);
    
    check[0] = check[1] = 0;
    for (i = 1; i < comm_size; i++) {
      check[0] += i + len;
      check[1] += (i * 3) + len;
    }
    
    if (check[0] != recv[0] || check[1] != recv[1]) {
      sprintf(msg, "NODE %d - 1) ERROR in Allreduce, recv[0]=%d, recv[1]=%d, should be %d, %d", 
	      my_rank, recv[0], recv[1], check[0], check[1]);
      Fail(msg);
    }
  }
  Pass(); // Init

  Testing("Free");
  if (op1 != MPI::OP_NULL) {
    op1.Free();

    if (op1 != MPI::OP_NULL) {
      sprintf(msg, "NODE %d - 2) ERROR in op1.Free, op1 not set to MPI::OP_NULL", my_rank);
      Fail(msg);
    }
  } else {
    sprintf(msg, "NODE %d - 3) ERROR in op1.Free, op1 never set to something not MPI::OP_NULL", my_rank);
      Fail(msg);
  }

  Pass(); // Free
}


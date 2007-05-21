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
// I made this.

#include "mpi2c++_test.h"
extern "C" {
#if !MPI2CPP_AIX 
#include <unistd.h>
#else
  // #@%$#@%$@#%$#@% AIX!!!
typedef unsigned int   useconds_t;
unsigned int sleep (useconds_t Seconds);
#endif
}

void
request1()
{
  char msg[150];
  int data;
  int i;
  MPI::Request request;
  MPI::Status status;

  request = MPI::REQUEST_NULL;

  Testing("Wait w/o Status");

  if ((my_rank % 2) == 0) {
    data = my_rank;

    MPI::COMM_WORLD.Send(&data, 1, MPI::INT, my_rank + 1, my_rank);
  } else if ((my_rank % 2) == 1) {
    data = -1;

    request = MPI::COMM_WORLD.Irecv(&data, 1, MPI::INT, my_rank - 1, my_rank - 1);
    request.Wait();

    if (data != my_rank - 1) {
      sprintf(msg, "NODE %d - 1) ERROR in Irecv, data = %d, should be %d", my_rank, data, my_rank - 1);
      Fail(msg);
    }
  }
    
  Pass(); // Wait w/o Status

  Testing("Wait w/ Status");

  if ((my_rank % 2) == 0) {
    data = my_rank;

    MPI::COMM_WORLD.Send(&data, 1, MPI::INT, my_rank + 1, my_rank);
  } else if ((my_rank % 2) == 1) {
    data = -1;

    request = MPI::COMM_WORLD.Irecv(&data, 1, MPI::INT, my_rank - 1, my_rank - 1);
    request.Wait(status);

    if (data != my_rank - 1) {
      sprintf(msg, "NODE %d - 2) ERROR in Irecv, data = %d, should be %d", my_rank, data, my_rank - 1);
      Fail(msg);
    }

    if (status.Get_source() != my_rank - 1) {
      sprintf(msg, "NODE %d - 3) ERROR in Wait, status.Get_source = %d, should be %d", my_rank, status.Get_source(), my_rank - 1);
      Fail(msg);
    }

    if (status.Get_tag() != my_rank - 1) {
      sprintf(msg, "NODE %d - 4) ERROR in Wait, status.Get_tag = %d, should be %d", my_rank, status.Get_tag(), my_rank - 1);
      Fail(msg);
    }
  }
  Pass(); // Wait w/ Status

  Testing("Test w/o Status");
  if ((my_rank % 2) == 0) {
    data = my_rank;

    MPI::COMM_WORLD.Send(&data, 1, MPI::INT, my_rank + 1, my_rank);
  } else if ((my_rank % 2) == 1) {
    data = -1;
    i = 0;

    request = MPI::COMM_WORLD.Irecv(&data, 1, MPI::INT, my_rank - 1, my_rank - 1);
    while (request.Test() == 0) {
      //      usleep(1);
      sleep(1);
      i++;
      if (i == 5000) {
	sprintf(msg, "NODE %d - 5) ERROR in Test, 5000 iterations have passed, and Test has not returned true yet.", my_rank);
	Fail(msg);
      }
    }

    if (data != my_rank - 1) {
      sprintf(msg, "NODE %d - 6) ERROR in Irecv, data = %d, should be %d", my_rank, data, my_rank - 1);
      Fail(msg);
    }
  }
  Pass(); // Test w/o Status

  Testing("Test w/ Status");
  if ((my_rank % 2) == 0) {
    data = my_rank;

    MPI::COMM_WORLD.Send(&data, 1, MPI::INT, my_rank + 1, my_rank);
  } else if ((my_rank % 2) == 1) {
    data = -1;

    request = MPI::COMM_WORLD.Irecv(&data, 1, MPI::INT, my_rank - 1, my_rank - 1);
    while (request.Test(status) == 0) {
      //usleep(1);
      sleep(1);
      i++;
      if (i == 5000) {
	sprintf(msg, "NODE %d - 7) ERROR in Test, 5000 iterations have passed, and Test has not returned true yet.", my_rank);
	Fail(msg);
      }
    }

    if (data != my_rank - 1) {
      sprintf(msg, "NODE %d - 8) ERROR in Irecv, data = %d, should be %d", my_rank, data, my_rank - 1);
      Fail(msg);
    }

    if (status.Get_source() != my_rank - 1) {
      sprintf(msg, "NODE %d - 9) ERROR in Wait, status.Get_source = %d, should be %d", my_rank, status.Get_source(), my_rank - 1);
      Fail(msg);
    }

    if (status.Get_tag() != my_rank - 1) {
      sprintf(msg, "NODE %d - 10) ERROR in Wait, status.Get_tag = %d, should be %d", my_rank, status.Get_tag(), my_rank - 1);
      Fail(msg);
    }
  }

  Pass(); // Test w/ Status

  if(request != MPI::REQUEST_NULL)
    request.Free();
}

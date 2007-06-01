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
/****************************************************************************

 MESSAGE PASSING INTERFACE TEST CASE SUITE

 Copyright IBM Corp. 1995

 IBM Corp. hereby grants a non-exclusive license to use, copy, modify, and
 distribute this software for any purpose and without fee provided that the
 above copyright notice and the following paragraphs appear in all copies.

 IBM Corp. makes no representation that the test cases comprising this
 suite are correct or are an accurate representation of any standard.

 In no event shall IBM be liable to any party for direct, indirect, special
 incidental, or consequential damage arising out of the use of this software
 even if IBM Corp. has been advised of the possibility of such damage.

 IBM CORP. SPECIFICALLY DISCLAIMS ANY WARRANTIES INCLUDING, BUT NOT LIMITED
 TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS AND IBM
 CORP. HAS NO OBLIGATION TO PROVIDE MAINTENANCE, SUPPORT, UPDATES,
 ENHANCEMENTS, OR MODIFICATIONS.

****************************************************************************

 These test cases reflect an interpretation of the MPI Standard.  They are
 are, in most cases, unit tests of specific MPI behaviors.  If a user of any
 test case from this set believes that the MPI Standard requires behavior
 different than that implied by the test case we would appreciate feedback.

 Comments may be sent to:
    Richard Treumann
    treumann@kgn.ibm.com

****************************************************************************
*/
#include "mpi2c++_test.h"

#define ITER 10

void
ssend()
{
  char msg[150];
  double *ptime; 
  double *ptimeoffset;
  double time;
  double timeoffset;
  int buf[ITER];
  int i;
  int len;
  int recv[ITER];
  int send[ITER];
  MPI::Request request;
  MPI::Status status;
  
  request = MPI::REQUEST_NULL;
  
  Testing("Ssend");
   
  if(TIGHTLY_COUPLED) {
    time = 0.0;
    timeoffset = 0.0;
    
    ptime = &time;
    ptimeoffset = &timeoffset;
    
    /* This test makes assumptions about the global nature of MPI_WTIME that
       are not required by MPI, and may falsely signal an error */
    
    len = sizeof(buf); 
    
    MPI::COMM_WORLD.Barrier();
    
    if((my_rank % 2) == 0) {
      /* First, roughly synchronize the clocks */
      MPI::COMM_WORLD.Recv(ptimeoffset, 1, MPI::DOUBLE, my_rank + 1, 1); 
      timeoffset = MPI::Wtime() - timeoffset;
 
      MPI::COMM_WORLD.Ssend(buf, len, MPI::CHAR, my_rank + 1, 1); 
      time = MPI::Wtime() - timeoffset;
      MPI::COMM_WORLD.Send(ptime, 1, MPI::DOUBLE, my_rank + 1, 2);
    } else if((my_rank %2) == 1) {
      time = MPI::Wtime(); 
      MPI::COMM_WORLD.Send(ptime, 1, MPI::DOUBLE, my_rank - 1, 1);
      
      for(i=0;i<3000000;i++) ;
      
      MPI::COMM_WORLD.Recv(buf, len, MPI::CHAR, my_rank - 1, 1);
      MPI::COMM_WORLD.Recv(ptime, 1, MPI_DOUBLE, my_rank - 1, 2);
      time = time - MPI::Wtime();
      if(time < 0) time = -time;
      if(time > .1) {
	sprintf(msg, "NODE %d - 1) ERROR in MPI::Ssend, did not synchronize",
		my_rank);
	Fail(msg);
      }
    }
  } else {
    if((my_rank % 2) == 0) {
      for(i = 0; i < ITER; i++) {
	send[i] = i;
	recv[i] = -1;
      }
      
      MPI::COMM_WORLD.Ssend(send, ITER, MPI::INT, my_rank + 1, 1);
      request = MPI::COMM_WORLD.Irecv(recv, ITER, MPI::INT, my_rank + 1, 1);
      request.Wait();

      for(i = 0; i < ITER; i++)
	if(recv[i] != (ITER - i)) {
	  sprintf(msg, "NODE %d - 2) ERROR in MPI::Ssend, data = %d, should be %d", my_rank, recv[i], ITER - 1 - i);
	  Fail(msg);
	}
    } else if((my_rank % 2) == 1) {
      for(i = 0; i < ITER; i++) {
	send[i] = ITER - i;
	recv[i] = -1;
      }

      request = MPI::COMM_WORLD.Irecv(recv, ITER, MPI::INT, my_rank - 1, 1);
      request.Wait();
      MPI::COMM_WORLD.Ssend(send, ITER, MPI::INT, my_rank - 1, 1);
     
      for(i = 0; i < ITER; i++)
	if(recv[i] != i) {
	  sprintf(msg, "NODE %d - 3) ERROR in MPI::Ssend, data = %d, should be %d", my_rank, recv[i], i);
	  Fail(msg);
	}
    }
  }

  Pass(); // Ssend
}

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
 
static int data2;
static int wst1 = 0; // Which call to wstart1 error occurs on.
static MPI::Prequest request_start1;
static MPI::Request non_blocker1;
 
void wstart2();
 
void
startall()
{
  char msg[150];
  int buf[10000];
  int size;
  void* oldbuf;

  Testing("Startall");
 
  Testing("Send_init");

  request_start1 = MPI::REQUEST_NULL;
  non_blocker1 = MPI::REQUEST_NULL;

  request_start1 = MPI::COMM_WORLD.Send_init(&my_rank, 1, MPI::INT, my_rank, 1);
  non_blocker1 = MPI::COMM_WORLD.Irecv(&data2, 1, MPI::INT, my_rank, 1);
  
  wstart2();

  Pass(); // Send_init
  
  Testing("Ssend_init");
  
  if(request_start1 != MPI::REQUEST_NULL)
    request_start1.Free();
  if(non_blocker1 != MPI::REQUEST_NULL)
    non_blocker1.Free();

  request_start1 = MPI::COMM_WORLD.Ssend_init(&my_rank, 1, MPI::INT, my_rank, 2);
  non_blocker1 = MPI::COMM_WORLD.Irecv(&data2, 1, MPI::INT, my_rank, 2);
  
  wstart2();

  Pass(); // Ssend_init

  Testing("Bsend_init");
  
  if (flags[SKIP_IBM21014])
    Done("Skipped (IBM 2.1.0.14)");
  else if (flags[SKIP_IBM21015])
    Done("Skipped (IBM 2.1.0.15)");
  else if (flags[SKIP_IBM21016])
    Done("Skipped (IBM 2.1.0.16)");
  else if (flags[SKIP_IBM21017])
    Done("Skipped (IBM 2.1.0.17)");
  else {
    MPI::Attach_buffer(buf,sizeof(buf));

    if(request_start1 != MPI::REQUEST_NULL)
      request_start1.Free();
    if(non_blocker1 != MPI::REQUEST_NULL)
      non_blocker1.Free();
    
    request_start1 = MPI::COMM_WORLD.Bsend_init(&my_rank, 1, MPI::INT, my_rank, 3);
    non_blocker1 = MPI::COMM_WORLD.Irecv(&data2, 1, MPI::INT, my_rank, 3);
    
    wstart2();

    size = MPI::Detach_buffer(oldbuf);
    if (size != sizeof(buf)) {
      sprintf(msg, "NODE %d - 00) ERROR: Buffer not detached", my_rank);
      Fail(msg);
    }
    Pass(); // Bsend_init
  }

  Testing("Rsend_init");
  
  if(request_start1 != MPI::REQUEST_NULL)
    request_start1.Free();
  if(non_blocker1 != MPI::REQUEST_NULL)
    non_blocker1.Free();
  
  request_start1 = MPI::COMM_WORLD.Rsend_init(&my_rank, 1, MPI::INT, my_rank, 4);
  non_blocker1 = MPI::COMM_WORLD.Irecv(&data2, 1, MPI::INT, my_rank, 4);
  
  wstart2();  
  
  Pass(); // Rsend_init

  Testing("Recv_init");
  
  if(request_start1 != MPI::REQUEST_NULL)
    request_start1.Free();
  if(non_blocker1 != MPI::REQUEST_NULL)
    non_blocker1.Free();
  
  request_start1 = MPI::COMM_WORLD.Recv_init(&data2, 1, MPI::INT, my_rank, 5);
  non_blocker1 = MPI::COMM_WORLD.Isend(&my_rank, 1, MPI::INT, my_rank, 5);
  
  wstart2();  
  
  Pass(); // Recv_init
    
  Pass(); // Startall
  
  if(request_start1 != MPI::REQUEST_NULL)
    request_start1.Free();
  if(non_blocker1 != MPI::REQUEST_NULL)
    non_blocker1.Free();
}

void 
wstart2()
{
  char msg[150];
  MPI::Status stats[5];
  
  data2 = -1;
  
  MPI::Prequest::Startall(1, &request_start1);
  request_start1.Wait();

  non_blocker1.Wait();
  
  if(data2 != my_rank) {
    sprintf(msg, "NODE %d - %d) ERROR after waitall, data2 = %d, should be %d", my_rank, wst1, data2, my_rank);
    Fail(msg);
  }

  wst1++;
}

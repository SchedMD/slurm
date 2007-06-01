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

void
waitany()
{
  char msg[150];
  int data[2000];
  int i;
  int index;
  MPI::Request req[2000];
  MPI::Status status;
  
  Testing("Waitany w/ Status");

  for(i = 0; i < 2000; i++) {
    data[i] = -1;
    req[i] = MPI::REQUEST_NULL;
  }

  if(my_rank > 0) 
    MPI::COMM_WORLD.Send(&my_rank, 1, MPI::INT, 0, 1);
  else {
    req[0] = MPI::REQUEST_NULL;
    for(i = 1; i < comm_size; i++)  
      req[i] = MPI::COMM_WORLD.Irecv(&data[i], 1, MPI::INT, i, 1);

    index = MPI::Request::Waitany(comm_size, req, status);  
    if(index == MPI::UNDEFINED) {
      sprintf(msg, "NODE %d - 2) ERROR in MPI::Waitany, index = %d (MPI::UNDEFINED)", my_rank, index);
      Fail(msg);
    }
    if(req[index] != MPI::REQUEST_NULL) {
      sprintf(msg, "NODE %d - 3) ERROR in MPI::Waitany, req[%d] not set to MPI::REQUEST_NULL", my_rank, index);
      Fail(msg);
    }
    if(data[index] != index) {
      sprintf(msg, "NODE %d - 4) ERROR in MPI::Waitany, data = %d, should be %d", my_rank, data[index], index);
      Fail(msg);
    }
  }

  Pass(); // Waitany w/ Status

  MPI::COMM_WORLD.Barrier();

  Testing("Waitany w/o Status");

  for(i = 0; i < 2000; i++) {
    data[i] = -1;
    req[i] = MPI::REQUEST_NULL;
  }

  if(my_rank > 0) 
    MPI::COMM_WORLD.Send(&my_rank, 1, MPI::INT, 0, 1);
  else {
    req[0] = MPI::REQUEST_NULL;
    for(i = 1; i < comm_size; i++)  
      req[i] = MPI::COMM_WORLD.Irecv(&data[i], 1, MPI::INT, i, 1);

    index = MPI::Request::Waitany(comm_size, req);
    if(index == MPI::UNDEFINED) {
      sprintf(msg, "NODE %d - 7) ERROR in MPI::Waitany, index = %d (MPI::UNDEFINED)", my_rank, index);
      Fail(msg);
    }
    if(req[index] != MPI::REQUEST_NULL) {
      sprintf(msg, "NODE %d - 8) ERROR in MPI::Waitany, req[%d] not set to MPI::REQUEST_NULL", my_rank, index);
      Fail(msg);
    }
    if(data[index] != index) {
      sprintf(msg, "NODE %d - 9) ERROR in MPI::Waitany, data = %d, should be %d", my_rank, data[index], index);
      Fail(msg);
    }
  }

  Pass(); // Waitany w/o Status

  for (i = 0; i < 2000; i++)
    if (req[i] != MPI::REQUEST_NULL) {
      req[i].Cancel();
      req[i].Wait();
    }

  MPI::COMM_WORLD.Barrier();
}

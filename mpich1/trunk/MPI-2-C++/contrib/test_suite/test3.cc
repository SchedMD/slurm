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
test3()
{
  char msg[150];
  int done;
  int in;
  int out;
  MPI::Request req1;
  MPI::Request req2;
  MPI::Status status;
 
  Testing("Test w/o Status");

  req1 = MPI::REQUEST_NULL;
  req2 = MPI::REQUEST_NULL;

  in = -1;
  out = 1;

  if((my_rank % 2) == 0) {
    req1 = MPI::COMM_WORLD.Isend(&out, 1, MPI::INT, my_rank + 1, 1);
    req2 = MPI::COMM_WORLD.Irecv(&in, 1, MPI::INT, my_rank + 1, 2);
    for(;;) { 
      done = req1.Test(); 
      if(done) 
	break; 
    }
    for(;;) { 
      done = req2.Test(); 
      if(done) 
	break; 
    }
  } else if((my_rank % 2) == 1) { 
    MPI::COMM_WORLD.Send(&out, 1, MPI_INT, my_rank - 1, 2);
    MPI::COMM_WORLD.Recv(&in, 1, MPI_INT, my_rank - 1, 1);
  }

  if(in != 1) {
    sprintf(msg, "NODE %d - 1) ERROR in MPI::Test, in = %d, should be 1",
	    my_rank, in);
    Fail(msg);
  }

  Pass(); // Test w/o Status

  if(req1 != MPI::REQUEST_NULL)
    req1.Free();
  if(req2 != MPI::REQUEST_NULL)
    req2.Free();
}

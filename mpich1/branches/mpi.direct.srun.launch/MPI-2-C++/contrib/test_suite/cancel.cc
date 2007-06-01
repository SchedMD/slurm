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
#if !MPI2CPP_AIX
extern "C" {
#include <unistd.h>
}
#endif

void
cancel()
{
  char msg[150];
  int data;
  int flag;
  MPI::Request request;
  MPI::Status status;

  data = 13;
  request = MPI::REQUEST_NULL;

  Testing("Cancel");

  if (flags[SKIP_SGI20])
    Done("Skipped (SGI 2.0)");
  else if (flags[SKIP_SGI30])
    Done("Skipped (SGI 3.0)");
  else if (flags[SKIP_SGI31])
    Done("Skipped (SGI 3.1)");
  else if (flags[SKIP_SGI32])
    Done("Skipped (SGI 3.2)");
  else if (flags[SKIP_LAM63])
    Done("Skipped (LAM 6.3.x)");
  else if (flags[SKIP_LAM64])
    Done("Skipped (LAM 6.4.x)");
  else if (flags[SKIP_CRAY1104])
    Done("Skipped (CRAY 1.1.0.4)");
  else if (flags[SKIP_HPUX0102])
    Done("Skipped (HPUX 01.02)");
  else if (flags[SKIP_IBM2_3_0_0])
    Done("Skipped (IBM POE 2.3.0.0)");
  else { 
#if (MPI2CPP_HPUX0103 || MPI2CPP_HPUX0105)
    if (getenv("MPI_FLAGS") == 0) {
      if (my_rank == 0) {
	cout << endl << endl 
	     << "The MPI-2 C++ test suite depends on the MPI_FLAGS environment"
	     << endl
	     << "variable being set to \"sa5\" *before* mpirun is invoked for"
	     << endl
	     << "successful testing. The test suite will now exit since MPI_FLAGS"
	     << endl
	     << "is not currently set. Set the MPI_FLAGS variable and re-run the"
	     << endl
	     << "MPI-2 C++ test suite." << endl << endl;
      }
      Fail("MPI_FLAGS not set");   
    }
#endif
    if ((my_rank % 2) == 0)  {
      data = 5;

      request = MPI::COMM_WORLD.Isend(&data, 1, MPI::INT, my_rank + 1, 5);
      request.Cancel();
      request.Wait(status);

      flag = status.Is_cancelled();
      if(!flag) {
	sprintf(msg, "NODE %d - 3) ERROR: Isend request not cancelled!",
		my_rank);
	Fail(msg);
      }
      
      MPI::COMM_WORLD.Barrier();
      data = 6;
      MPI::COMM_WORLD.Send(&data, 1, MPI::INT, my_rank + 1, 5);
    } else if ((my_rank % 2) == 1) {
      MPI::COMM_WORLD.Barrier();

      data = 0;
      
      MPI::COMM_WORLD.Recv(&data, 1, MPI::INT, my_rank - 1, 5, status);
      if (data != 6) {
	sprintf(msg, "NODE %d - 4) ERROR: Isend request not cancelled! Data = %d, should be 6", my_rank, data);
	Fail(msg);
      }
    }
    Pass(); // Cancel
  }
}

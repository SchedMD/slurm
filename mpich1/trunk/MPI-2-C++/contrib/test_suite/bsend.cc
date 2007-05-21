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
bsend()
{
#if _MPIPP_USEEXCEPTIONS_
  char msg[150];
  int class1;
  int data[100000];
  int i;
  int size;
  void* oldbuf;
  MPI::Status status;
#endif
  char* buf1;

  Testing("Bsend");

  if (flags[SKIP_IBM21014])
    Done("Skipped (IBM 2.1.0.14)");
  else if (flags[SKIP_IBM21015])
    Done("Skipped (IBM 2.1.0.15)");
  else if (flags[SKIP_IBM21016])
    Done("Skipped (IBM 2.1.0.16)");
  else if (flags[SKIP_IBM21017])
    Done("Skipped (IBM 2.1.0.17)");
  else {
    buf1 = new char[sizeof(int) * 1000 + MPI::BSEND_OVERHEAD];
    
#if _MPIPP_USEEXCEPTIONS_
    MPI::COMM_WORLD.Set_errhandler(MPI::ERRORS_THROW_EXCEPTIONS);
    
    if((my_rank % 2) == 0) {
      for(i = 0; i < 100000; i++)  
	data[i] = 1;
      
      class1 = MPI::SUCCESS;
      
      MPI::Attach_buffer(buf1, sizeof(int) * 1000 + MPI::BSEND_OVERHEAD);
      
      try {
	MPI::COMM_WORLD.Bsend(data, 1000, MPI::INT, my_rank + 1, 1);
      }
      catch(MPI::Exception e) {
	class1 = e.Get_error_class();
      }
      if(class1 != MPI::SUCCESS) {
	sprintf(msg, "NODE %d - 1) Error in 1st bsend: %d", my_rank, class1);
	Fail(msg);
      }
      
      size = MPI::Detach_buffer(oldbuf);
      if(size != sizeof(int) * 1000 + MPI::BSEND_OVERHEAD) {
	sprintf(msg, "NODE %d - 2) ERROR in Detach_buffer, incorrect size returned.", my_rank);
	Fail(msg);
      }
    } else if((my_rank % 2) == 1) {
      for(i = 0; i < 100000; i++)  
	data[i] = 2;
      
      MPI::COMM_WORLD.Recv(data, 1000, MPI::INT, my_rank - 1, 1, status);
      for(i = 0; i < 1000; i++)
	if(data[i] != 1) {
	  sprintf(msg, "NODE %d - 6) ERROR, incorrect data value received, task 1, recv 1", 
		  my_rank);
	  Fail(msg);
	}
    }
    MPI::COMM_WORLD.Set_errhandler(MPI::ERRORS_RETURN);

    Pass(); // Bsend
#else
    Done("Compiler does not have exceptions");
#endif

    delete[] buf1;
  }
}




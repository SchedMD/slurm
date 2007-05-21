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

/* Please note the following does intentionally call both C and C++ functions.
   This is done to make an easy comparison check between retruned results. If 
   the comparison fails, the test fails. Thank you for your cooperation.    */

#include "mpi2c++_test.h"

void
waitsome()
{
  char msg[150];
  int data[2000];
  int done;
  int i;
  int index[2000];
  int outcount;
  int poutcount;
  
  MPI_Request array_of_requests[2000];
  MPI::Request req[2000];
  MPI_Status array_of_statuses[2000];
  MPI::Status status[2000];

  Testing("Waitsome w/ Status");
  
  {
    for(i = 0; i < 2000; i++) {
      data[i] = -1;
      index[i] = -1;
      array_of_requests[i] = MPI_REQUEST_NULL;
      req[i] = MPI::REQUEST_NULL;
    }
    
    if(my_rank > 0) 
      MPI::COMM_WORLD.Send(&my_rank, 1, MPI::INT, 0, 1);
    else {
      req[0] = MPI::REQUEST_NULL;
      for(i = 1; i < comm_size; i++)  
	req[i] = MPI::COMM_WORLD.Irecv(&data[i], 1, MPI::INT, i, 1);
      
      for (i = 0; i < 0; i++)
	array_of_requests[i] = req[i];
      
      MPI_Waitsome(0, array_of_requests, &poutcount, index, array_of_statuses);
      outcount = MPI::Request::Waitsome(0, req, index, status);  
      if(outcount != poutcount) {
	sprintf(msg, "NODE %d - 1) ERROR in MPI::Waitsome, coutcount = %d, should be %d", my_rank, outcount, poutcount);
	Fail(msg);
      }
      
      done = 0; 
      while(done < comm_size - 1)  {
	outcount = MPI::Request::Waitsome(comm_size, req, index, status);  
	if(outcount == 0) {
	  sprintf(msg, "NODE %d - 2) ERROR in MPI::Waitsome, outcount = 0",
		  my_rank);
	  Fail(msg);
	}
	for(i = 0; i < outcount; i++)  {
	  done++;
	  if(index[i] == MPI::UNDEFINED) {
	    sprintf(msg, "NODE %d - 3) ERROR in MPI::Waitsome, index = %d (MPI::UNDEFINED)", my_rank, index[i]);
	    Fail(msg);
	  }
	  if(req[index[i]] != MPI::REQUEST_NULL) {
	    sprintf(msg, "NODE %d - 4) ERROR in MPI::Waitsome, req[%d] not set to MPI::REQUEST_NULL", my_rank, index[i]);
	    Fail(msg);
	  }
	  if(data[index[i]] != index[i]) {
	    sprintf(msg, "NODE %d - 5) ERROR in MPI::Waitsome, data = %d, should be %d", my_rank, data[index[i]], index[i]);
	    Fail(msg);
	  }
	}
      }
    
      for (i = 0; i < comm_size; i++)
	array_of_requests[i] = req[i];
      
      MPI_Waitsome(comm_size, array_of_requests, &poutcount, index,
		   array_of_statuses);
      outcount = MPI::Request::Waitsome(comm_size, req, index, status);  
      if(outcount != poutcount) {
	sprintf(msg, "NODE %d - 6) ERROR in MPI::Waitsome, coutcount = %d, should be %d", my_rank, outcount, poutcount);
	Fail(msg);
      }
    }

    Pass(); // Waitsome w/ Status
  }

  MPI::COMM_WORLD.Barrier();

  Testing("Waitsome w/o Status");

  {
    for(i = 0; i < 2000; i++) {
      data[i] = -1;
      index[i] = -1;
      array_of_requests[i] = MPI_REQUEST_NULL;
      req[i] = MPI::REQUEST_NULL;
    }
    
    if(my_rank > 0) 
      MPI::COMM_WORLD.Send(&my_rank, 1, MPI::INT, 0, 1);
    else {
      req[0] = MPI::REQUEST_NULL;
      for(i = 1; i < comm_size; i++)  
	req[i] = MPI::COMM_WORLD.Irecv(&data[i], 1, MPI::INT, i, 1);
      
      for (i = 0; i < 0; i++)
	array_of_requests[i] = req[i];
      
      MPI_Waitsome(0, array_of_requests, &poutcount, index, array_of_statuses);
      outcount = MPI::Request::Waitsome(0, req, index);  
      if(outcount != poutcount) {
	sprintf(msg, "NODE %d - 7) ERROR in MPI::Waitsome, coutcount = %d, should be %d", my_rank, outcount, poutcount);
	Fail(msg);
      }
      
      done = 0; 
      while(done < comm_size - 1)  {
	outcount = MPI::Request::Waitsome(comm_size, req, index);  
	if(outcount == 0) {
	  sprintf(msg, "NODE %d - 8) ERROR in MPI::Waitsome, outcount = 0",
		  my_rank);
	  Fail(msg);
	}
	for(i = 0; i < outcount; i++)  {
	  done++;
	  if(index[i] == MPI::UNDEFINED) {
	    sprintf(msg, "NODE %d - 9) ERROR in MPI::Waitsome, index = %d (MPI::UNDEFINED)", my_rank, index[i]);
	    Fail(msg);
	  }
	  if(req[index[i]] != MPI::REQUEST_NULL) {
	    sprintf(msg, "NODE %d - 10) ERROR in MPI::Waitsome, req[%d] not set to MPI::REQUEST_NULL", my_rank, index[i]);
	    Fail(msg);
	  }
	  if(data[index[i]] != index[i]) {
	    sprintf(msg, "NODE %d - 11) ERROR in MPI::Waitsome, data = %d, should be %d", my_rank, data[index[i]], index[i]);
	    Fail(msg);
	  }
	}
      }
      
      for (i = 0; i < comm_size; i++)
	array_of_requests[i] = req[i];
      
      MPI_Waitsome(comm_size, array_of_requests, &poutcount, index,
		 array_of_statuses);
      outcount = MPI::Request::Waitsome(comm_size, req, index);  
      if(outcount != poutcount) {
	sprintf(msg, "NODE %d - 12) ERROR in MPI::Waitsome, coutcount = %d, should be %d", my_rank, outcount, poutcount);
	Fail(msg);
      }
    }
    Pass(); // Waitsome w/o Status
  }

  for (i = 0; i < 2000; i++) {
    if (array_of_requests[i] != MPI_REQUEST_NULL) {
      MPI_Cancel(&array_of_requests[i]);
      MPI_Wait(&array_of_requests[i], &array_of_statuses[0]);
    }
    if (req[i] != MPI::REQUEST_NULL) {
      req[i].Cancel();
      req[i].Wait();
    }
  }
}

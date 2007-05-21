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
probe()
{
  char msg[150];
  int cnt;
  int data;
  int i;
  int src;
  int tag;
  MPI::Intracomm comm;
  MPI::Status status;
 
  comm = MPI::COMM_WORLD;

  Testing("Probe w/ Status");

  // Probe for specific source and tag.

  {
    if(my_rank > 0) {
      data = my_rank;
      comm.Send(&data, 1, MPI::INT, 0, my_rank);
    } else  {
      for(i = 1; i < comm_size; i++)  {
	data = -1;
	
	comm.Probe(i, i, status);
	
	src = status.Get_source();
	if(src != i) {
	  sprintf(msg, "NODE %d - 1) ERROR in MPI::Probe, src = %d, should be %d", 
		  my_rank, src, i);
	  Fail(msg);
	}
	
	tag = status.Get_tag();
	if(tag != i) {
	  sprintf(msg, "NODE %d - 2) ERROR in MPI::Probe, tag = %d, should be %d", 
		  my_rank, tag, i);
	  Fail(msg);
	}
	
	cnt = status.Get_count(MPI::INT);
	if(cnt != 1) {
	  sprintf(msg, "NODE %d - 3) ERROR in MPI::Probe, count = %d, should be 1",
		  my_rank, cnt);
	  Fail(msg);
	}
	
	comm.Recv(&data, cnt, MPI::INT, src, tag, status);
	if(data != i) {
	  sprintf(msg, "NODE %d - 4) ERROR in MPI::Recv, data = %d, should be %d",
		  my_rank, data, i);
	  Fail(msg);
	}
      }
    }
    
    // Probe for specific source and tag = MPI::ANY_TAG.
    
    if(my_rank > 0) {
      data = my_rank;
      
      comm.Send(&data, 1, MPI::INT, 0, my_rank);
    } else {
      for(i = 1; i < comm_size; i++)  {
	data = -1;
	
	comm.Probe(i, MPI::ANY_TAG, status);
	
	src = status.Get_source();
	if(src != i) {
	  sprintf(msg, "NODE %d - 5) ERROR in MPI::Probe, src = %d, should be %d", 
		  my_rank, src, i);
	  Fail(msg);
	}
	
	tag = status.Get_tag();
	if(tag != i) {
	  sprintf(msg, "NODE %d - 6) ERROR in MPI::Probe, tag = %d, should be %d", 
		  my_rank, tag, i);
	  Fail(msg);
	}
	
	cnt = status.Get_count(MPI::INT);
	if(cnt != 1) {
	  sprintf(msg, "NODE %d - 7) ERROR in MPI::Probe, cnt = %d, should be 1", 
		  my_rank, cnt);
	  Fail(msg);
	}
	
	comm.Recv(&data, cnt, MPI::INT, src, tag, status);
	if(data != i) {
	  sprintf(msg, "NODE %d - 8) ERROR in MPI::Recv, data = %d, should be %d", 
		  my_rank, data, i);
	  Fail(msg);
	}
      }
    }
    comm.Barrier();
    
    // Probe for specific tag and source = MPI_ANY_SOURCE.
    
    if(my_rank > 0) {
      data = my_rank;
      comm.Send(&data, 1, MPI::INT, 0, my_rank);
    } else  {
      for(i = 1; i < comm_size; i++)  {
	data = -1;
	comm.Probe(MPI::ANY_SOURCE, i, status);
	src = status.Get_source();
	if(src != i) {
	  sprintf(msg, "NODE %d - 9) ERROR in MPI::Probe, src = %d, should be %d", 
		  my_rank, src, i);
	  Fail(msg);
	}
	tag = status.Get_tag();
	if(tag != i) {
	  sprintf(msg, "NODE %d - 10) ERROR in MPI::Probe, tag = %d, should be %d",
		  my_rank, tag, i);
	  Fail(msg);
	}
	cnt = status.Get_count(MPI::INT);
	if(cnt != 1) {
	  sprintf(msg, "NODE %d - 11) ERROR in MPI::Probe, cnt = %d, should be 1",
		  my_rank, cnt);
	  Fail(msg);
	}
	comm.Recv(&data, cnt, MPI::INT, src, tag, status);
	if(data != i) {
	  sprintf(msg, "NODE %d - 12) ERROR in MPI::Recv, data = %d, should be %d",
		  my_rank, data, i);
	  Fail(msg);
	}
      }
    }
    comm.Barrier();
    
    // Probe for source = MPI_ANY_SOURCE and tag = MPI_ANY_TAG.
    
    if(my_rank > 0) {
      data = my_rank;
      comm.Send(&data, 1, MPI::INT, 0, my_rank);
    } else  {
      for(i = 1; i < comm_size; i++)  {
	data = -1;
	
	comm.Probe(MPI::ANY_SOURCE, MPI::ANY_TAG, status);
	
	src = status.Get_source();
	tag = status.Get_tag();
	if(src != tag) {
	  sprintf(msg, "NODE %d - 13) ERROR in MPI::Probe, tag = %d, should be %d",
		  my_rank, tag, src);
	  Fail(msg);
	}
	
	cnt = status.Get_count(MPI::INT);
	if(cnt != 1) {
	  sprintf(msg, "NODE %d - 14) ERROR in MPI::Probe, cnt = %d, should be 1",
		  my_rank, cnt);
	  Fail(msg);
	}
	
	comm.Recv(&data, cnt, MPI::INT, src, tag, status);
	if(data != src) {
	  sprintf(msg, "NODE %d - 15) ERROR in MPI::Recv, data = %d, should be %d",
		  my_rank, data, src);
	  Fail(msg);
	}
      }
    }
    Pass(); // Probe w/ Status
  }
  
  Testing("Probe w/o Status");
  
  // Probe for specific source and tag.
  
  {
    if(my_rank > 0) {
      data = my_rank;
      comm.Send(&data, 1, MPI::INT, 0, my_rank);
    } else  {
      for(i = 1; i < comm_size; i++)  {
	data = -1;
	
	comm.Probe(i, i);
	
	comm.Recv(&data, 1, MPI::INT, i, i);
	if(data != i) {
	  sprintf(msg, "NODE %d - 16) ERROR in MPI::Recv, data = %d, should be %d",
		  my_rank, data, i);
	  Fail(msg);
	}
      }
    }
    
    // Probe for specific source and tag = MPI_ANY_TAG.
    
    if(my_rank > 0) {
      data = my_rank;
      comm.Send(&data, 1, MPI::INT, 0, my_rank);
    } else  {
      for(i = 1; i < comm_size; i++) {
	data = -1;
	
	comm.Probe(i, MPI::ANY_TAG);
	comm.Recv(&data, 1, MPI::INT, i, MPI::ANY_TAG);
	if(data != i) {
	  sprintf(msg, "NODE %d - 17) ERROR in MPI::Recv, data = %d, should be %d",
		  my_rank, data, i);
	  Fail(msg);
	}
      }
    }
    comm.Barrier();
    
    // Probe for specific tag and source = MPI_ANY_SOURCE.
    
    if(my_rank > 0) {
      data = my_rank;
      comm.Send(&data, 1, MPI::INT, 0, my_rank);
    } else  {
      for(i = 1; i < comm_size; i++) {
	data = -1;
	
	comm.Probe(MPI::ANY_SOURCE, i);
	comm.Recv(&data, 1, MPI::INT, MPI::ANY_SOURCE, i);
	if(data != i) {
	  sprintf(msg, "NODE %d - 18) ERROR in MPI::Recv, data = %d, should be %d",
		  my_rank, data, i);
	  Fail(msg);
	}
      }
    }
    comm.Barrier();
    
    // Probe for source = MPI_ANY_SOURCE and tag = MPI_ANY_TAG.
    
    if(my_rank > 0) {
      data = my_rank;
      comm.Send(&data, 1, MPI::INT, 0, my_rank);
    } else  {
      for(i = 1; i < comm_size; i++)  {
	data = -1;
	
	comm.Probe(MPI::ANY_SOURCE, MPI::ANY_TAG);
	comm.Recv(&data, 1, MPI::INT, MPI::ANY_SOURCE, MPI::ANY_TAG);
	if(!(data > 0 && data < comm_size)) {
	  sprintf(msg, "NODE %d - 19) ERROR in MPI::Recv, data = %d, should be %d",
		  my_rank, data, i);
	  Fail(msg);
	}
      }
    }
    
    Pass(); // Probe w/o Status
  }

  if(comm != MPI::COMM_NULL && comm != MPI::COMM_WORLD)
    comm.Free();
}

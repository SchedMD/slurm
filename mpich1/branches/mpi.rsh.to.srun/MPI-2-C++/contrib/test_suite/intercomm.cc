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
 
static int pass1;

void inter_tests1(MPI::Intercomm&, int, int);

void
intercomm1()
{
  char msg[150];
  int flag;
  int key;
  int color;
  int local_lead;
  int remote_lead;
  int sum;
  MPI::Intracomm comm;
  MPI::Intercomm intercomm;
  MPI::Intercomm intercomm2;

  intercomm = MPI::COMM_NULL;
  intercomm2 = MPI::COMM_NULL;
  comm = MPI::COMM_NULL;

  pass1 = 0;

  key = my_rank;
  color = my_rank % 2;

  comm = MPI::COMM_WORLD.Split(color, key);
  if (comm == MPI::COMM_NULL) {
    sprintf(msg, "NODE %d - 1) ERROR in MPI::Split, comm == MPI::COMM_NULL",
	    my_rank);
    Fail(msg);
  }

  flag = -1;
  flag = comm.Is_inter();
  if (flag) {
    sprintf(msg, "NODE %d - 2) ERROR in MPI::Is_inter, flag = %d, should be 0",
	    my_rank, flag);
    Fail(msg);
  }

  Testing("Create_intercomm");

  comm.Allreduce(&my_rank, &sum, 1, MPI::INT, MPI::SUM);
  
  local_lead = 0;
  remote_lead = color ? 0 : 1;
  intercomm = comm.Create_intercomm(local_lead, MPI::COMM_WORLD, 
				    remote_lead, 5);
  if (intercomm == MPI::COMM_NULL) {
    sprintf(msg, "NODE %d - 3) ERROR in MPI::Create_intercomm, intercomm == MPI::COMM_NULL, create failed", my_rank);
    Fail(msg);
  }
  
  inter_tests1(intercomm, color, sum);
  
  Pass(); // Create_intercomm
  
  pass1 = 0;
  
  Testing("Intercomm::Dup");
  
  if(flags[SKIP_IBM21014])
    Done("Skipped (IBM 2.1.0.14)");
  else if(flags[SKIP_IBM21015])
    Done("Skipped (IBM 2.1.0.15)");
  else if(flags[SKIP_IBM21016])
    Done("Skipped (IBM 2.1.0.16)");
  else if(flags[SKIP_IBM21017])
    Done("Skipped (IBM 2.1.0.17)");
  else if(flags[SKIP_IBM21018])
    Done("Skipped (IBM 2.1.0.18)");
  else {
    intercomm2 = intercomm.Dup();
    if (intercomm2 == MPI::COMM_NULL) {
      sprintf(msg, "NODE %d - 4) ERROR in MPI::Create_intercomm, intercomm2 == MPI::COMM_NULL, create failed", my_rank);
      Fail(msg);
    }
    
    inter_tests1(intercomm2, color, sum);
    
    Pass(); // Intercomm::Dup
  }

  if (intercomm != MPI::COMM_NULL)
    intercomm.Free();
  if (intercomm2 != MPI::COMM_NULL)
    intercomm2.Free();
  if (comm != MPI::COMM_NULL)
    comm.Free();
  
  MPI::COMM_WORLD.Barrier();
}

void 
inter_tests1(MPI::Intercomm& intercomm, int color, int sum)
{
  char msg[150];
  int flag;
  int newsize;
  int newsum;
  int othersum;
  int rank;
  int size;
  int newto;
  int newfrom;
  MPI::Group newgid;
  MPI::Intracomm mergecomm;
  MPI::Status status;

  mergecomm = MPI::COMM_NULL;
  newgid = MPI::GROUP_NULL;
  rank = intercomm.Get_rank();
  size = intercomm.Get_size();
  newto = (rank + 1)  % size;
  newfrom = (rank + size - 1) % size;

  Testing("Is_inter");

  flag = -1;

  flag = intercomm.Is_inter();
  if (flag != 1) {
    sprintf(msg, "NODE %d - 5) ERROR in MPI::Is_inter, flag = %d, should be 1",
	    my_rank, flag);
    Fail(msg);
  }

  Pass(); // Is_inter

  Testing("Get_remote_size");
  
  newsize = -1;
  newsize = intercomm.Get_remote_size();
  if (newsize != (comm_size / 2)) {
    sprintf(msg, "NODE %d - 6) ERROR in MPI::Get_remote_size, newsize = %d, should be %d", my_rank, newsize, comm_size / 2);
    Fail(msg);
  }
  Pass(); // Get_remote_size

  Testing("Get_remote_group");

  newgid = intercomm.Get_remote_group();
  if (newgid == MPI::GROUP_NULL) {
    sprintf(msg, "NODE %d - 7) ERROR in MPI::Get_remote_group, newgid == MPI::GROUP_NULL", my_rank);
    Fail(msg);
  }

  newsize = -1;
  newsize = newgid.Get_size();
  if (newsize != (comm_size / 2)) {
    sprintf(msg, "NODE %d - 8) ERROR in MPI::Get_remote_group, newsize = %d, should be %d", my_rank, newsize, comm_size / 2);
    Fail(msg);
  }

  Pass(); // Get_remote_group

  newsum = sum;
  intercomm.Sendrecv_replace(&newsum, 1, MPI::INT, newto, 70, newfrom, 
			     70, status);
  othersum = comm_size / 2 * (comm_size / 2 - 1);
  if (my_rank % 2 == 0)  
    othersum += comm_size / 2;
  if (othersum != newsum) {
    if (pass1 == 0)
      sprintf(msg, "NODE %d - 9) ERROR in MPI::Intercomm_create, sum = %d, should be %d", my_rank, othersum, newsum);
    else
      sprintf(msg, "NODE %d - 10) ERROR in MPI::Dup, sum = %d, should be %d", 
	      my_rank, othersum, newsum);
    Fail(msg);
  }

  Testing("Merge");
  
  mergecomm = intercomm.Merge((MPI2CPP_BOOL_T) color);
  mergecomm.Allreduce(&my_rank, &newsum, 1, MPI::INT, MPI::SUM);
  if (newsum != (comm_size * (comm_size - 1) / 2)) {
    sprintf(msg, "NODE %d - 11) ERROR IN MPI::Merge, sum = %d, should be %d",
	    my_rank, newsum, comm_size * (comm_size - 1) / 2);
    Fail(msg);
  }


  Pass(); // Merge

  if (mergecomm != MPI::COMM_NULL)
    mergecomm.Free();
  if (newgid != MPI::GROUP_NULL)
    newgid.Free();

  pass1++;
}



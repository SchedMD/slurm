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
group()
{
  char msg[150];
  int i;
  int rank;
  int ranks1[256];
  int ranks2[256];
  int result;
  int size;
  MPI::Group group1;
  MPI::Group group2;
  MPI::Group group3;
  MPI::Group newgroup;
  MPI::Intracomm newcomm;
 
  group1 = MPI::GROUP_NULL;
  group2 = MPI::GROUP_NULL;
  group3 = MPI::GROUP_NULL;
  newgroup = MPI::GROUP_NULL;

  newcomm = MPI::COMM_NULL;

  for(i = 0; i < 256; i++) {
    ranks1[i] = -1;
    ranks2[i] = -1;
  }

  Testing("Get_group");

  group1 = MPI::COMM_WORLD.Get_group();
  if(group1 == MPI::GROUP_NULL) {
    sprintf(msg, "NODE %d - 1) ERROR in MPI::Get_group failed, group1 == MPI::GROUP_NULL", my_rank);
    Fail(msg);
  }

  Pass(); // Get_group

  Testing("Get_size");

  size = 0;

  size = group1.Get_size();
  if(size != comm_size) {
    sprintf(msg, "NODE %d - 2) ERROR in MPI::Get_size, size = %d, should be %d", my_rank, size, comm_size);
    Fail(msg);
  }

  Pass(); // Get_size

  Testing("Get_rank");

  rank = 0;

  rank = group1.Get_rank();
  if(rank != my_rank) {
    sprintf(msg, "NODE %d - 3) ERROR in MPI::Get_rank, rank = %d, should be %d", my_rank, rank, my_rank);
    Fail(msg);
  }

  Pass(); // Get_rank
  
  Testing("Compare");

  result = -1;

  result = MPI::Group::Compare(group1, group1);
  if(result != MPI::IDENT) {
    sprintf(msg, "NODE %d - 4) ERROR in MPI::Compare, result = %d, should be %d (MPI::IDENT)", my_rank, result, MPI::IDENT);
    Fail(msg);
  }

  Pass(); // Compare

  Testing("Incl");

  if (my_rank < (comm_size / 2)) {
    for(i = 0; i < comm_size / 2; i++)  
      ranks1[i] = i;
    newgroup = group2 = group1.Incl(comm_size / 2, ranks1);
  } else {
    for(i = comm_size / 2; i < comm_size; i++)  
      ranks1[i - (comm_size / 2)] = i;
    newgroup = group3 = group1.Incl(comm_size / 2, ranks1);
  }
  
  size = 0;

  size = newgroup.Get_size();
  if(size != comm_size / 2) {
    sprintf(msg, "NODE %d - 5) ERROR in MPI::Get_size, size = %d, should be %d", my_rank, size, comm_size);
    Fail(msg);
  }

  result = -1;
  result = MPI::Group::Compare(newgroup, group1);
  if(result != MPI::UNEQUAL) {
    sprintf(msg, "NODE %d - 6) ERROR in MPI::Compare, result = %d, should be %d (MPI::UNEQUAL)", my_rank, result, MPI::UNEQUAL);
    Fail(msg);
  }

  Pass(); // Incl

  Testing("Union");

  group2 = MPI::Group::Union(group1, newgroup);

  result = -1;
  result = MPI::Group::Compare(group1, group2);
  if(result != MPI::IDENT) {
    sprintf(msg, "NODE %d - 7) ERROR in MPI::Compare, result = %d, should be %d (MPI::IDENT)", my_rank, result, MPI::IDENT);
    Fail(msg);
  }

  Pass(); // Union

  Testing("Intersect");

  if(group2 != MPI::GROUP_NULL)
    group2.Free();

  group2 = MPI::Group::Intersect(newgroup,group1);

  result = -1;
  result = MPI::Group::Compare(group2, newgroup);
  if(result != MPI::IDENT) {
    sprintf(msg, "NODE %d - 8) ERROR in MPI::Compare, result = %d, should be %d (MPI::IDENT)", my_rank, result, MPI::IDENT);
    Fail(msg);
  }

  Pass(); // Intersect

  Testing("Difference");

  if(group2 != MPI::GROUP_NULL)
    group2.Free();

  group2 = MPI::Group::Difference(group1,newgroup);
  
  size = 0;
  size = group2.Get_size();
  if(size != comm_size / 2) {
    sprintf(msg, "NODE %d - 9) ERROR in MPI::Get_size, size = %d, should be %d",
	    my_rank, size, comm_size / 2);
    Fail(msg);
  }

  Pass(); // Difference

  Testing("Translate_ranks");

  for(i = 0; i < size; i++)
    ranks1[i] = i;
  
  MPI::Group::Translate_ranks(group2, size, ranks1, group1, ranks2);
  if(my_rank < (comm_size / 2)) {
    for(i = 0; i < size; i++) 
      if(ranks2[i] != comm_size / 2 + i) {
	sprintf(msg, "NODE %d - 10) ERROR in MPI::Translate_ranks, ranks2[%d] = %d, should be %d", my_rank, i, ranks2[i], comm_size / 2 + i);
	Fail(msg);
      }
  } else {
    for(i = 0; i < size; i++) {
      if(ranks2[i] != i) {
	sprintf(msg, "NODE %d - 11) ERROR in MPI::Translate_ranks, ranks2[%d] = %d, should be %d", my_rank, i, ranks2[i], i);
	Fail(msg);
      }
    }
  }

  Pass(); // Translate_ranks

  Testing("Intracomm::Create");

  newcomm = MPI::COMM_WORLD.Create(newgroup);
  if(newcomm != MPI::COMM_NULL) { 
    if(group3 != MPI::GROUP_NULL)
      group3.Free();

    group3 = newcomm.Get_group();
    if(group3 == MPI::GROUP_NULL) {
      sprintf(msg, "NODE %d - 12) ERROR in MPI::Get_group, group3 == MPI::GROUP_NULL, Create Failed!", my_rank);
      Fail(msg);
    }
  } else {
    sprintf(msg, "NODE %d - 13) ERROR in MPI::Create, newcomm == MPI::COMM_NULL", my_rank);
    Fail(msg);
  }

  Pass(); // Create

  Testing("Excl");

  if(my_rank < (comm_size / 2)) {
    if(group3 != MPI::GROUP_NULL)
      group3.Free();

    group3 = group1.Excl(comm_size / 2, ranks1);

    result = -1;
    result = MPI::Group::Compare(group2, group3);
    if(result != MPI::IDENT) {
      sprintf(msg, "NODE %d - 14) ERROR in MPI::Compare, result = %d, should be %d", my_rank, result, MPI::IDENT);
      Fail(msg);
    }

    if(group3 != MPI::GROUP_NULL)
      group3.Free();
  }

  Pass(); // Excl

  MPI::COMM_WORLD.Set_errhandler(MPI::ERRORS_RETURN);

  MPI::Group to_free[3];
  to_free[0] = group2;
  to_free[1] = group3;
  to_free[2] = newgroup;

  int j;
  for (i = 0; i < 2; i++)
    for (j = i + 1; j < 3; j++)
      if (to_free[i] != MPI::GROUP_NULL &&
	  to_free[j] != MPI::GROUP_NULL &&
	  MPI::Group::Compare(to_free[i], to_free[j]) == MPI::IDENT)
        to_free[j] = MPI::GROUP_NULL;
  for (i = 0; i < 3; i++)
    if (to_free[i] != MPI::GROUP_NULL)
      to_free[i].Free();

  group2 = MPI::GROUP_NULL;
  group3 = MPI::GROUP_NULL;
  newgroup = MPI::GROUP_NULL;
  
  if(newcomm != MPI::COMM_NULL && newcomm != MPI::COMM_WORLD)
    newcomm.Free();
}

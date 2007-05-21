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
compare()
{
  char msg[150];
  int compare_color;
  int compare_key;
  int compare_result;
  MPI::Intracomm compare_comm1;
  MPI::Intracomm compare_comm2;

  Testing("Compare - MPI::IDENT");

  compare_comm1 = MPI::COMM_WORLD.Dup();
  
  compare_result = MPI::COMM_WORLD.Compare(compare_comm1, compare_comm1);
  if(compare_result != MPI::IDENT) {
    sprintf(msg, "NODE %d - 1) ERROR in MPI::Compare, compare_result = %d, should be %d (MPI::IDENT)", my_rank, compare_result, MPI::IDENT);
    Fail(msg);
  }

  Pass(); // Compare - MPI::IDENT
  
  Testing("Compare - MPI::CONGRUENT");

  compare_result = MPI::COMM_WORLD.Compare(MPI::COMM_WORLD, compare_comm1);
  if(compare_result != MPI::CONGRUENT) {
    sprintf(msg, "NODE %d - 2) ERROR in MPI::Compare, compare_result = %d, should be %d (MPI::CONGRUENT)", my_rank, compare_result, MPI::CONGRUENT);
    Fail(msg);
  }

  Pass(); // Compare - MPI::CONGRUENT

  Testing("Compare - MPI::SIMILAR");

  compare_color = 1;
  compare_key = -my_rank;

  compare_comm2 = MPI::COMM_WORLD.Split(compare_color, compare_key);
  compare_result = MPI::COMM_WORLD.Compare(compare_comm1, compare_comm2);
  if(compare_result != MPI::SIMILAR) {
    sprintf(msg, "NODE %d - 3) ERROR in MPI::Compare, compare_result = %d, should be %d (MPI::SIMILAR)", my_rank, compare_result, MPI::SIMILAR);
    Fail(msg);
  }
  
  Pass(); // Compare - MPI::SIMILAR

  if(compare_comm2 != MPI::COMM_NULL && compare_comm2 != MPI::COMM_WORLD)
    compare_comm2.Free();

  Testing("Compare - MPI::UNEQUAL");

  compare_color = my_rank;

  compare_comm2 = MPI::COMM_WORLD.Split(compare_color, compare_key);
  compare_result = MPI::COMM_WORLD.Compare(compare_comm1, compare_comm2);
  if(compare_result != MPI::UNEQUAL) {
    sprintf(msg, "NODE %d - 4) ERROR in MPI::Compare, compare_result = %d, should be %d (MPI::UNEQUAL)", my_rank, compare_result, MPI::UNEQUAL);
    Fail(msg);
  }
  
  Pass(); // Compare - MPI::UNEQUAL

  if(compare_comm1 != MPI::COMM_NULL && compare_comm1 != MPI::COMM_WORLD)
    compare_comm1.Free();
  if(compare_comm2 != MPI::COMM_NULL && compare_comm2 != MPI::COMM_WORLD)
    compare_comm2.Free();
}

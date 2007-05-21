// Copyright 1997-1999, University of Notre Dame.
// Authors:  Jeremy G. Siek, Michael P. McNally, Jeffery M. Squyres, 
//           Andrew Lumsdaine
//
// This file is part of the Notre Dame C++ bindings for MPI
//
// You should have received a copy of the License Agreement for the
// Notre Dame C++ bindings for MPI along with the software;  see the
// file LICENSE.  If not, contact Office of Research, University of Notre
// Dame, Notre Dame, IN  46556.
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
#include <iostream.h>
#include <stdlib.h>
#include "mpi++.h"
 
int
main(int argc, char* argv[])
{
   int i,size,myself,ranks1[16],ranks2[16];
   int ranges[10][3];
   MPI::Group group,newgroup;
 
   MPI::Init(argc, argv);
   myself = MPI::COMM_WORLD.Get_rank();
   group = MPI::COMM_WORLD.Get_group();
   size = group.Get_size();
   if(size != 8) { 
     cout << "MUST RUN WITH 8 TASKS\n"; 
     exit(0); 
   }
 
   ranges[0][0] = 1; ranges[0][1] = 4; ranges[0][2] = 1;
   ranges[1][0] = 5; ranges[1][1] = 8; ranges[1][2] = 2;
   newgroup = group.Range_incl(2,ranges);
   size = newgroup.Get_size();
   if(size != 6)  cout << "ERROR: Size = " << size << ", should be 6\n";
   for(i=0;i<6;i++)  ranks1[i] = i;
   MPI::Group::Translate_ranks(newgroup,6,ranks1,group,ranks2);
   if(ranks2[0] != 1 || ranks2[1] != 2 || ranks2[2] != 3 || ranks2[3] != 4
      || ranks2[4] != 5 || ranks2[5] != 7)
     cout << "ERROR: Wrong ranks " << ranks2[0] << " " << ranks2[1] << " "
	  << ranks2[2] << " " << ranks2[3] << " " << ranks2[4] << " "
	  << ranks2[5] << "\n";

   newgroup = group.Range_excl(2,ranges);
   size = newgroup.Get_size();
   if(size != 2)  cout << "ERROR: Size = " << size << ", should be 2\n";
   MPI::Group::Translate_ranks(newgroup,2,ranks1,group,ranks2);
   if(ranks2[0] != 0 || ranks2[1] != 6)
     cout << "ERROR: Wrong ranks " << ranks2[0] << " " << ranks2[1] << "\n";

   ranges[0][0] = 6; ranges[0][1] = 0; ranges[0][2] = -3;
   newgroup = group.Range_incl(1,ranges);
   size = newgroup.Get_size();
   if(size != 3)  cout << "ERROR: Size = " << size << ", should be 3\n";
   for(i=0;i<3;i++)  ranks1[i] = i;
   MPI::Group::Translate_ranks(newgroup,3,ranks1,group,ranks2);
   if(ranks2[0] != 6 || ranks2[1] != 3 || ranks2[2] != 0)
     cout << "ERROR: Wrong ranks " << ranks2[0] << " " << ranks2[1] << " " 
	  << ranks2[2] << "\n";

   newgroup = group.Range_excl(1,ranges);
   size = newgroup.Get_size();
   if(size != 5)  cout << "ERROR: Size = " << size << ", should be 5\n";
   MPI::Group::Translate_ranks(newgroup,5,ranks1,group,ranks2);
   if(ranks2[0] != 1 || ranks2[1] != 2 || ranks2[2] != 4 || ranks2[3] != 5 || ranks2[4] != 7)
     cout << "ERROR: Wrong ranks " << ranks2[0] << " " << ranks2[1] << " " 
	  << ranks2[2] << " " << ranks2[3] << " " << ranks2[4] << "\n";

   MPI::COMM_WORLD.Barrier();
   if(myself == 0)
     cout << "TEST COMPLETE\n";
   MPI::Finalize();
   return 0;
}

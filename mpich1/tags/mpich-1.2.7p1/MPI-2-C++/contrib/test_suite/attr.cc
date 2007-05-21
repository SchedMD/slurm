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
attr()
{
#if _MPIPP_USEEXCEPTIONS_
  int class1;
#endif
  char msg[150];
  int flag;
  int pflag;
  int pkey;
  MPI2CPP_ATTR pkeyval;
  MPI2CPP_ATTR pval;
  MPI2CPP_ATTR val;
  MPI::Intracomm pcomm;
  
  Testing("Get_attr");

  Testing("MPI::TAG_UB");

  flag = 0;
  val = 0;
  pflag = 0;
  pval = 0;

  MPI_Attr_get(MPI_COMM_WORLD, MPI_TAG_UB, &val, &flag);
  if(!flag) {
    sprintf(msg, "NODE %d - 1) ERROR in MPI_Attr_get: no val for MPI_TAG_UB",
	    my_rank);
    Fail(msg);
  }
  pflag = MPI::COMM_WORLD.Get_attr(MPI::TAG_UB, &pval);
  if(!pflag) {
    sprintf(msg, "NODE %d - 2) ERROR in MPI::COMM_WORLD.Get_attr: no val for MPI::TAG_UB", my_rank);
    Fail(msg);
  }
  if(val != pval) {
    sprintf(msg, "NODE %d - 3) ERROR in MPI::COMM_WORLD.Get_attr: tag_ub incorrect", my_rank);
    Fail(msg);
  }

  Pass(); // MPI::TAG_UB

  Testing("MPI::HOST");

  flag = 0;
  val = 0;
  pflag = 0;
  pval = 0;

  MPI_Attr_get(MPI_COMM_WORLD,MPI_HOST,&val,&flag);
  if(!flag) {
    sprintf(msg, "NODE %d - 4) ERROR in MPI_Attr_get: no val for MPI_HOST",
	    my_rank);
    Fail(msg);
  }
  pflag = MPI::COMM_WORLD.Get_attr(MPI::HOST,&pval);
  if(!pflag) {
    sprintf(msg, "NODE %d - 5) ERROR in MPI::COMM_WORLD.Get_attr: no val for MPI::HOST", my_rank);
    Fail(msg);
  }
  if(val != pval) {
    sprintf(msg, "NODE %d - 6) ERROR in MPI::COMM_WORLD.Get_attr: host incorrect", my_rank);
    Fail(msg);
  }

  Pass(); // MPI::HOST

  Testing("MPI::IO");

  flag = 0;
  val = 0;
  pflag = 0;
  pval = 0;

  MPI_Attr_get(MPI_COMM_WORLD,MPI_IO,&val,&flag);
  if(!flag) {
    sprintf(msg, "NODE %d - 7)ERROR in MPI_Attr_get: no val for MPI_IO", my_rank);
    Fail(msg);
  }
  pflag = MPI::COMM_WORLD.Get_attr(MPI::IO,&pval);
  if(!pflag) {
    sprintf(msg, "8) NODE %d - ERROR in MPI::COMM_WORLD.Get_attr: no val for MPI::IO", my_rank);
    Fail(msg);
  }
  if(val != pval) {
    sprintf(msg, "NODE %d - 9) ERROR in MPI::COMM_WORLD.Get_attr: io incorrect", my_rank);
    Fail(msg);
  }

  Pass(); // MPI::IO

  Testing("MPI::WTIME_IS_GLOBAL");

  flag = 0;
  val = 0;
  pflag = 0;
  pval = 0;

  MPI_Attr_get(MPI_COMM_WORLD,MPI_WTIME_IS_GLOBAL,&val,&flag);
  if(!flag) {
    sprintf(msg, "NODE %d - 10)ERROR in MPI_Attr_get: no val for MPI_WTIME_IS_GLOBAL", my_rank);
    Fail(msg);
  }
  pflag = MPI::COMM_WORLD.Get_attr(MPI::WTIME_IS_GLOBAL,&pval);
  if(!pflag) {
    sprintf(msg, "11) NODE %d - ERROR in MPI::COMM_WORLD.Get_attr: no val for MPI::WTIME_IS_GLOBAL", my_rank);
    Fail(msg);
  }
  if(val != pval) {
    sprintf(msg, "NODE %d - 12) ERROR in MPI::COMM_WORLD.Get_attr: wtime_is_global incorrect", my_rank);
    Fail(msg);
  }

  Pass(); // MPI::WTIME_IS_GLOBAL

  Pass(); // Get_attr

  Testing("Comm::Create_keyval");

  pkey = 0;

  pkey = MPI::Comm::Create_keyval(MPI::Comm::NULL_COPY_FN,
				  MPI::Comm::NULL_DELETE_FN, 0);
  if(pkey == 0) {
    sprintf(msg, "NODE %d - 13) ERROR in MPI::COMM_WORLD.Create_keyval: The keys returned were not unique.", my_rank);
    Fail(msg);
  }
  
  Pass(); // Create_keyval

  Testing("Attr_put / Set_attr");

  pcomm = MPI::COMM_WORLD.Dup();

  pval = 12345;

  pcomm.Set_attr(pkey, (void*) pval);

  pkeyval = 0;

  pflag = pcomm.Get_attr(pkey, &pkeyval);
  if(pflag == 0) {
    sprintf(msg, "NODE %d - 14) ERROR in MPI_Attr_get: flag is false", my_rank);
    Fail(msg);
  }
  if(pkeyval != pval) {
    sprintf(msg, "NODE %d - 16) ERROR in pcomm.Get_attr: val incorrect",
	    my_rank);
    Fail(msg);
  }

  Pass(); // Attr_put / Set_attr

  Testing("Delete_attr");

#if _MPIPP_USEEXCEPTIONS_
  pcomm.Set_errhandler(MPI::ERRORS_THROW_EXCEPTIONS);
 
  class1 = MPI::SUCCESS;
  try {
    pcomm.Delete_attr(pkey);
  }
  catch(MPI::Exception e) {
    class1 = e.Get_error_class();
  }
  if(class1 != MPI::SUCCESS) {
    sprintf(msg, "NODE %d - 22) ERROR in MPI_Attr_delete, pkeyval not deleted",
	    my_rank);
    Fail(msg);
  }

  pcomm.Set_errhandler(MPI::ERRORS_RETURN);

  Pass(); // Delete_attr
#else
  // It is erroneous not to delete the attribute, so we have to hope
  // that it doesn't fail!
  pcomm.Delete_attr(pkey);
  Done("Compiler does not have exceptions");
#endif

  Testing("MPI::COMM_WORLD.Free_keyval");

  MPI::Comm::Free_keyval(pkey);
  if(pkey != MPI::KEYVAL_INVALID) {
    sprintf(msg, "NODE %d - 17) ERROR in MPI::COMM_WORLD.Free_keyval: key not set to INVALID", my_rank);
    Fail(msg);
  }

  Pass(); // MPI::COMM_WORLD.Free_keyval

  if (pcomm != MPI::COMM_NULL && pcomm != MPI::COMM_WORLD) {
    pcomm.Free();
  }
}

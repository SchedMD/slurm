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
// The vast majority of this awesome file came from Jeff Squyres,
// Perpetual Obsessive Notre Dame Student Craving Utter Madness, 
// and Brian McCandless, another of the LSC crew, under the guidance
// of Herr Doctor Boss Andrew Lumsdaine. My thanks for making my
// life a whole lot easier.

#include "mpi2c++_test.h"
#include <iostream.h>

void
errhandler()
{
#if _MPIPP_USEEXCEPTIONS_
  int i;
  MPI2CPP_BOOL_T skip = MPI2CPP_FALSE;
  MPI2CPP_BOOL_T pass = MPI2CPP_FALSE;
#endif

  MPI::Intracomm a;

  a = MPI::COMM_NULL;

  Testing("ERRORS_THROW_EXCEPTIONS");

  if (flags[SKIP_CRAY1104])
    Done("Skipped (CRAY 1.1.0.4)");
  else if (flags[SKIP_SGI20])
    Done("Skipped (SGI 2.0)");
  else if (flags[SKIP_SGI30])
    Done("Skipped (SGI 3.0)");
  else if (flags[SKIP_NO_THROW])
    Done("Skipped (compiler exceptions broken)");
  else
    {
#if _MPIPP_USEEXCEPTIONS_
      MPI::COMM_WORLD.Set_errhandler(MPI::ERRORS_THROW_EXCEPTIONS);
      
      try {
#if MPI2CPP_SGI20
	// The negative rank will cause SGI 2.0 to fail (we think --
	// don't have 2.0 around any more to test with.  Users should
	// upgrade anyway...)
	a.Send(&i, -1, MPI::DATATYPE_NULL, -my_rank, 201);
#elif MPI2CPP_SGI30
	// SGI 3.0 requires MPI_CHECK_ARGS to be set before we call
	// MPI_INIT()
	if (getenv("MPI_CHECK_ARGS") == 0) {
	  if (my_rank == 0) {
	    cout << endl << endl 
		 << "The MPI-2 C++ test suite depends on the MPI_CHECK_ARGS"
		 << endl
		 << "environment variable being set to \"1\" *before* mpirun"
		 << endl
		 << "is invoked for successful testing. The test suite will"
		 << endl
		 << "now exit since MPI_CHECK_ARGS is not currently set. Set"
		 << endl
		 << "the MPI_CHECK_ARGS variable and re-run the MPI-2 C++"
		 << endl
		 << "test suite." << endl << endl;
	  }
	  skip = MPI2CPP_TRUE;
	}
	else
	  MPI::COMM_WORLD.Send(&i, 1, MPI::DATATYPE_NULL, my_rank, 201);
#elif MPI2CPP_LAM
	// LAM will definitely fail on the negative tag
	a.Send(&i, 1, MPI::DATATYPE_NULL, my_rank, -201);
#else
	// All others should fail with an MPI_DATATYPE_NULL
	a.Send(&i, 1, MPI::DATATYPE_NULL, my_rank, 201);
#endif
      }
      catch (MPI::Exception& e) {
	pass = MPI2CPP_TRUE;
      }
      if (skip)
	Fail("MPI_CHECK_ARGS not set");
      else if (!pass)
	Fail("Exception not caught");
      else
	Pass();
      
      MPI::COMM_WORLD.Set_errhandler(MPI::ERRORS_RETURN);
#else
      Done("Compiler does not have exceptions");
#endif
    }

  if(a != MPI::COMM_NULL && a != MPI::COMM_WORLD)
    a.Free();
}


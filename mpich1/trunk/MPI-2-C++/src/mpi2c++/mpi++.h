// -*- c++ -*-
//
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
//

#ifndef MPIPP_H
#define MPIPP_H

// 
// Let's ensure that we're really in C++, and some errant programmer
// hasn't included <mpi++.h> just "for completeness"
//

#if defined(__cplusplus) || defined(c_plusplus) 

#include "mpi2c++/mpi2c++_config.h"
#include <mpi.h>

#include <stdarg.h>
#include "mpi2c++/mpi2c++_map.h"


//JGS: this is used for implementing user functions for MPI::Op
extern "C" void
op_intercept(void *invec, void *outvec, int *len, MPI_Datatype *datatype);

#if MPI2CPP_IBM_SP
//Here's the sp2 typedeffrom their header file:
//  typedef void MPI_Handler_function(MPI_Comm *,int *,char *,int *,int *);
extern "C" void
errhandler_intercept(MPI_Comm * mpi_comm, int * err, char*, int*, int*);

extern "C" void
throw_excptn_fctn(MPI_Comm* comm, int* errcode, char*, int*, int*);

#else

//JGS: this is used as the MPI_Handler_function for
// the mpi_errhandler in ERRORS_THROW_EXCEPTIONS
extern "C" void
throw_excptn_fctn(MPI_Comm* comm, int* errcode, ...);

extern "C" void
errhandler_intercept(MPI_Comm * mpi_comm, int * err, ...);
#endif


//used for attr intercept functions
enum CommType { eIntracomm, eIntercomm, eCartcomm, eGraphcomm};

extern "C" int
copy_attr_intercept(MPI_Comm oldcomm, int keyval, 
		    void *extra_state, void *attribute_val_in, 
		    void *attribute_val_out, int *flag);

extern "C" int
delete_attr_intercept(MPI_Comm comm, int keyval, 
		      void *attribute_val, void *extra_state);


#if _MPIPP_PROFILING_
#include "mpi2c++/pmpi++.h"
#endif

#if _MPIPP_USENAMESPACE_
namespace MPI {
#else
class MPI {
public:
#endif

#if ! _MPIPP_USEEXCEPTIONS_
  _MPIPP_EXTERN_ _MPIPP_STATIC_ int errno;
#endif

  class Comm_Null;
  class Comm;
  class Intracomm;
  class Intercomm;
  class Graphcomm;
  class Cartcomm;
  class Datatype;
  class Errhandler;
  class Group;
  class Op;
  class Request;
  class Status;

  typedef MPI_Aint Aint;

#include "mpi2c++/constants.h"
#include "mpi2c++/functions.h"
#include "mpi2c++/datatype.h"

  typedef void User_function(const void* invec, void* inoutvec, int len,
			     const Datatype& datatype);

#include "mpi2c++/exception.h"
#include "mpi2c++/op.h"
#include "mpi2c++/status.h"
#include "mpi2c++/request.h"   //includes class Prequest
#include "mpi2c++/group.h" 
#include "mpi2c++/comm.h"
#include "mpi2c++/errhandler.h"
#include "mpi2c++/intracomm.h"
#include "mpi2c++/topology.h"  //includes Cartcomm and Graphcomm
#include "mpi2c++/intercomm.h"
  
#if ! _MPIPP_USENAMESPACE_
private:
  MPI() { }
};
#else
}
#endif

#if _MPIPP_PROFILING_
#include "mpi2c++/pop_inln.h"
#include "mpi2c++/pgroup_inln.h"
#include "mpi2c++/pstatus_inln.h"
#include "mpi2c++/prequest_inln.h"
#endif

//
// These are the "real" functions, whether prototyping is enabled
// or not. These functions are assigned to either the MPI::XXX class
// or the PMPI::XXX class based on the value of the macro _REAL_MPI_
// which is set in mpi2c++_config.h.
// If prototyping is enabled, there is a top layer that calls these
// PMPI functions, and this top layer is in the XXX.cc files.
//



#include "mpi2c++/datatype_inln.h"
#include "mpi2c++/functions_inln.h"
#include "mpi2c++/request_inln.h"
#include "mpi2c++/comm_inln.h"
#include "mpi2c++/intracomm_inln.h"
#include "mpi2c++/topology_inln.h"
#include "mpi2c++/intercomm_inln.h"
#include "mpi2c++/group_inln.h"
#include "mpi2c++/op_inln.h"
#include "mpi2c++/errhandler_inln.h"
#include "mpi2c++/status_inln.h"

#endif
#endif

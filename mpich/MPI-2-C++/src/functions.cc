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

#include "mpi++.h"

#if _MPIPP_PROFILING_

//
// Point-to-Point Communication
//

void 
MPI::Attach_buffer(void* buffer, int size)
{
  PMPI::Attach_buffer(buffer, size);
}

int 
MPI::Detach_buffer(void*& buffer)
{
  return PMPI::Detach_buffer(buffer);
}

//
// Process Topologies
//

void
MPI::Compute_dims(int nnodes, int ndims, int dims[])
{
  PMPI::Compute_dims(nnodes, ndims, dims);
}


//
// Environmental Inquiry
//

void 
MPI::Get_processor_name(char*& name, int& resultlen)
{
  PMPI::Get_processor_name(name, resultlen);
}

void
MPI::Get_error_string(int errorcode, char* string, int& resultlen)
{
  PMPI::Get_error_string(errorcode, string, resultlen);
}

int 
MPI::Get_error_class(int errorcode) 
{
  return PMPI::Get_error_class(errorcode);
}

double 
MPI::Wtime()
{
  return PMPI::Wtime();
}

double 
MPI::Wtick()
{
  return PMPI::Wtick();
}


void
MPI::Init(int& argc, char**& argv)
{
  PMPI::Init(argc, argv);
  // (void)MPI_Init(&argc, &argv);
  // MPI::ERRORS_THROW_EXCEPTIONS.init(); 
}


// This causes problems with Solaris CC compiler
void
MPI::Init()
{
  PMPI::Init();
}


void
MPI::Finalize()
{
  PMPI::Finalize();
}

MPI2CPP_BOOL_T
MPI::Is_initialized()
{
  return PMPI::Is_initialized();
}

//
// Profiling
//

void
MPI::Pcontrol(const int level, ...)
{
  va_list ap;
  va_start(ap, level);
 
  PMPI::Pcontrol(level, ap);
  va_end(ap);
}

#if MPI2CPP_HAVE_MPI_GET_VERSION
void
MPI::Get_version(int& version, int& subversion)
{
  PMPI::Get_version(version, subversion);
}
#endif

MPI::Aint
MPI::Get_address(void* location)
{
  return PMPI::Get_address(location);
}

#endif

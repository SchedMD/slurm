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

//
// Point-to-Point Communication
//

_MPIPP_STATIC_ void 
Attach_buffer(void* buffer, int size);

_MPIPP_STATIC_ int 
Detach_buffer(void*& buffer);

//
// Process Topologies
//

_MPIPP_STATIC_ void
Compute_dims(int nnodes, int ndims, int dims[]);

//
// Environmental Inquiry
//

_MPIPP_STATIC_ void 
Get_processor_name(char*& name, int& resultlen);

_MPIPP_STATIC_ void
Get_error_string(int errorcode, char* string, int& resultlen);

_MPIPP_STATIC_ int 
Get_error_class(int errorcode);

_MPIPP_STATIC_ double 
Wtime();

_MPIPP_STATIC_ double 
Wtick();

_MPIPP_STATIC_ void
Init(int& argc, char**& argv);

_MPIPP_STATIC_ void
Init();

_MPIPP_STATIC_ void
Real_init();

_MPIPP_STATIC_ void
Finalize();

_MPIPP_STATIC_ MPI2CPP_BOOL_T
Is_initialized();

//
// Profiling
//

_MPIPP_STATIC_ void
Pcontrol(const int level, ...);


#if MPI2CPP_HAVE_MPI_GET_VERSION
_MPIPP_STATIC_ void
Get_version(int& version, int& subversion);
#endif

_MPIPP_STATIC_ _REAL_MPI_::Aint
Get_address(void* location);

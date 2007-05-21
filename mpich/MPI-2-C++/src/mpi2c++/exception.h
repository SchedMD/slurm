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

class Exception {
public:

#if _MPIPP_PROFILING_

  inline Exception(int ec) : pmpi_exception(ec) { }

  int Get_error_code() const;
  
  int Get_error_class() const;
  
  const char* Get_error_string() const;
 
#else

  inline Exception(int ec) : error_code(ec), error_string(0), error_class(-1) {
    (void)MPI_Error_class(error_code, &error_class);
    int resultlen;
    error_string = new char[MAX_ERROR_STRING];
    (void)MPI_Error_string(error_code, error_string, &resultlen);
  }
  inline ~Exception() {
    delete[] error_string;
  }
  // Better put in a copy constructor here since we have a string;
  // copy by value (from the default copy constructor) would be
  // disasterous.
  inline Exception(const Exception& a)
    : error_code(a.error_code), error_class(a.error_class)
  {
    error_string = new char[MAX_ERROR_STRING];
    // Rather that force an include of <string.h>, especially this
    // late in the game (recall that this file is included deep in
    // other .h files), we'll just do the copy ourselves.
    for (int i = 0; i < MAX_ERROR_STRING; i++)
      error_string[i] = a.error_string[i];
  }

  inline int Get_error_code() const { return error_code; }

  inline int Get_error_class() const { return error_class; }
  
  inline const char* Get_error_string() const { return error_string; }

#endif
 
protected:
#if _MPIPP_PROFILING_
  PMPI::Exception pmpi_exception;
#else
  int error_code;
  char* error_string;
  int error_class;
#endif
};

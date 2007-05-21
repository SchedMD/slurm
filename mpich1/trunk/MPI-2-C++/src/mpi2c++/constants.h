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


// return  codes
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int SUCCESS;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int ERR_BUFFER;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int ERR_COUNT;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int ERR_TYPE;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int ERR_TAG ;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int ERR_COMM;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int ERR_RANK;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int ERR_REQUEST;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int ERR_ROOT;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int ERR_GROUP;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int ERR_OP;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int ERR_TOPOLOGY;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int ERR_DIMS;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int ERR_ARG;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int ERR_UNKNOWN;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int ERR_TRUNCATE;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int ERR_OTHER;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int ERR_INTERN;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int ERR_PENDING;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int ERR_IN_STATUS;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int ERR_LASTCODE;

// assorted constants
_MPIPP_EXTERN_ _MPIPP_STATIC_ const void* BOTTOM;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int PROC_NULL;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int ANY_SOURCE;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int ANY_TAG;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int UNDEFINED;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int BSEND_OVERHEAD;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int KEYVAL_INVALID;

// error-handling specifiers
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Errhandler  ERRORS_ARE_FATAL;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Errhandler  ERRORS_RETURN;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Errhandler  ERRORS_THROW_EXCEPTIONS;

// maximum sizes for strings
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int MAX_PROCESSOR_NAME;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int MAX_ERROR_STRING;

// elementary datatypes (C / C++)
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype CHAR;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype SHORT;          
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype INT;            
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype LONG;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype SIGNED_CHAR;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype UNSIGNED_CHAR;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype UNSIGNED_SHORT; 
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype UNSIGNED;       
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype UNSIGNED_LONG;  
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype FLOAT;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype DOUBLE;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype LONG_DOUBLE;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype BYTE;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype PACKED;

// datatypes for reductions functions (C / C++)
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype FLOAT_INT;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype DOUBLE_INT;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype LONG_INT;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype TWOINT;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype SHORT_INT;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype LONG_DOUBLE_INT;

#if MPI2CPP_FORTRAN
// elementary datatype (Fortran)
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype INTEGER;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype REAL;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype DOUBLE_PRECISION;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype F_COMPLEX;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype LOGICAL;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype CHARACTER;

// datatype for reduction functions (Fortran)
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype TWOREAL;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype TWODOUBLE_PRECISION;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype TWOINTEGER;
#endif

#if MPI2CPP_ALL_OPTIONAL_FORTRAN
// optional datatypes (Fortran)
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype INTEGER1;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype INTEGER2;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype INTEGER4;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype REAL2;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype REAL4;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype REAL8;
#elif MPI2CPP_SOME_OPTIONAL_FORTRAN
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype INTEGER2;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype REAL2;
#endif

#if MPI2CPP_OPTIONAL_C
// optional datatype (C / C++)
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype LONG_LONG;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype UNSIGNED_LONG_LONG;
#endif

#if 0
// c++ types
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype BOOL;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype COMPLEX;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype DOUBLE_COMPLEX;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype LONG_DOUBLE_COMPLEX;
#endif

// special datatypes for contstruction of derived datatypes
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype UB;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype LB;

// reserved communicators
// JGS these can not be const because Set_errhandler is not const
_MPIPP_EXTERN_ _MPIPP_STATIC_ Intracomm COMM_WORLD;
_MPIPP_EXTERN_ _MPIPP_STATIC_ Intracomm COMM_SELF;

// results of communicator and group comparisons
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int IDENT;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int CONGRUENT;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int SIMILAR;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int UNEQUAL;

// environmental inquiry keys
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int TAG_UB;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int IO;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int HOST;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int WTIME_IS_GLOBAL;

// collective operations
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Op MAX;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Op MIN;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Op SUM;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Op PROD;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Op MAXLOC;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Op MINLOC;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Op BAND;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Op BOR;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Op BXOR;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Op LAND;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Op LOR;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Op LXOR;

// null handles
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Group        GROUP_NULL;
//_MPIPP_EXTERN_ _MPIPP_STATIC_ const Comm         COMM_NULL;
//_MPIPP_EXTERN_ _MPIPP_STATIC_ const MPI_Comm     COMM_NULL;
_MPIPP_EXTERN_ _MPIPP_STATIC_ Comm_Null          COMM_NULL;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Datatype     DATATYPE_NULL;
_MPIPP_EXTERN_ _MPIPP_STATIC_ Request            REQUEST_NULL;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Op           OP_NULL;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Errhandler   ERRHANDLER_NULL;  

// empty group
_MPIPP_EXTERN_ _MPIPP_STATIC_ const Group  GROUP_EMPTY;

// topologies
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int GRAPH;
_MPIPP_EXTERN_ _MPIPP_STATIC_ const int CART;



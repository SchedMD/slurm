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

inline void
_REAL_MPI_::Request::Wait(_REAL_MPI_::Status &status) 
{
  (void)MPI_Wait(&mpi_request, &status.mpi_status);
}

inline void
_REAL_MPI_::Request::Wait() 
{
#if MPI2CPP_HAVE_STATUS_IGNORE
  (void)MPI_Wait(&mpi_request, MPI_STATUS_IGNORE);
#else
  (void)MPI_Wait(&mpi_request, &ignored_status.mpi_status);
#endif
}

inline void
_REAL_MPI_::Request::Free() 
{
  (void)MPI_Request_free(&mpi_request);
}

inline MPI2CPP_BOOL_T
_REAL_MPI_::Request::Test(_REAL_MPI_::Status &status) 
{
  int t;
  (void)MPI_Test(&mpi_request, &t, &status.mpi_status);
  return (MPI2CPP_BOOL_T) t;
}

inline MPI2CPP_BOOL_T
_REAL_MPI_::Request::Test() 
{
  int t;
#if MPI2CPP_HAVE_STATUS_IGNORE
  (void)MPI_Test(&mpi_request, &t, MPI_STATUS_IGNORE);
#else
  (void)MPI_Test(&mpi_request, &t, &ignored_status.mpi_status);
#endif
  return (MPI2CPP_BOOL_T) t;
}

inline int
_REAL_MPI_::Request::Waitany(int count, _REAL_MPI_::Request array[],
			     _REAL_MPI_::Status& status)
{
  int index, i;
  MPI_Request* array_of_requests = new MPI_Request[count];
  for (i=0; i < count; i++)
    array_of_requests[i] = array[i];
  (void)MPI_Waitany(count, array_of_requests, &index, &status.mpi_status);
  for (i=0; i < count; i++)
    array[i] = array_of_requests[i];
  delete [] array_of_requests;
  return index;
}

inline int
_REAL_MPI_::Request::Waitany(int count, _REAL_MPI_::Request array[])
{
  int index, i;
  MPI_Request* array_of_requests = new MPI_Request[count];
  for (i=0; i < count; i++)
    array_of_requests[i] = array[i];
#if MPI2CPP_HAVE_STATUS_IGNORE
  (void)MPI_Waitany(count, array_of_requests, &index, MPI_STATUS_IGNORE);
#else
  (void)MPI_Waitany(count, array_of_requests, &index, 
		    &ignored_status.mpi_status);
#endif
  for (i=0; i < count; i++)
    array[i] = array_of_requests[i];
  delete [] array_of_requests;
  return index; //JGS, Waitany return value
}

inline MPI2CPP_BOOL_T
_REAL_MPI_::Request::Testany(int count, _REAL_MPI_::Request array[],
			     int& index, _REAL_MPI_::Status& status)
{
  int i, flag;
  MPI_Request* array_of_requests = new MPI_Request[count];
  for (i=0; i < count; i++)
    array_of_requests[i] = array[i];
  (void)MPI_Testany(count, array_of_requests, &index, &flag, &status.mpi_status);
  for (i=0; i < count; i++)
    array[i] = array_of_requests[i];
  delete [] array_of_requests;
  return (MPI2CPP_BOOL_T)flag;
}

inline MPI2CPP_BOOL_T
_REAL_MPI_::Request::Testany(int count, _REAL_MPI_::Request array[], int& index)
{
  int i, flag;
  MPI_Request* array_of_requests = new MPI_Request[count];
  for (i=0; i < count; i++)
    array_of_requests[i] = array[i];
#if MPI2CPP_HAVE_STATUS_IGNORE
  (void)MPI_Testany(count, array_of_requests, &index, &flag, 
		    MPI_STATUS_IGNORE);
#else
  (void)MPI_Testany(count, array_of_requests, &index, &flag, 
		    &ignored_status.mpi_status);
#endif
  for (i=0; i < count; i++)
    array[i] = array_of_requests[i];
  delete [] array_of_requests;
  return (MPI2CPP_BOOL_T)flag;
}

inline void
_REAL_MPI_::Request::Waitall(int count, _REAL_MPI_::Request req_array[],
			     _REAL_MPI_::Status stat_array[])
{
  int i;
  MPI_Request* array_of_requests = new MPI_Request[count];
  MPI_Status* array_of_statuses = new MPI_Status[count];
  for (i=0; i < count; i++)
    array_of_requests[i] = req_array[i];
  (void)MPI_Waitall(count, array_of_requests, array_of_statuses);
  for (i=0; i < count; i++)
    req_array[i] = array_of_requests[i];
  for (i=0; i < count; i++)
    stat_array[i] = array_of_statuses[i];
  delete [] array_of_requests;
  delete [] array_of_statuses;
}

inline void
_REAL_MPI_::Request::Waitall(int count, _REAL_MPI_::Request req_array[])
{
  int i;
  MPI_Request* array_of_requests = new MPI_Request[count];
#if !MPI2CPP_HAVE_STATUS_IGNORE
  MPI_Status* array_of_statuses = new MPI_Status[count];
#endif
  for (i=0; i < count; i++)
    array_of_requests[i] = req_array[i];
#if MPI2CPP_HAVE_STATUSES_IGNORE
  (void)MPI_Waitall(count, array_of_requests, MPI_STATUSES_IGNORE);
#else
  (void)MPI_Waitall(count, array_of_requests, array_of_statuses);
#endif
  for (i=0; i < count; i++)
    req_array[i] = array_of_requests[i];
  delete [] array_of_requests;
#if !MPI2CPP_HAVE_STATUS_IGNORE
  delete [] array_of_statuses;
#endif
} 

inline MPI2CPP_BOOL_T
_REAL_MPI_::Request::Testall(int count, _REAL_MPI_::Request req_array[],
			     _REAL_MPI_::Status stat_array[])
{
  int i, flag;
  MPI_Request* array_of_requests = new MPI_Request[count];
  MPI_Status* array_of_statuses = new MPI_Status[count];
  for (i=0; i < count; i++)
    array_of_requests[i] = req_array[i];
  (void)MPI_Testall(count, array_of_requests, &flag, array_of_statuses);
  for (i=0; i < count; i++)
    req_array[i] = array_of_requests[i];
  for (i=0; i < count; i++)
    stat_array[i] = array_of_statuses[i];
  delete [] array_of_requests;
  delete [] array_of_statuses;
  return (MPI2CPP_BOOL_T) flag;
}

inline MPI2CPP_BOOL_T
_REAL_MPI_::Request::Testall(int count, _REAL_MPI_::Request req_array[])
{
  int i, flag;
  MPI_Request* array_of_requests = new MPI_Request[count];
#if !MPI2CPP_HAVE_STATUS_IGNORE
  MPI_Status* array_of_statuses = new MPI_Status[count];
#endif
  for (i=0; i < count; i++)
    array_of_requests[i] = req_array[i];
#if MPI2CPP_HAVE_STATUSES_IGNORE
  (void)MPI_Testall(count, array_of_requests, &flag, MPI_STATUSES_IGNORE);
#else
  (void)MPI_Testall(count, array_of_requests, &flag, array_of_statuses);
#endif
  for (i=0; i < count; i++)
    req_array[i] = array_of_requests[i];
  delete [] array_of_requests;
#if !MPI2CPP_HAVE_STATUS_IGNORE
  delete [] array_of_statuses;
#endif
  return (MPI2CPP_BOOL_T) flag;
} 

inline int
_REAL_MPI_::Request::Waitsome(int incount, _REAL_MPI_::Request req_array[],
			      int array_of_indices[], _REAL_MPI_::Status stat_array[]) 
{
  int i, outcount;
  MPI_Request* array_of_requests = new MPI_Request[incount];
  MPI_Status* array_of_statuses = new MPI_Status[incount];
  for (i=0; i < incount; i++)
    array_of_requests[i] = req_array[i];
  (void)MPI_Waitsome(incount, array_of_requests, &outcount,
		     array_of_indices, array_of_statuses);
  for (i=0; i < incount; i++)
    req_array[i] = array_of_requests[i];
  for (i=0; i < incount; i++)
    stat_array[i] = array_of_statuses[i];
  delete [] array_of_requests;
  delete [] array_of_statuses;
  return outcount;
}

inline int
_REAL_MPI_::Request::Waitsome(int incount, _REAL_MPI_::Request req_array[],
			      int array_of_indices[]) 
{
  int i, outcount;
  MPI_Request* array_of_requests = new MPI_Request[incount];
#if !MPI2CPP_HAVE_STATUS_IGNORE
  MPI_Status* array_of_statuses = new MPI_Status[incount];
#endif
  for (i=0; i < incount; i++)
    array_of_requests[i] = req_array[i];
#if MPI2CPP_HAVE_STATUS_IGNORE
  (void)MPI_Waitsome(incount, array_of_requests, &outcount,
		     array_of_indices, MPI_STATUSES_IGNORE);
#else
  (void)MPI_Waitsome(incount, array_of_requests, &outcount,
		     array_of_indices, array_of_statuses);
#endif
  for (i=0; i < incount; i++)
    req_array[i] = array_of_requests[i];
  delete [] array_of_requests;
#if !MPI2CPP_HAVE_STATUS_IGNORE
  delete [] array_of_statuses;
#endif
  return outcount;
}

inline int
_REAL_MPI_::Request::Testsome(int incount, _REAL_MPI_::Request req_array[],
			      int array_of_indices[], _REAL_MPI_::Status stat_array[]) 
{
  int i, outcount;
  MPI_Request* array_of_requests = new MPI_Request[incount];
  MPI_Status* array_of_statuses = new MPI_Status[incount];
  for (i=0; i < incount; i++)
    array_of_requests[i] = req_array[i];
  (void)MPI_Testsome(incount, array_of_requests, &outcount,
		     array_of_indices, array_of_statuses);
  for (i=0; i < incount; i++)
    req_array[i] = array_of_requests[i];
  for (i=0; i < incount; i++)
    stat_array[i] = array_of_statuses[i];
  delete [] array_of_requests;
  delete [] array_of_statuses;
  return outcount;
}

inline int
_REAL_MPI_::Request::Testsome(int incount, _REAL_MPI_::Request req_array[],
			      int array_of_indices[]) 
{
  int i, outcount;
  MPI_Request* array_of_requests = new MPI_Request[incount];
#if !MPI2CPP_HAVE_STATUS_IGNORE
  MPI_Status* array_of_statuses = new MPI_Status[incount];
#endif
  for (i=0; i < incount; i++)
    array_of_requests[i] = req_array[i];
#if MPI2CPP_HAVE_STATUS_IGNORE
  (void)MPI_Testsome(incount, array_of_requests, &outcount,
		     array_of_indices, MPI_STATUSES_IGNORE);
#else
  (void)MPI_Testsome(incount, array_of_requests, &outcount,
		     array_of_indices, array_of_statuses);
#endif
  for (i=0; i < incount; i++)
    req_array[i] = array_of_requests[i];
  delete [] array_of_requests;
#if !MPI2CPP_HAVE_STATUS_IGNORE
  delete [] array_of_statuses;
#endif
  return outcount;
}

inline void
_REAL_MPI_::Request::Cancel(void) const
{
  (void)MPI_Cancel((MPI_Request*)&mpi_request);
}

inline void
_REAL_MPI_::Prequest::Start()
{
  (void)MPI_Start(&mpi_request);
}

inline void
_REAL_MPI_::Prequest::Startall(int count, _REAL_MPI_:: Prequest array_of_requests[])
{
  //convert the array of Prequests to an array of MPI_requests
  MPI_Request* mpi_requests = new MPI_Request[count];
  int i;
  for (i=0; i < count; i++) {
    mpi_requests[i] = array_of_requests[i];
  }
  (void)MPI_Startall(count, mpi_requests); 
  for (i=0; i < count; i++)
    array_of_requests[i].mpi_request = mpi_requests[i] ;
  delete [] mpi_requests;
} 


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

//
// Point-to-Point Communication
//

void MPI::Request::Wait(MPI::Status &status) 
{
  pmpi_request.Wait(status.pmpi_status);
}

void MPI::Request::Wait() 
{
  pmpi_request.Wait();
}

MPI2CPP_BOOL_T MPI::Request::Test(MPI::Status &status) 
{
  return pmpi_request.Test(status.pmpi_status);
}

MPI2CPP_BOOL_T MPI::Request::Test() 
{
  return pmpi_request.Test();
}

void MPI::Request::Free(void) 
{
  pmpi_request.Free();
}

int MPI::Request::Waitany(int count, MPI::Request array[],
			  MPI::Status& status)
{
  int ret, i;
  PMPI::Request* pmpi_array = new PMPI::Request[count];
  for (i=0; i < count; i++)
    pmpi_array[i] = array[i].pmpi_request;
  ret = PMPI::Request::Waitany(count, pmpi_array, status.pmpi_status);
  for (i=0; i < count; i++)
    array[i].pmpi_request = pmpi_array[i] ;
  delete [] pmpi_array;
  return ret;
}

int MPI::Request::Waitany(int count, MPI::Request array[])
{
  int ret, i;
  PMPI::Request* pmpi_array = new PMPI::Request[count];
  for (i=0; i < count; i++)
    pmpi_array[i] = array[i].pmpi_request;
  ret = PMPI::Request::Waitany(count, pmpi_array);
  for (i=0; i < count; i++)
    array[i].pmpi_request = pmpi_array[i] ;
  delete [] pmpi_array;
  return ret;
}

MPI2CPP_BOOL_T MPI::Request::Testany(int count, MPI::Request array[],
			   int& index, MPI::Status& status)
{
  MPI2CPP_BOOL_T ret;
  int i;
  PMPI::Request* pmpi_array = new PMPI::Request[count];
  for (i=0; i < count; i++)
    pmpi_array[i] = array[i].pmpi_request;
  ret = PMPI::Request::Testany(count, pmpi_array, index, status.pmpi_status);
  for (i=0; i < count; i++)
    array[i].pmpi_request = pmpi_array[i] ;
  delete [] pmpi_array;
  return ret;
}

MPI2CPP_BOOL_T MPI::Request::Testany(int count, MPI::Request array[], int& index)
{
  MPI2CPP_BOOL_T ret;
  int i;
  PMPI::Request* pmpi_array = new PMPI::Request[count];
  for (i=0; i < count; i++)
    pmpi_array[i] = array[i].pmpi_request;
  ret = PMPI::Request::Testany(count, pmpi_array, index);
  for (i=0; i < count; i++)
    array[i].pmpi_request = pmpi_array[i] ;
  delete [] pmpi_array;
  return ret;
}

void MPI::Request::Waitall(int count, MPI::Request array[],
			   MPI::Status stat_array[])
{
  PMPI::Request* pmpi_array = new PMPI::Request[count];
  int i;
  for (i=0; i < count; i++)
    pmpi_array[i] = array[i].pmpi_request;
  PMPI::Status* pmpi_status = new PMPI::Status[count];
  for (i=0; i < count; i++)
    pmpi_status[i] = stat_array[i].pmpi_status;
  PMPI::Request::Waitall(count, pmpi_array, pmpi_status);
  for (i=0; i < count; i++)
    array[i].pmpi_request = pmpi_array[i] ;
  for (i=0; i < count; i++)
    stat_array[i] = pmpi_status[i];
  delete [] pmpi_array;
  delete [] pmpi_status;
}
 
void MPI::Request::Waitall(int count, MPI::Request array[])
{
  int i;
  PMPI::Request* pmpi_array = new PMPI::Request[count];
  for (i=0; i < count; i++)
    pmpi_array[i] = array[i].pmpi_request;
  PMPI::Request::Waitall(count, pmpi_array);
  for (i=0; i < count; i++)
    array[i].pmpi_request = pmpi_array[i] ;
  delete [] pmpi_array;
} 

MPI2CPP_BOOL_T MPI::Request::Testall(int count, MPI::Request array[],
			   MPI::Status stat_array[])
{
  MPI2CPP_BOOL_T ret;
  int i;
  PMPI::Request* pmpi_array = new PMPI::Request[count];
  for (i=0; i < count; i++)
    pmpi_array[i] = array[i].pmpi_request;
  PMPI::Status* pmpi_status = new PMPI::Status[count];
  for (i=0; i < count; i++)
    pmpi_status[i] = stat_array[i].pmpi_status;
  ret = PMPI::Request::Testall(count, pmpi_array, pmpi_status);
  for (i=0; i < count; i++)
    array[i].pmpi_request = pmpi_array[i] ;
  for (i=0; i < count; i++)
    stat_array[i] = pmpi_status[i];
  delete [] pmpi_array;
  delete [] pmpi_status;
  return ret;
}
 
MPI2CPP_BOOL_T MPI::Request::Testall(int count, MPI::Request array[])
{
  MPI2CPP_BOOL_T ret;
  int i;
  PMPI::Request* pmpi_array = new PMPI::Request[count];
  for (i=0; i < count; i++)
    pmpi_array[i] = array[i].pmpi_request;
  ret = PMPI::Request::Testall(count, pmpi_array);
  for (i=0; i < count; i++)
    array[i].pmpi_request = pmpi_array[i] ;
  delete [] pmpi_array;
  return ret;
} 

int MPI::Request::Waitsome(int count, MPI::Request array[],
			   int array_of_indices[], MPI::Status stat_array[]) 
{
  int ret;
  int i;
  PMPI::Request* pmpi_array = new PMPI::Request[count];
  for (i=0; i < count; i++)
    pmpi_array[i] = array[i].pmpi_request;
  PMPI::Status* pmpi_status = new PMPI::Status[count];
  for (i=0; i < count; i++)
    pmpi_status[i] = stat_array[i].pmpi_status;
  ret = PMPI::Request::Waitsome(count, pmpi_array, array_of_indices, pmpi_status);
  for (i=0; i < count; i++)
    array[i].pmpi_request = pmpi_array[i] ;
  for (i=0; i < count; i++)
    stat_array[i] = pmpi_status[i];
  delete [] pmpi_array;
  delete [] pmpi_status;
  return ret;
}

int MPI::Request::Waitsome(int count, MPI::Request array[],
			   int array_of_indices[]) 
{
  int ret, i;
  PMPI::Request* pmpi_array = new PMPI::Request[count];
  for (i=0; i < count; i++)
    pmpi_array[i] = array[i].pmpi_request;
  ret = PMPI::Request::Waitsome(count, pmpi_array, array_of_indices);
  for (i=0; i < count; i++)
    array[i].pmpi_request = pmpi_array[i] ;
  delete [] pmpi_array;
  return ret;
}

int MPI::Request::Testsome(int count, MPI::Request array[],
			   int array_of_indices[], MPI::Status stat_array[]) 
{
  int ret;
  int i;
  PMPI::Request* pmpi_array = new PMPI::Request[count];
  for (i=0; i < count; i++)
    pmpi_array[i] = array[i].pmpi_request;
  PMPI::Status* pmpi_status = new PMPI::Status[count];
  for (i=0; i < count; i++)
    pmpi_status[i] = stat_array[i].pmpi_status;
  ret = PMPI::Request::Testsome(count, pmpi_array, array_of_indices, pmpi_status);
  for (i=0; i < count; i++)
    array[i].pmpi_request = pmpi_array[i] ;
  for (i=0; i < count; i++)
    stat_array[i] = pmpi_status[i];
  delete [] pmpi_array;
  delete [] pmpi_status;
  return ret;
}

int MPI::Request::Testsome(int count, MPI::Request array[],
			   int array_of_indices[]) 
{
  int ret, i;
  PMPI::Request* pmpi_array = new PMPI::Request[count];
  for (i=0; i < count; i++)
    pmpi_array[i] = array[i].pmpi_request;
  ret = PMPI::Request::Testsome(count, pmpi_array, array_of_indices);
  for (i=0; i < count; i++)
    array[i].pmpi_request = pmpi_array[i] ;
  delete [] pmpi_array;
  return ret;
}

void MPI::Request::Cancel(void) const
{
  pmpi_request.Cancel();
}

void MPI::Prequest::Start()
{
  pmpi_request.Start();
}

void MPI::Prequest::Startall(int count, MPI::Prequest array[])
{
  int i;
  PMPI::Prequest* pmpi_array = new PMPI::Prequest[count];
  for (i=0; i < count; i++)
    pmpi_array[i] = array[i].pmpi_request;
  PMPI::Prequest::Startall(count, pmpi_array);
  for (i=0; i < count; i++)
    array[i].pmpi_request = pmpi_array[i] ;
  delete [] pmpi_array;
}

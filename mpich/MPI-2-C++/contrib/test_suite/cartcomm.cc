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

#define MAXDIMS 10

void
cartcomm()
{
  MPI2CPP_BOOL_T periods[MAXDIMS];
  MPI2CPP_BOOL_T remain[MAXDIMS];
  char msg[150];
  int ccoords[MAXDIMS];
  int cdest;
  int cdims[MAXDIMS];
  int cndims;
  int coords[MAXDIMS];
  int cperiods[MAXDIMS];
  int crank;
  int csrc;
  int dest;
  int dims[MAXDIMS];
  int dims_save0;
  int dims_save1;
  int i;
  int ndims;
  int rank;
  int size;
  int src;
  int type;
  MPI::Cartcomm comm;
  MPI::Cartcomm dupcomm;
  MPI::Cartcomm mapcomm;
  MPI::Cartcomm shiftcomm;
  MPI::Cartcomm subcomm;
  MPI_Comm ccomm;
  MPI_Comm cshiftcomm;

  comm = MPI::COMM_NULL;
  dupcomm = MPI::COMM_NULL;
  mapcomm = MPI::COMM_NULL;
  shiftcomm = MPI::COMM_NULL;
  subcomm = MPI::COMM_NULL;
  ccomm = MPI_COMM_NULL;
  cshiftcomm = MPI_COMM_NULL;

  Testing("Non-Periodic Topology");

  for(i = 0; i < MAXDIMS; i++) {
    cdims[i] = 0;
    dims[i] = 0;
  }

  MPI::Compute_dims(comm_size, 2, dims);
  MPI_Dims_create(comm_size, 2, cdims);

  dims_save0 = dims[0];
  dims_save1 = dims[1];

  Testing("Create_cart");

  for(i = 0; i < MAXDIMS; i++) {
    cperiods[i] = 0;
    periods[i] = MPI2CPP_FALSE;
  }

  comm = MPI::COMM_WORLD.Create_cart(2, dims, periods, MPI2CPP_FALSE);
  if(comm == MPI::COMM_NULL) {
    sprintf(msg, "NODE %d - 1) Create_cart failed, comm == MPI::COMM_NULL.", my_rank);
    Fail(msg);
  }

  MPI_Cart_create(MPI_COMM_WORLD, 2, cdims, cperiods, 0, &ccomm);
  if(ccomm == MPI_COMM_NULL) {
    sprintf(msg, "NODE %d - 2) The C version used for comparison was not created properly.", my_rank);
    Fail(msg);
  }
  
  type = -1;

  type = comm.Get_topology();

  if(type != MPI::CART) {
    sprintf(msg, "NODE %d - 3) ERROR in comm.Get_topology, type = %d, should be %d", my_rank, type, MPI::CART);
    Fail(msg);
  }

  Pass(); // Create_cart

  mapcomm = MPI::COMM_WORLD.Create_cart(2, dims, periods, MPI2CPP_FALSE);
  shiftcomm = MPI::COMM_WORLD.Create_cart(2, dims, periods, MPI2CPP_FALSE);
  MPI_Cart_create(MPI_COMM_WORLD, 2, cdims, cperiods, 0, &cshiftcomm);

  Testing("Get_dim");
  
  ndims = -1;
  cndims = -1;
   
  ndims = comm.Get_dim();
  MPI_Cartdim_get(ccomm, &cndims);
  if(ndims != cndims) {
    sprintf(msg, "NODE %d - 4) ERROR in comm.Get_dim, ndims = %d, should be %d", my_rank, ndims, cndims);
    Fail(msg);
  }
 
  Pass(); // Get_dim

  Testing("Get_topo");

  if (flags[SKIP_IBM21014])
    Done("Skipped (IBM 2.1.0.14)");
  else if (flags[SKIP_IBM21015])
    Done("Skipped (IBM 2.1.0.15)");
  else if (flags[SKIP_IBM21016])
    Done("Skipped (IBM 2.1.0.16)");
  else if (flags[SKIP_IBM21017])
    Done("Skipped (IBM 2.1.0.17)");
  else {
    for(i = 0; i < MAXDIMS; i++) {
      dims[i] = -1;
      coords[i] = -1;
      periods[i] = MPI2CPP_FALSE;
      cdims[i] = -1;
      ccoords[i] = -1;
      cperiods[i] = -1;
    }
    
    comm.Get_topo(ndims, dims, periods, coords);
    MPI_Cart_get(ccomm, cndims, cdims, cperiods, ccoords);
    for(i = 0; i < ndims; i++) {
      if(dims[i] != cdims[i]) {
	sprintf(msg, "NODE %d - 5) ERROR in comm.Get_topo, dims[%d] = %d, should be %d", my_rank, i, dims[i], cdims[i]);
	Fail(msg);
      }
      if(periods[i] != cperiods[i]) {
	sprintf(msg, "NODE %d - 6) ERROR in comm.Get_topo, periods[%d] = %d, should be %d", my_rank, i, periods[i], cperiods[i]);  
	Fail(msg);
      }
      if(coords[i] != ccoords[i]) {
	sprintf(msg, "NODE %d - 7) ERROR in comm.Get_topo, coords[%d] = %d, should be %d", my_rank, i, coords[i], ccoords[i]);  
	Fail(msg);
      }
    }
    Pass(); // Get_topo
  }

  Testing("Get_cart_rank");
  
  rank = -1;

  rank = comm.Get_cart_rank(coords);
  if(rank != my_rank) {
    sprintf(msg, "NODE %d - 8) ERROR in comm.Get_cart_rank, rank = %d, should be %d",
	    my_rank, rank, my_rank);
    Fail(msg);
  }

  Pass(); // Get_cart_rank

  Testing("Get_coords");

  for(i = 0; i < MAXDIMS; i++) {
    coords[i] = -1;
    ccoords[i] = -1;
  }

  comm.Get_coords(rank, ndims, coords);
  MPI_Cart_coords(ccomm, rank, cndims, ccoords);
  for(i = 0; i < ndims; i++)
    if(coords[i] != ccoords[i]) { 
      sprintf(msg, "NODE %d - 9) ERROR in comm.Get_coords, coords[%d] = %d, should be %d", my_rank, i, coords[i], ccoords[i]);
      Fail(msg);
    }

  Pass(); // Get_coords

  Testing("Shift");

  dest = -1;
  src = -1;
  cdest = -1;
  csrc = -1;

  shiftcomm.Shift(0, 5, src, dest);
  if(src != MPI::PROC_NULL || dest != MPI::PROC_NULL) {
    sprintf(msg, "NODE %d - 10) ERROR in shiftcomm.Shift, src/dest = %d %d, should be %d %d", my_rank, src, dest, csrc, cdest);
    Fail(msg);
  }

  dest = -1;
  src = -1;
  cdest = -1;
  csrc = -1;

  shiftcomm.Shift(0, 1, src, dest);
  MPI_Cart_shift(cshiftcomm, 0, 1, &csrc, &cdest);

  if(my_rank / 2 < 2 && dest != cdest) {
    sprintf(msg, "NODE %d - 11) ERROR in shiftcomm.Shift, dest = %d, should be %d", my_rank, dest, cdest);
    Fail(msg);
  }
  
  if(my_rank / 2 > 0 && src != csrc) {
    sprintf(msg, "NODE %d - 12) ERROR in shiftcomm.Shift, src = %d, should be %d", my_rank, src, csrc);
    Fail(msg);
  }
 
  dest = -1;
  src = -1;
  cdest = -1;
  csrc = -1;

  shiftcomm.Shift(1, -1, src, dest);
  MPI_Cart_shift(cshiftcomm, 1, -1, &csrc, &cdest);

  if(my_rank % 2 && dest != cdest) {
    sprintf(msg, "NODE %d - 13) ERROR in shiftcomm.Shift, dest = %d, should be %d", my_rank, dest, cdest);
    Fail(msg);
  } 
  if(my_rank % 2 && src != MPI::PROC_NULL) {
    sprintf(msg, "NODE %d - 14) ERROR in shiftcomm.Shift, src = %d, should be %d", my_rank, src, csrc);
    Fail(msg);
  }
  if((my_rank % 2) == 0 && src != csrc) {
    sprintf(msg, "NODE %d - 15) ERROR in shiftcomm.Shift, src = %d, should be %d", my_rank, src, csrc);
    Fail(msg);
  }
  if((my_rank % 2) == 0 && dest != MPI::PROC_NULL) {
    sprintf(msg, "NODE %d - 16) ERROR in shiftcomm.Shift, dest = %d, should be %d", my_rank, dest, cdest);
    Fail(msg);
  }
 
  Pass(); // Shift

  Testing("Sub");

  remain[0] = MPI2CPP_FALSE;
  remain[1] = MPI2CPP_TRUE;
  for(i = 2; i < MAXDIMS; i++)
    remain[i] = MPI2CPP_FALSE;

  subcomm = comm.Sub(remain);
  size = subcomm.Get_size();
  if(size != dims_save1) {
    sprintf(msg, "NODE %d - 17) ERROR in subcomm.Sub, size = %d, should be %d", my_rank, size, dims_save1);
    Fail(msg);
  }
  rank = subcomm.Get_rank();
  if(rank != my_rank % dims_save1) {
    sprintf(msg, "NODE %d - 18) ERROR in subcomm.Sub, rank = %d, should be %d", my_rank, rank, my_rank % dims_save1);
    Fail(msg);
  }
  if(subcomm != MPI::COMM_NULL)
    subcomm.Free();

  remain[0] = MPI2CPP_TRUE;
  remain[1] = MPI2CPP_FALSE;

  subcomm = comm.Sub(remain);
  size = subcomm.Get_size();
  if(size != dims_save0) {
    sprintf(msg, "NODE %d - 23) ERROR in subcomm.Sub, size = %d, should be %d", my_rank, size, dims_save0);
    Fail(msg);
  }
  
  rank = subcomm.Get_rank();
  if(rank != my_rank / dims_save1) {
    sprintf(msg, "NODE %d - 24) ERROR in subcomm.Sub, rank = %d, should be %d", my_rank, rank, my_rank / dims_save1);
    Fail(msg);
  }

  Pass(); // Sub

  Testing("Map");

  for(i = 0; i < MAXDIMS; i++) {
    dims[i] = -1;
    coords[i] = -1;
    periods[i] = MPI2CPP_FALSE;
    cdims[i] = -1;
    ccoords[i] = -1;
    cperiods[i] = -1;
  }

  if (flags[SKIP_IBM21014])
    Done("Skipped (IBM 2.1.0.14)");
  else if (flags[SKIP_IBM21015])
    Done("Skipped (IBM 2.1.0.15)");
  else if (flags[SKIP_IBM21016])
    Done("Skipped (IBM 2.1.0.16)");
  else if (flags[SKIP_IBM21017])
    Done("Skipped (IBM 2.1.0.17)");
  else {
    mapcomm.Get_topo(ndims, dims, periods, coords);
    MPI_Cart_get(ccomm, cndims, cdims, cperiods, ccoords);
  }

  rank = mapcomm.Map(ndims, dims, periods);
  MPI_Cart_map(ccomm, cndims, cdims, cperiods, &crank);
  if(rank != crank) {
    sprintf(msg, "NODE %d - 55) ERROR in mapcomm.Map, rank = %d, should be %d", my_rank, rank, crank);
    Fail(msg);
  }

  Pass(); // Map


  Testing("Dup");

  dupcomm = comm.Dup();

  for(i = 0; i < MAXDIMS; i++) {
    dims[i] = -1;
    coords[i] = -1;
    periods[i] = MPI2CPP_FALSE;
    cdims[i] = -1;
    ccoords[i] = -1;
    cperiods[i] = -1;
  }

  if (flags[SKIP_IBM21014])
    Done("Skipped (IBM 2.1.0.14)");
  else if (flags[SKIP_IBM21015])
    Done("Skipped (IBM 2.1.0.15)");
  else if (flags[SKIP_IBM21016])
    Done("Skipped (IBM 2.1.0.16)");
  else if (flags[SKIP_IBM21017])
    Done("Skipped (IBM 2.1.0.17)");
  else {
    dupcomm.Get_topo(ndims, dims, periods, coords);
    MPI_Cart_get(ccomm, cndims, cdims, cperiods, ccoords);
    for(i = 0; i < ndims; i++) {
      if(dims[i] != cdims[i]) {
	sprintf(msg, "NODE %d - 25) ERROR in dupcomm.Get_topo, dims[%d] = %d, should be %d", my_rank, i, dims[i], cdims[i]);
	Fail(msg);
      }
      if(periods[i] != cperiods[i]) {
	sprintf(msg, "NODE %d - 26) ERROR in dupcomm.Get_topo, periods[%d] = %d, should be %d", my_rank, i, periods[i], cperiods[i]);  
	Fail(msg);
      }
      if(coords[i] != ccoords[i]) {
	sprintf(msg, "NODE %d - 27) ERROR in dupcomm.Get_topo, coords[%d] = %d, should be %d", my_rank, i, coords[i], ccoords[i]);  
	Fail(msg);
      }
    }
  }

  Pass(); // Dup

  Testing("Clone");

  MPI::Cartcomm& clonecomm = (MPI::Cartcomm&) comm.Clone();

  for(i = 0; i < MAXDIMS; i++) {
    dims[i] = -1;
    coords[i] = -1;
    periods[i] = MPI2CPP_FALSE;
    cdims[i] = -1;
    ccoords[i] = -1;
    cperiods[i] = -1;
  }

  if (flags[SKIP_IBM21014])
    Done("Skipped (IBM 2.1.0.14)");
  else if (flags[SKIP_IBM21015])
    Done("Skipped (IBM 2.1.0.15)");
  else if (flags[SKIP_IBM21016])
    Done("Skipped (IBM 2.1.0.16)");
  else if (flags[SKIP_IBM21017])
    Done("Skipped (IBM 2.1.0.17)");
  else {
    clonecomm.Get_topo(ndims, dims, periods, coords);
    MPI_Cart_get(ccomm, cndims, cdims, cperiods, ccoords);
    for(i = 0; i < ndims; i++) {
      if(dims[i] != cdims[i]) {
	sprintf(msg, "NODE %d - 28) ERROR in clonecomm.Get_topo, dims[%d] = %d, should be %d", my_rank, i, dims[i], cdims[i]);
	Fail(msg);
      }
      if(periods[i] != cperiods[i]) {
	sprintf(msg, "NODE %d - 29) ERROR in clonecomm.Get_topo, periods[%d] = %d, should be %d", my_rank, i, periods[i], cperiods[i]);  
	Fail(msg);
      }
      if(coords[i] != ccoords[i]) {
	sprintf(msg, "NODE %d - 30) ERROR in clonecomm.Get_topo, coords[%d] = %d, should be %d", my_rank, i, coords[i], ccoords[i]);  
	Fail(msg);
      }
    }
    if (clonecomm != MPI::COMM_NULL && clonecomm != MPI::COMM_WORLD) {
      clonecomm.Free();
      delete &clonecomm;
    }
  }

  Pass(); // Clone

  Pass(); // Non-Periodic Topology

  if(comm != MPI::COMM_NULL && comm != MPI::COMM_WORLD)
    comm.Free();
  if(dupcomm != MPI::COMM_NULL && dupcomm != MPI::COMM_WORLD)
    dupcomm.Free();
  if(mapcomm != MPI::COMM_NULL && mapcomm != MPI::COMM_WORLD)
    mapcomm.Free();
  if(shiftcomm != MPI::COMM_NULL && shiftcomm != MPI::COMM_WORLD)
    shiftcomm.Free();
  if(subcomm != MPI::COMM_NULL && subcomm != MPI::COMM_WORLD)
    subcomm.Free();
  if(ccomm != MPI_COMM_NULL && ccomm != MPI_COMM_WORLD)
    MPI_Comm_free(&ccomm);
  if(cshiftcomm != MPI_COMM_NULL && cshiftcomm != MPI_COMM_WORLD)
    MPI_Comm_free(&cshiftcomm);

  Testing("Periodic Topology");

  for(i = 0; i < MAXDIMS; i++) {
    cdims[i] = 0;
    dims[i] = 0;
  }

  cdims[0] = 2;
  dims[0] = 2;

  MPI::Compute_dims(comm_size, 2, dims);
  MPI_Dims_create(comm_size, 2, cdims);

  dims_save0 = dims[0];
  dims_save1 = dims[1];

  Testing("Create_cart");

  for(i = 0; i < MAXDIMS; i++) {
    cperiods[i] = 0;
    periods[i] = MPI2CPP_FALSE;
  }
  
  comm = MPI::COMM_WORLD.Create_cart(2, dims, periods, MPI2CPP_FALSE);
  if(comm == MPI::COMM_NULL) {
    sprintf(msg, "NODE %d - 31) Create_cart failed, comm == MPI::COMM_NULL.", my_rank);
    Fail(msg);
  }
  
  MPI_Cart_create(MPI_COMM_WORLD, 2, cdims, cperiods, 0, &ccomm);
  if(ccomm == MPI_COMM_NULL) {
    sprintf(msg, "NODE %d - 32) The C version used for comparison was not created properly.", my_rank);
    Fail(msg);
  }
  
  type = -1;

  type = comm.Get_topology();

  if(type != MPI::CART) {
    sprintf(msg, "NODE %d - 33) ERROR in comm.Get_topology, type = %d, should be %d", my_rank, type, MPI::CART);
    Fail(msg);
  }

  Pass(); // Create_cart
  
  mapcomm = MPI::COMM_WORLD.Create_cart(2, dims, periods, MPI2CPP_FALSE);
  shiftcomm = MPI::COMM_WORLD.Create_cart(2, dims, periods, MPI2CPP_FALSE);
  MPI_Cart_create(MPI_COMM_WORLD, 2, cdims, cperiods, 0, &cshiftcomm);

  Testing("Get_dim");
  
  ndims = -1;
  cndims = -1;
   
  ndims = comm.Get_dim();
  MPI_Cartdim_get(ccomm, &cndims);
  if(ndims != cndims) {
    sprintf(msg, "NODE %d - 34) ERROR in comm.Get_dim, ndims = %d, should be %d", my_rank, ndims, cndims);
    Fail(msg);
  }
 
  Pass(); // Get_dim

  Testing("Get_topo");

  if (flags[SKIP_IBM21014])
    Done("Skipped (IBM 2.1.0.14)");
  else if (flags[SKIP_IBM21015])
    Done("Skipped (IBM 2.1.0.15)");
  else if (flags[SKIP_IBM21016])
    Done("Skipped (IBM 2.1.0.16)");
  else if (flags[SKIP_IBM21017])
    Done("Skipped (IBM 2.1.0.17)");
  else {
    for(i = 0; i < MAXDIMS; i++) {
      dims[i] = -1;
      coords[i] = -1;
      periods[i] = MPI2CPP_FALSE;
      cdims[i] = -1;
      ccoords[i] = -1;
      cperiods[i] = -1;
    }
    
    comm.Get_topo(ndims, dims, periods, coords);
    MPI_Cart_get(ccomm, cndims, cdims, cperiods, ccoords);
    for(i = 0; i < ndims; i++) {
      if(dims[i] != cdims[i]) {
	sprintf(msg, "NODE %d - 35) ERROR in comm.Get_topo, dims[%d] = %d, should be %d", my_rank, i, dims[i], cdims[i]);
	Fail(msg);
      }
      if(periods[i] != cperiods[i]) {
	sprintf(msg, "NODE %d - 36) ERROR in comm.Get_topo, periods[%d] = %d, should be %d", my_rank, i, periods[i], cperiods[i]);  
	Fail(msg);
      }
      if(coords[i] != ccoords[i]) {
	sprintf(msg, "NODE %d - 37) ERROR in comm.Get_topo, coords[%d] = %d, should be %d", my_rank, i, coords[i], ccoords[i]);  
	Fail(msg);
      }
    }
    Pass(); // Get_topo
  }

  Testing("Get_cart_rank");
  
  rank = -1;

  rank = comm.Get_cart_rank(coords);
  if(rank != my_rank) {
    sprintf(msg, "NODE %d - 38) ERROR in comm.Get_cart_rank, rank = %d, should be %d",
	    my_rank, rank, my_rank);
    Fail(msg);
  }

  Pass(); // Get_cart_rank

  Testing("Get_coords");

  for(i = 0; i < MAXDIMS; i++) {
    coords[i] = -1;
    ccoords[i] = -1;
  }

  comm.Get_coords(rank, ndims, coords);
  MPI_Cart_coords(ccomm, rank, cndims, ccoords);
  for(i = 0; i < ndims; i++)
    if(coords[i] != ccoords[i]) { 
      sprintf(msg, "NODE %d - 39) ERROR in comm.Get_coords, coords[%d] = %d, should be %d", my_rank, i, coords[i], ccoords[i]);
      Fail(msg);
    }

  Pass(); // Get_coords

  Testing("Shift");

  dest = -1;
  src = -1;
  cdest = -1;
  csrc = -1;

  shiftcomm.Shift(0, 5, src, dest);
  MPI_Cart_shift(cshiftcomm, 0, 5, &csrc, &cdest);
  if(src != csrc || dest != cdest) {
    sprintf(msg, "NODE %d - 40) ERROR in shiftcomm.Shift, src/dest = %d %d, should be %d %d", my_rank, src, dest, csrc, cdest);
    Fail(msg);
  }

  dest = -1;
  src = -1;
  cdest = -1;
  csrc = -1;

  shiftcomm.Shift(0, 1, src, dest);
  MPI_Cart_shift(cshiftcomm, 0, 1, &csrc, &cdest);

  if(my_rank / 2 < 2 && dest != cdest) {
    sprintf(msg, "NODE %d - 41) ERROR in shiftcomm.Shift, dest = %d, should be %d", my_rank, dest, cdest);
    Fail(msg);
  }
  
  if(my_rank / 2 > 0 && src != csrc) {
    sprintf(msg, "NODE %d - 42) ERROR in shiftcomm.Shift, src = %d, should be %d", my_rank, src, csrc);
    Fail(msg);
  }
 
  dest = -1;
  src = -1;
  cdest = -1;
  csrc = -1;

  shiftcomm.Shift(1, -1, src, dest);
  MPI_Cart_shift(cshiftcomm, 1, -1, &csrc, &cdest);

  if(dest != cdest) {
    sprintf(msg, "NODE %d - 43) ERROR in shiftcomm.Shift, dest = %d, should be %d", my_rank, dest, cdest);
    Fail(msg);
  } 
  if(src != csrc) {
    sprintf(msg, "NODE %d - 44) ERROR in shiftcomm.Shift, src = %d, should be %d", my_rank, src, csrc);
    Fail(msg);
  }

  Pass(); // Shift

  Testing("Sub");

  remain[0] = MPI2CPP_FALSE;
  remain[1] = MPI2CPP_TRUE;
  for(i = 2; i < MAXDIMS; i++)
    remain[i] = MPI2CPP_FALSE;

  subcomm = comm.Sub(remain);
  size = subcomm.Get_size();
  if(size != dims_save1) {
    sprintf(msg, "NODE %d - 47) ERROR in subcomm.Sub, size = %d, should be %d", my_rank, size, dims_save1);
    Fail(msg);
  }
  rank = subcomm.Get_rank();
  if(rank != my_rank % dims_save1) {
    sprintf(msg, "NODE %d - 48) ERROR in subcomm.Sub, rank = %d, should be %d", my_rank, rank, my_rank % dims_save1);
    Fail(msg);
  }
  if(subcomm != MPI::COMM_NULL)
    subcomm.Free();

  remain[0] = MPI2CPP_TRUE;
  remain[1] = MPI2CPP_FALSE;

  subcomm = comm.Sub(remain);
  size = subcomm.Get_size();
  if(size != dims_save0) {
    sprintf(msg, "NODE %d - 53) ERROR in subcomm.Sub, size = %d, should be %d", my_rank, size, dims_save0);
    Fail(msg);
  }
  
  rank = subcomm.Get_rank();
  if(rank != my_rank / dims_save1) {
    sprintf(msg, "NODE %d - 54) ERROR in subcomm.Sub, rank = %d, should be %d", my_rank, rank, my_rank / dims_save1);
    Fail(msg);
  }

  Pass(); // Sub

  Testing("Map");

  for(i = 0; i < MAXDIMS; i++) {
    dims[i] = -1;
    coords[i] = -1;
    periods[i] = MPI2CPP_FALSE;
    cdims[i] = -1;
    ccoords[i] = -1;
    cperiods[i] = -1;
  }

  if (flags[SKIP_IBM21014])
    Done("Skipped (IBM 2.1.0.14)");
  else if (flags[SKIP_IBM21015])
    Done("Skipped (IBM 2.1.0.15)");
  else if (flags[SKIP_IBM21016])
    Done("Skipped (IBM 2.1.0.16)");
  else if (flags[SKIP_IBM21017])
    Done("Skipped (IBM 2.1.0.17)");
  else {
    mapcomm.Get_topo(ndims, dims, periods, coords);
    MPI_Cart_get(ccomm, cndims, cdims, cperiods, ccoords);
  }

  rank = mapcomm.Map(ndims, dims, periods);
  MPI_Cart_map(ccomm, cndims, cdims, cperiods, &crank);
  if(rank != crank) {
    sprintf(msg, "NODE %d - 55) ERROR in mapcomm.Map, rank = %d, should be %d", my_rank, rank, crank);
    Fail(msg);
  }

  Pass(); // Map

  Testing("Dup");

  dupcomm = comm.Dup();

  for(i = 0; i < MAXDIMS; i++) {
    dims[i] = -1;
    coords[i] = -1;
    periods[i] = MPI2CPP_FALSE;
    cdims[i] = -1;
    ccoords[i] = -1;
    cperiods[i] = -1;
  }
  
  if (flags[SKIP_IBM21014])
    Done("Skipped (IBM 2.1.0.14)");
  else if (flags[SKIP_IBM21015])
    Done("Skipped (IBM 2.1.0.15)");
  else if (flags[SKIP_IBM21016])
    Done("Skipped (IBM 2.1.0.16)");
  else if (flags[SKIP_IBM21017])
    Done("Skipped (IBM 2.1.0.17)");
  else {
    dupcomm.Get_topo(ndims, dims, periods, coords);
    MPI_Cart_get(ccomm, cndims, cdims, cperiods, ccoords);
    for(i = 0; i < ndims; i++) {
      if(dims[i] != cdims[i]) {
	sprintf(msg, "NODE %d - 55) ERROR in dupcomm.Get_topo, dims[%d] = %d, should be %d", my_rank, i, dims[i], cdims[i]);
	Fail(msg);
      }
      if(periods[i] != cperiods[i]) {
	sprintf(msg, "NODE %d - 56) ERROR in dupcomm.Get_topo, periods[%d] = %d, should be %d", my_rank, i, periods[i], cperiods[i]);  
	Fail(msg);
      }
      if(coords[i] != ccoords[i]) {
	sprintf(msg, "NODE %d - 57) ERROR in dupcomm.Get_topo, coords[%d] = %d, should be %d", my_rank, i, coords[i], ccoords[i]);  
	Fail(msg);
      }
    }
  }

  Pass(); // Dup

  Testing("Clone");

  MPI::Cartcomm& clonecomm2 = (MPI::Cartcomm&) comm.Clone();

  for(i = 0; i < MAXDIMS; i++) {
    dims[i] = -1;
    coords[i] = -1;
    periods[i] = MPI2CPP_FALSE;
    cdims[i] = -1;
    ccoords[i] = -1;
    cperiods[i] = -1;
  }

  if (flags[SKIP_IBM21014])
    Done("Skipped (IBM 2.1.0.14)");
  else if (flags[SKIP_IBM21015])
    Done("Skipped (IBM 2.1.0.15)");
  else if (flags[SKIP_IBM21016])
    Done("Skipped (IBM 2.1.0.16)");
  else if (flags[SKIP_IBM21017])
    Done("Skipped (IBM 2.1.0.17)");
  else {
    clonecomm2.Get_topo(ndims, dims, periods, coords);
    MPI_Cart_get(ccomm, cndims, cdims, cperiods, ccoords);
    for(i = 0; i < ndims; i++) {
      if(dims[i] != cdims[i]) {
	sprintf(msg, "NODE %d - 58) ERROR in clonecomm.Get_topo, dims[%d] = %d, should be %d", my_rank, i, dims[i], cdims[i]);
	Fail(msg);
      }
      if(periods[i] != cperiods[i]) {
	sprintf(msg, "NODE %d - 59) ERROR in clonecomm.Get_topo, periods[%d] = %d, should be %d", my_rank, i, periods[i], cperiods[i]);  
	Fail(msg);
      }
      if(coords[i] != ccoords[i]) {
	sprintf(msg, "NODE %d - 60) ERROR in clonecomm.Get_topo, coords[%d] = %d, should be %d", my_rank, i, coords[i], ccoords[i]);  
	Fail(msg);
      }
    }
    if (clonecomm2 != MPI::COMM_NULL && clonecomm2 != MPI::COMM_WORLD) {
      clonecomm2.Free();
      delete &clonecomm2;
    }
  }

  Pass(); // Clone

  Pass(); // Periodic Topology

  if(comm != MPI::COMM_NULL && comm != MPI::COMM_WORLD)
    comm.Free();
  if(dupcomm != MPI::COMM_NULL && dupcomm != MPI::COMM_WORLD)
    dupcomm.Free();
  if(mapcomm != MPI::COMM_NULL && mapcomm != MPI::COMM_WORLD)
    mapcomm.Free();
  if(shiftcomm != MPI::COMM_NULL && shiftcomm != MPI::COMM_WORLD)
    shiftcomm.Free();
  if(subcomm != MPI::COMM_NULL && subcomm != MPI::COMM_WORLD)
    subcomm.Free();
  if(ccomm != MPI_COMM_NULL && ccomm != MPI_COMM_WORLD)
    MPI_Comm_free(&ccomm);
  if(cshiftcomm != MPI_COMM_NULL && cshiftcomm != MPI_COMM_WORLD)
    MPI_Comm_free(&cshiftcomm);
}

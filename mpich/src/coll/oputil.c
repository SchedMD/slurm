/*
 *  $Id: oputil.c,v 1.4 1999/08/20 02:26:02 ashton Exp $
 *
 *  (C) 1996 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"
#include "mpimem.h"
#include "mpiops.h"

/* 
 * This file contains the routines to setup the MPI ops
 */

/* 
   This function is used to initialize the MPI_Op's; it can be used
   for the predefined operations as well 
 */
int MPIR_Op_setup( 
	MPI_User_function *function, 
	int commute, 
	int is_perm, 
	MPI_Op newop )
{

    struct MPIR_OP *new;
    MPIR_ALLOC(new,NEW( struct MPIR_OP ),MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, 
	       "MPI_OP_CREATE");
    MPIR_SET_COOKIE(new,MPIR_OP_COOKIE)
    new->commute   = commute;
    new->op	   = function;
    new->permanent = is_perm;
    MPIR_RegPointerIdx( newop, new );
    return MPI_SUCCESS;
}


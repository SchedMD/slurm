/*
 *  $Id: ic_create.c,v 1.14 2001/11/14 19:54:27 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Intercomm_create = PMPI_Intercomm_create
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Intercomm_create  MPI_Intercomm_create
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Intercomm_create as PMPI_Intercomm_create
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif
#include "mpimem.h"

/*@

MPI_Intercomm_create - Creates an intercommuncator from two intracommunicators

Input Paramters:
+ local_comm - Local (intra)communicator
. local_leader - Rank in local_comm of leader (often 0)
. peer_comm - Remote communicator
. remote_leader - Rank in peer_comm of remote leader (often 0)
- tag - Message tag to use in constructing intercommunicator; if multiple
  'MPI_Intercomm_creates' are being made, they should use different tags (more
  precisely, ensure that the local and remote leaders are using different
  tags for each 'MPI_intercomm_create').

Output Parameter:
. comm_out - Created intercommunicator

Notes:
  The MPI 1.1 Standard contains two mutually exclusive comments on the
  input intracommunicators.  One says that their repective groups must be
  disjoint; the other that the leaders can be the same process.  After
  some discussion by the MPI Forum, it has been decided that the groups must
  be disjoint.  Note that the `reason` given for this in the standard is
  `not` the reason for this choice; rather, the `other` operations on 
  intercommunicators (like 'MPI_Intercomm_merge') do not make sense if the
  groups are not disjoint.

.N fortran

Algorithm:
+ 1) Allocate a send context, an inter-coll context, and an intra-coll context
. 2) Send "send_context" and lrank_to_grank list from local comm group 
     if I''m the local_leader.
. 3) If I''m the local leader, then wait on the posted sends and receives
     to complete.  Post the receive for the remote group information and
	 wait for it to complete.
. 4) Broadcast information received from the remote leader.  
. 5) Create the inter_communicator from the information we now have.
-    An inter-communicator ends up with three levels of communicators. 
     The inter-communicator returned to the user, a "collective" 
     inter-communicator that can be used for safe communications between
     local & remote groups, and a collective intra-communicator that can 
     be used to allocate new contexts during the merge and dup operations.

	 For the resulting inter-communicator, 'comm_out'

.vb
       comm_out                       = inter-communicator
       comm_out->comm_coll            = "collective" inter-communicator
       comm_out->comm_coll->comm_coll = safe collective intra-communicator
.ve

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_TAG
.N MPI_ERR_EXHAUSTED
.N MPI_ERR_RANK

.seealso: MPI_Intercomm_merge, MPI_Comm_free, MPI_Comm_remote_group, 
          MPI_Comm_remote_size
@*/
int MPI_Intercomm_create ( MPI_Comm local_comm, int local_leader, 
			   MPI_Comm peer_comm, int remote_leader, int tag, 
			   MPI_Comm *comm_out )
{
  int              local_size, local_rank, peer_size, peer_rank;
  int              remote_size;
  int              mpi_errno = MPI_SUCCESS;
  MPIR_CONTEXT     context, send_context;
  struct MPIR_GROUP *remote_group_ptr;
  struct MPIR_COMMUNICATOR *new_comm, *local_comm_ptr, *peer_comm_ptr=0;
  MPI_Request      req[6];
  MPI_Status       status[6];
  MPIR_ERROR_DECL;
  static char myname[]="MPI_INTERCOMM_CREATE";

  TR_PUSH(myname);
  local_comm_ptr = MPIR_GET_COMM_PTR(local_comm);

  
#ifndef MPIR_NO_ERROR_CHECKING
  /* Check for valid arguments to function */
  MPIR_TEST_MPI_COMM(local_comm,local_comm_ptr,local_comm_ptr,myname);
  MPIR_TEST_SEND_TAG(tag);
  if (mpi_errno)
      return MPIR_ERROR(local_comm_ptr, mpi_errno, myname );
#endif

  if (local_comm  == MPI_COMM_NULL) {
      mpi_errno = MPIR_Err_setmsg( MPI_ERR_COMM, MPIR_ERR_LOCAL_COMM, myname, 
		   "Local communicator must not be MPI_COMM_NULL", (char *)0 );
      return MPIR_ERROR( local_comm_ptr, mpi_errno, myname );
  }

  (void) MPIR_Comm_size ( local_comm_ptr, &local_size );
  (void) MPIR_Comm_rank ( local_comm_ptr, &local_rank );

  if ( local_leader == local_rank ) {
      /* Peer_comm need be valid only at local_leader */
      peer_comm_ptr = MPIR_GET_COMM_PTR(peer_comm);
      if ((MPIR_TEST_COMM_NOTOK(peer_comm,peer_comm_ptr) || 
	   (peer_comm == MPI_COMM_NULL))) {
	  mpi_errno = MPIR_Err_setmsg( MPI_ERR_COMM, MPIR_ERR_PEER_COMM,
			       myname, "Peer communicator is not valid", 
				       (char *)0 );
	  return MPIR_ERROR( local_comm_ptr, mpi_errno, myname );
      }

    (void) MPIR_Comm_size ( peer_comm_ptr,  &peer_size  );
    (void) MPIR_Comm_rank ( peer_comm_ptr,  &peer_rank  );

    if (((peer_rank     == MPI_UNDEFINED) && (mpi_errno = MPI_ERR_RANK)))
	return MPIR_ERROR( local_comm_ptr, mpi_errno, myname );

    if (((remote_leader >= peer_size)     && (mpi_errno = MPI_ERR_RANK)) || 
        ((remote_leader <  0)             && (mpi_errno = MPI_ERR_RANK))) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_RANK, MPIR_ERR_REMOTE_RANK, 
				     myname, 
				     "Error specifying remote_leader", 
"Error specifying remote_leader; value %d not between 0 and %d", remote_leader, peer_size );
       return MPIR_ERROR( local_comm_ptr, mpi_errno, myname );
    }
  }

  if (((local_leader  >= local_size)    && (mpi_errno = MPI_ERR_RANK)) || 
      ((local_leader  <  0)             && (mpi_errno = MPI_ERR_RANK))) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_RANK, MPIR_ERR_LOCAL_RANK, 
				     myname, 
				     "Error specifying local_leader", 
"Error specifying local_leader; value %d not in between 0 and %d", local_leader, local_size );
       return MPIR_ERROR( local_comm_ptr, mpi_errno, myname );
    }

  /* Allocate send context, inter-coll context and intra-coll context */
  MPIR_Context_alloc ( local_comm_ptr, 3, &context );

  
  /* If I'm the local leader, then exchange information */
  if (local_rank == local_leader) {
      MPIR_ERROR_PUSH(peer_comm_ptr);

      /* Post the receives for the information from the remote_leader */
      /* We don't post a receive for the remote group yet, because we */
      /* don't know how big it is yet. */
      MPIR_CALL_POP(MPI_Irecv (&remote_size, 1, MPI_INT, remote_leader, tag,
			       peer_comm, &(req[2])),peer_comm_ptr,myname);
      MPIR_CALL_POP(MPI_Irecv (&send_context, 1, MPIR_CONTEXT_TYPE, 
			       remote_leader,tag, peer_comm, &(req[3])),
		    peer_comm_ptr,myname);
    
      /* Send the lrank_to_grank table of the local_comm and an allocated */
      /* context. Currently I use multiple messages to send this info.    */
      /* Eventually, this will change(?) */
      MPIR_CALL_POP(MPI_Isend (&local_size, 1, MPI_INT, remote_leader, tag, 
               peer_comm, &(req[0])),peer_comm_ptr,myname);
      MPIR_CALL_POP(MPI_Isend (&context, 1, MPIR_CONTEXT_TYPE, remote_leader, 
               tag, peer_comm, &(req[1])),peer_comm_ptr,myname);
    
      /* Wait on the communication requests to finish */
      MPIR_CALL_POP(MPI_Waitall ( 4, req, status ),peer_comm_ptr,myname);
    
      /* We now know how big the remote group is, so create it */
      remote_group_ptr = MPIR_CreateGroup ( remote_size );
      remote_group_ptr->self = 
	  (MPI_Group) MPIR_FromPointer( remote_group_ptr );

      /* Post the receive for the group information */
      MPIR_CALL_POP(MPI_Irecv (remote_group_ptr->lrank_to_grank, remote_size, 
			       MPI_INT, remote_leader, tag, peer_comm, 
			       &(req[5])),peer_comm_ptr,myname);
    
      /* Send the local group info to the remote group */
      MPIR_CALL_POP(MPI_Isend (local_comm_ptr->group->lrank_to_grank, local_size, 
			       MPI_INT, remote_leader, tag, peer_comm, 
			       &(req[4])),peer_comm_ptr,myname);
    
      /* wait on the send and the receive for the group information */
      MPIR_CALL_POP(MPI_Waitall ( 2, &(req[4]), &(status[4]) ),peer_comm_ptr,
		    myname);
      MPIR_ERROR_POP(peer_comm_ptr);

      /* Now we can broadcast the group information to the other local comm */
      /* members. */
      MPIR_ERROR_PUSH(local_comm_ptr);
      MPIR_CALL_POP(MPI_Bcast(&remote_size,1,MPI_INT,local_rank,local_comm),
		    local_comm_ptr,myname);
      MPIR_CALL_POP(MPI_Bcast(remote_group_ptr->lrank_to_grank, remote_size, 
			      MPI_INT, local_rank, local_comm),local_comm_ptr,
		    myname);
      MPIR_ERROR_POP(local_comm_ptr);
  }
  /* Else I'm just an ordinary comm member, so receive the bcast'd */
  /* info about the remote group */
  else {
      MPIR_ERROR_PUSH(local_comm_ptr);
      MPIR_CALL_POP(MPI_Bcast(&remote_size, 1, MPI_INT, local_leader,
			      local_comm),local_comm_ptr,myname);
    
      /* We now know how big the remote group is, so create it */
      remote_group_ptr = MPIR_CreateGroup ( remote_size );
      remote_group_ptr->self = 
	  (MPI_Group) MPIR_FromPointer( remote_group_ptr );
	
      /* Receive the group info */
      MPIR_CALL_POP(MPI_Bcast(remote_group_ptr->lrank_to_grank, remote_size, 
			      MPI_INT, local_leader, local_comm),
		    local_comm_ptr,myname );
      MPIR_ERROR_POP(local_comm_ptr);
  }

  MPIR_ERROR_PUSH(local_comm_ptr);
  /* Broadcast the send context */
  MPIR_CALL_POP(MPI_Bcast(&send_context, 1, MPIR_CONTEXT_TYPE, 
			  local_leader, local_comm),local_comm_ptr,myname);
  MPIR_ERROR_POP(local_comm_ptr);

  /* We all now have all the information necessary, start building the */
  /* inter-communicator */
  MPIR_ALLOC(new_comm,NEW(struct MPIR_COMMUNICATOR),local_comm_ptr, 
	     MPI_ERR_EXHAUSTED,myname );
  MPIR_Comm_init( new_comm, local_comm_ptr, MPIR_INTER );
  *comm_out = new_comm->self;
  new_comm->group = remote_group_ptr;
  MPIR_Group_dup( local_comm_ptr->group, &(new_comm->local_group) );
  new_comm->local_rank	   = new_comm->local_group->local_rank;
  new_comm->lrank_to_grank = new_comm->group->lrank_to_grank;
  new_comm->np             = new_comm->group->np;
  new_comm->send_context   = send_context;
  new_comm->recv_context   = context;
  new_comm->comm_name      = 0;
  (void) MPIR_Attr_create_tree ( new_comm );
  if ((mpi_errno = MPID_CommInit( local_comm_ptr, new_comm )) )
      return mpi_errno;

  /* Build the collective inter-communicator */
  MPIR_Comm_make_coll( new_comm, MPIR_INTER );
  
  /* Build the collective intra-communicator.  Note that we require
     an intra-communicator for the "coll_comm" so that MPI_COMM_DUP
     can use it for some collective operations (do we need this
     for MPI-2 with intercommunicator collective?) 
     
     Note that this really isn't the right thing to do; we need to replace
     *all* of the Mississippi state collective code.
   */
  MPIR_Comm_make_coll( new_comm->comm_coll, MPIR_INTRA );
  
  /* Remember it for the debugger */
  MPIR_Comm_remember ( new_comm );

  TR_POP;
  return (mpi_errno);
}

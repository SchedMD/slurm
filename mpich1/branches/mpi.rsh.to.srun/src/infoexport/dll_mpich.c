/**********************************************************************
 * Copyright (C) 1997-1998 Dolphin Interconnect Solutions Inc.
 * Copyright (C) 1999-2001 Etnus LLC.
 *
 * Permission is hereby granted to use, reproduce, prepare derivative
 * works, and to redistribute to others.
 *
 *				  DISCLAIMER
 *
 * Neither Dolphin Interconnect Solutions, Etnus LLC, nor any of their
 * employees, makes any warranty express or implied, or assumes any
 * legal liability or responsibility for the accuracy, completeness,
 * or usefulness of any information, apparatus, product, or process
 * disclosed, or represents that its use would not infringe privately
 * owned rights.
 *
 * This code was written by
 * James Cownie: Dolphin Interconnect Solutions. <jcownie@dolphinics.com>
 *               Etnus LLC <jcownie@etnus.com>
 **********************************************************************/

/* Update log
 *
 * Mar  6 2001 JHC: Add mqs_comm_get_group to allow a debugger to acquire
 *                  processes less eagerly.
 * Dec 13 2000 JHC: totalview/2514: Modify image_has_queues to return
 *                  a silent FALSE if none of the expected data is
 *                  present. This way you won't get complaints when
 *                  you try this on non MPICH processes.
 * Sep  8 2000 JVD: #include <string.h> to silence Linux Alpha compiler warnings.
 * Mar 21 2000 JHC: Add the new entrypoint mqs_dll_taddr_width
 * Nov 26 1998 JHC: Fix the problem that we weren't handling
 *                  MPIR_Ignore_queues properly.
 * Oct 22 1998 JHC: Fix a zero allocation problem
 * Aug 19 1998 JHC: Fix some problems in our use of target_to_host on
 *                  big endian machines.
 * May 28 1998 JHC: Use the extra information we can return to say
 *                  explicitly that sends are only showing non-blocking ops
 * May 19 1998 JHC: Changed the names of the structs and added casts
 *                  where needed to reflect the change to the way we handle
 *                  type safety across the interface.
 * Oct 27 1997 JHC: Created by exploding db_message_state_mpich.cxx
 */

/*
 * This file is an example of how to use the DLL interface to handle
 * message queue display from a debugger.  It provides all of the
 * functions required to display MPICH message queues.
 * It has been tested with TotalView.
 *
 * James Cownie <jcownie@dolphinics.com>
 */

#include <stdlib.h>	
/* 
   The following was added by William Gropp to improve the portability 
   to systems with non-ANSI C compilers 
 */
#include "mpichconf.h"
#ifdef HAVE_NO_C_CONST
#define const
#endif

#include <string.h>
#include "mpi_interface.h"
#include "mpich_dll_defs.h"	

/* Essential macros for C */
#ifndef NULL
#define NULL ((void *)0)
#endif
#ifndef TRUE
#define TRUE (0==0)
#endif
#ifndef FALSE
#define FALSE (0==1)
#endif

#ifdef OLD_STYLE_CPP_CONCAT
#define concat(a,b) a/**/b
#define stringize(a) "a"
#else
#define concat(a,b) a##b
#define stringize(a) #a
#endif

/**********************************************************************/
/* Set up the basic callbacks into the debugger, also work out 
 * one crucial piece of info about the machine we're running on.
 */
static const mqs_basic_callbacks *mqs_basic_entrypoints;
static int host_is_big_endian;

void mqs_setup_basic_callbacks (const mqs_basic_callbacks * cb)
{
  int t = 1;

  host_is_big_endian    = (*(char *)&t) != 1;
  mqs_basic_entrypoints = cb;
} /* mqs_setup_callbacks */

/**********************************************************************/
/* Macros to make it transparent that we're calling the TV functions
 * through function pointers.
 */
#define mqs_malloc           (mqs_basic_entrypoints->mqs_malloc_fp)
#define mqs_free             (mqs_basic_entrypoints->mqs_free_fp)
#define mqs_prints           (mqs_basic_entrypoints->mqs_eprints_fp)
#define mqs_put_image_info   (mqs_basic_entrypoints->mqs_put_image_info_fp)
#define mqs_get_image_info   (mqs_basic_entrypoints->mqs_get_image_info_fp)
#define mqs_put_process_info (mqs_basic_entrypoints->mqs_put_process_info_fp)
#define mqs_get_process_info (mqs_basic_entrypoints->mqs_get_process_info_fp)

/* These macros *RELY* on the function already having set up the conventional
 * local variables i_info or p_info.
 */
#define mqs_find_type        (i_info->image_callbacks->mqs_find_type_fp)
#define mqs_field_offset     (i_info->image_callbacks->mqs_field_offset_fp)
#define mqs_get_type_sizes   (i_info->image_callbacks->mqs_get_type_sizes_fp)
#define mqs_find_function    (i_info->image_callbacks->mqs_find_function_fp)
#define mqs_find_symbol      (i_info->image_callbacks->mqs_find_symbol_fp)

#define mqs_get_image        (p_info->process_callbacks->mqs_get_image_fp)
#define mqs_get_global_rank  (p_info->process_callbacks->mqs_get_global_rank_fp)
#define mqs_fetch_data       (p_info->process_callbacks->mqs_fetch_data_fp)
#define mqs_target_to_host   (p_info->process_callbacks->mqs_target_to_host_fp)

/**********************************************************************/
/* Version handling functions.
 * This one should never be changed.
 */
int mqs_version_compatibility ( void )
{
  return MQS_INTERFACE_COMPATIBILITY;
} /* mqs_version_compatibility */

/* This one can say what you like */
#ifndef __DATE__
/* Some pre-ANSI C compilers don't grok __DATE__ */
#define __DATE__ "unknown"
#endif
char *mqs_version_string ( void )
{
  return "ETNUS MPICH message queue support for MPICH 1.1, 1.2 compiled on " __DATE__;
} /* mqs_version_string */

/* So the debugger can tell what interface width the library was compiled with */
int mqs_dll_taddr_width (void)
{
  return sizeof (mqs_taddr_t);
} /* mqs_dll_taddr_width */

/**********************************************************************/
/* Additional error codes and error string conversion.
 */
enum {
  err_silent_failure  = mqs_first_user_code,

  err_no_current_communicator,
  err_bad_request,
  err_no_store,

  err_failed_qhdr,
    err_unexpected,
    err_posted,

  err_failed_queue,
    err_first,

  err_failed_qel,
    err_context_id,
    err_tag,
    err_tagmask,
    err_lsrc,
    err_srcmask,
    err_next,
    err_ptr,

  err_failed_squeue,
    err_sq_head,

  err_failed_sqel,
    err_db_shandle,
    err_db_comm,
    err_db_target,
    err_db_tag,
    err_db_data,
    err_db_byte_length,
    err_db_next,

  err_failed_rhandle,
    err_is_complete,
    err_buf,
    err_len,
    err_s,

  err_failed_status,
    err_count,
    err_MPI_SOURCE,
    err_MPI_TAG,

  err_failed_commlist,
    err_sequence_number,
    err_comm_first,

  err_failed_communicator,
    err_np,
    err_lrank_to_grank,
    err_send_context,
    err_recv_context,
    err_comm_next,
    err_comm_name,

  err_all_communicators,
  err_mpid_recvs,
  err_group_corrupt
};

/**********************************************************************/
/* Forward declarations 
 */
static mqs_taddr_t fetch_pointer (mqs_process * proc, mqs_taddr_t addr, mpich_process_info *p_info);
static mqs_tword_t fetch_int (mqs_process * proc, mqs_taddr_t addr, mpich_process_info *p_info);

/* Internal structure we hold for each communicator */
typedef struct communicator_t
{
  struct communicator_t * next;
  group_t *               group;		/* Translations */
  int                     recv_context;		/* To catch changes */
  int                     present;
  mqs_communicator        comm_info;		/* Info needed at the higher level */
} communicator_t;

/**********************************************************************/
/* Functions to handle translation groups.
 * We have a list of these on the process info, so that we can
 * share the group between multiple communicators.
 */
/*
 * Changed parameter name from index to idx to make -Wshadow happy in gcc
 * (Unix BSD index function)
 */
/**********************************************************************/
/* Translate a process number */
static int translate (group_t *this, int idx) 
{ 	
  if (idx == MQS_INVALID_PROCESS ||
      ((unsigned int)idx) >= ((unsigned int) this->entries))
    return MQS_INVALID_PROCESS;
  else
    return this->local_to_global[idx]; 
} /* translate */

/**********************************************************************/
/* Reverse translate a process number i.e. global to local*/
static int reverse_translate (group_t * this, int idx) 
{ 	
  int i;
  for (i=0; i<this->entries; i++)
    if (this->local_to_global[i] == idx)
      return i;

  return MQS_INVALID_PROCESS;
} /* reverse_translate */

/**********************************************************************/
/* Search the group list for this group, if not found create it.
 */
static group_t * find_or_create_group (mqs_process *proc,
				       mqs_tword_t np,
				       mqs_taddr_t table)
{
  mpich_process_info *p_info = (mpich_process_info *)mqs_get_process_info (proc);
  /*  mqs_image * image          = mqs_get_image (proc); */
  /*  mpich_image_info *i_info   = (mpich_image_info *)mqs_get_image_info (image); */
  int intsize = p_info->sizes.int_size;
  communicator_t *comm  = p_info->communicator_list;
  int *tr;
  char *trbuffer;
  int i;
  group_t *g;

  if (np <= 0)
    return 0;					/* Makes no sense ! */

  /* Iterate over each communicator seeing if we can find this group */
  for (;comm; comm = comm->next)
    {
      g = comm->group;
      if (g && g->table_base == table)
	{
	  g->ref_count++;			/* Someone else is interested */
	  return g;
	}
    }

  /* Hmm, couldn't find one, so fetch it */	
  g = (group_t *)mqs_malloc (sizeof (group_t));
  tr = (int *)mqs_malloc (np*sizeof(int));
  trbuffer = (char *)mqs_malloc (np*intsize);
  g->local_to_global = tr;

  if (mqs_ok != mqs_fetch_data (proc, table, np*intsize, trbuffer) )
    {
      mqs_free (g);
      mqs_free (tr);
      mqs_free (trbuffer);
      return NULL;
    }

  /* This code is assuming that sizeof(int) is the same on target and host...
   * that's a bit flaky, but probably actually OK.
   */
  for (i=0; i<np; i++)
    mqs_target_to_host (proc, trbuffer+intsize*i, &tr[i], intsize);

  mqs_free (trbuffer);

  g->entries = np;
  g->ref_count = 1;

  return g;
} /* find_or_create_group */

/***********************************************************************/
static void group_decref (group_t * group)
{
  if (--(group->ref_count) == 0)
    {
      mqs_free (group->local_to_global);
      mqs_free (group);
    }
} /* group_decref */

/***********************************************************************
 * Perform basic setup for the image, we just allocate and clear
 * our info.
 */
int mqs_setup_image (mqs_image *image, const mqs_image_callbacks *icb)
{
  mpich_image_info *i_info = (mpich_image_info *)mqs_malloc (sizeof (mpich_image_info));

  if (!i_info)
    return err_no_store;

  memset ((void *)i_info, 0, sizeof (mpich_image_info));
  i_info->image_callbacks = icb;		/* Before we do *ANYTHING* */

  mqs_put_image_info (image, (mqs_image_info *)i_info);
  
  return mqs_ok;
} /* mqs_setup_image */


/***********************************************************************
 * Check for all the information we require to access the MPICH message queues.
 * Stash it into our structure on the image if we're succesful.
 */

/* A macro to save a lot of typing. */
/* If old-style CPP concatenation is used, no spaces can be present in 
   the uses of GETOFFSET.  E.g., GETOFFSET(q_type,first) is ok but
   GETOFFSET(q_type, first) is not. */
#define GETOFFSET(type,field)							\
do {										\
  i_info->concat(field,_offs) = mqs_field_offset(type, stringize(field));	\
  if (i_info->concat(field,_offs) < 0)						\
    return concat (err_,field);                                                 \
} while (0)

int mqs_image_has_queues (mqs_image *image, char **message)
{
  mpich_image_info * i_info = (mpich_image_info *)mqs_get_image_info (image);
  int have_qhdr = FALSE;
  int have_queue= FALSE;
  int have_qel  = FALSE;
  int have_sq   = FALSE;
  int have_sqel = FALSE;
  int have_rh   = FALSE;
  int have_co   = FALSE;
  int have_cl   = FALSE;

  /* Default failure message ! */
  *message = "The symbols and types in the MPICH library used by TotalView\n"
	     "to extract the message queues are not as expected in\n"
	     "the image '%s'\n"
	     "No message queue display is possible.\n"
	     "This is probably an MPICH version or configuration problem.";

  /* Force in the file containing our breakpoint function, to ensure that 
   * types have been read from there before we try to look them up.
   */
  mqs_find_function (image, "MPIR_Breakpoint", mqs_lang_c, NULL);

  /* Are we supposed to ignore this ? (e.g. it's really an HPF runtime using the
   * MPICH process acquisition, but not wanting queue display) 
   */
  if (mqs_find_symbol (image, "MPIR_Ignore_queues", NULL) == mqs_ok)
    {
      *message = NULL;				/* Fail silently */
      return err_silent_failure;
    }

  {
    mqs_type *qh_type = mqs_find_type (image, "MPID_QHDR", mqs_lang_c);
	
    if (qh_type)
      {
	have_qhdr = TRUE;
	GETOFFSET(qh_type,unexpected);
	GETOFFSET(qh_type,posted);
      }
  }

  {
    mqs_type *q_type = mqs_find_type (image,"MPID_QUEUE",mqs_lang_c);
    if (q_type)
      {
	have_queue = TRUE;
	GETOFFSET(q_type,first);
      }
  }
      
  { /* Now fill in fields from MPID_QEL */
    mqs_type * qel_type = mqs_find_type (image,"MPID_QEL",mqs_lang_c);
    if (qel_type)
      {
	have_qel = TRUE;
	GETOFFSET(qel_type,context_id);
	GETOFFSET(qel_type,tag);
	GETOFFSET(qel_type,tagmask);
	GETOFFSET(qel_type,lsrc);
	GETOFFSET(qel_type,srcmask);
	GETOFFSET(qel_type,next);
	GETOFFSET(qel_type,ptr);
      }
  }       

  { /* Fields from MPIR_SQUEUE */
    mqs_type * sq_type = mqs_find_type (image,"MPIR_SQUEUE",mqs_lang_c);
    if (sq_type)
      {
	have_sq = TRUE;
	GETOFFSET(sq_type,sq_head);
      }
  }

  { /* Fields from MPIR_SQEL */
    mqs_type * sq_type = mqs_find_type (image,"MPIR_SQEL",mqs_lang_c);
    if (sq_type)
      {
	have_sqel = TRUE;

	GETOFFSET(sq_type,db_shandle);
	GETOFFSET(sq_type,db_comm);
	GETOFFSET(sq_type,db_target);
	GETOFFSET(sq_type,db_tag);
	GETOFFSET(sq_type,db_data);
	GETOFFSET(sq_type,db_byte_length);
	GETOFFSET(sq_type,db_next);
      }
  }

  { /* Now fill in fields from MPIR_RHANDLE */
    mqs_type * rh_type = mqs_find_type (image,"MPIR_RHANDLE",mqs_lang_c);
    int status_offset;
    mqs_type *status_type;

    if (rh_type)
      {
	have_rh = TRUE;

	GETOFFSET(rh_type,is_complete);
	GETOFFSET(rh_type,buf);
	GETOFFSET(rh_type,len);

	/* Digital MPI doesn't provide this, so we handle not having it below,
	 * and don't complain about it
	 */
	i_info->start_offs = mqs_field_offset (rh_type, "start");

	/* And from the nested MPI_Status object. This is less pleasant */
	status_offset = mqs_field_offset (rh_type, "s");
	if (status_offset < 0)
	  return err_s;

	status_type = mqs_find_type (image, "MPI_Status",mqs_lang_c);
	if (!status_type)
	  return err_failed_status;
    
	/* Adjust the offsets of the embedded fields */
	GETOFFSET(status_type,count);
	i_info->count_offs += status_offset;
	GETOFFSET(status_type,MPI_SOURCE);
	i_info->MPI_SOURCE_offs += status_offset;      
	GETOFFSET(status_type,MPI_TAG);
	i_info->MPI_TAG_offs += status_offset;
      }
  }
      
  { /* Fields from the MPIR_Comm_list */
    mqs_type * cl_type = mqs_find_type (image,"MPIR_Comm_list",mqs_lang_c);
    if (cl_type)
      {
	have_cl = TRUE;
	GETOFFSET(cl_type,sequence_number);
	GETOFFSET(cl_type,comm_first);
      }
  }

  { /* Fields from the communicator */
    mqs_type * co_type = mqs_find_type (image, "MPIR_Communicator",mqs_lang_c);
    if (co_type)
      {
	have_co = TRUE;
	GETOFFSET(co_type,np);
	GETOFFSET(co_type,lrank_to_grank);
	GETOFFSET(co_type,send_context);
	GETOFFSET(co_type,recv_context);
	GETOFFSET(co_type,comm_next);
	GETOFFSET(co_type,comm_name);
      }
  }

  /* If we have none of the symbols we expect we decide that this isn't even
   * trying to be an MPICH code, and give up silently.
   */
  if (!have_qhdr && !have_queue && !have_qel  &&
      !have_sq   && !have_sqel &&  !have_rh   &&
      !have_co   && !have_cl)
    {
      *message = NULL;				/* Fail silently */
      return err_silent_failure;
    }

  /* Now check each status individually, we know at least one test
   * succeeded, so this is trying to be an MPICH code and it's worth
   * complaining vocally 
   */
  if (!have_qhdr)
    return  err_failed_qhdr;
  if (!have_queue)
    return err_failed_queue;
  if (!have_qel)
    return err_failed_qel;
  if (!have_sq)
    return err_failed_squeue;
  if (!have_sqel)
    return err_failed_sqel;
  if (!have_rh)
    return err_failed_rhandle;
  if (!have_co)
    return err_failed_communicator;
  if (!have_cl)
    return err_failed_commlist;

  *message = NULL;

  /* Also check for the sendq symbols */
  if (mqs_find_symbol (image, "MPIR_Sendq", NULL) != mqs_ok)
    *message = "The MPICH library built into the image '%s'\n"
	       "does not have the send queue symbol MPIR_Sendq in it, it has probably\n"
	       "been configured without the '-debug' flag.\n"
	       "No send queue display is possible without that.";

  return mqs_ok;
} /* mqs_image_has_queues */

/***********************************************************************
 * Setup information needed for a specific process.
 * TV assumes that this will hang something onto the process,
 * if nothing is attached to it, then TV will believe that this process
 * has no message queue information.
 */
int mqs_setup_process (mqs_process *process, const mqs_process_callbacks *pcb)
{ 
  /* Extract the addresses of the global variables we need and save them away */
  mpich_process_info *p_info = (mpich_process_info *)mqs_malloc (sizeof (mpich_process_info));

  if (p_info)
    {
      mqs_image        *image;
      mpich_image_info *i_info;

      p_info->process_callbacks = pcb;

      /* Now we can get the rest of the info ! */
      image  = mqs_get_image (process);
      i_info = (mpich_image_info *)mqs_get_image_info (image);

      /* Library starts at zero, so this ensures we go look to start with */
      p_info->communicator_sequence = -1;
      /* We have no communicators yet */
      p_info->communicator_list     = NULL;
      mqs_get_type_sizes (process, &p_info->sizes);

      mqs_put_process_info (process, (mqs_process_info *)p_info);
      
      return mqs_ok;
    }
  else
    return err_no_store;
} /* mqs_setup_process */

/***********************************************************************
 * Check the process for message queues.
 */
int mqs_process_has_queues (mqs_process *proc, char **msg)
{
  mpich_process_info *p_info = (mpich_process_info *)mqs_get_process_info (proc);
  mqs_image * image          = mqs_get_image (proc);
  mpich_image_info *i_info   = (mpich_image_info *)mqs_get_image_info (image);
  mqs_taddr_t debugged_addr;

  /* Don't bother with a pop up here, it's unlikely to be helpful */
  *msg = 0;

  if (mqs_find_symbol (image, "MPIR_All_communicators", &p_info->commlist_base) != mqs_ok)
    return err_all_communicators;
  
  if (mqs_find_symbol (image, "MPID_recvs", &p_info->queue_base) != mqs_ok)
    return err_mpid_recvs;
  
  /* Check for a send queue */
  if (mqs_ok != mqs_find_symbol (image, "MPIR_Sendq", &p_info->sendq_base) ||
      mqs_ok != mqs_find_symbol (image, "MPIR_being_debugged", &debugged_addr))
    {
      p_info->has_sendq = FALSE;
    }
  else
    {
      p_info->has_sendq = fetch_int (proc, debugged_addr, p_info) != 0;
    }
  
  return mqs_ok;
} /* mqs_setup_process_info */

/***********************************************************************
 * Check if the communicators have changed by looking at the 
 * sequence number.
 */
static int communicators_changed (mqs_process *proc)
{
  mpich_process_info *p_info = (mpich_process_info *)mqs_get_process_info (proc);
  mqs_image * image          = mqs_get_image (proc);
  mpich_image_info *i_info   = (mpich_image_info *)mqs_get_image_info (image);
  mqs_tword_t new_seq = fetch_int (proc, 
			       p_info->commlist_base+i_info->sequence_number_offs,
			       p_info);
  int  res = (new_seq != p_info->communicator_sequence);
      
  /* Save the sequence number for next time */
  p_info->communicator_sequence = new_seq;

  return res;
} /* mqs_communicators_changed */

/***********************************************************************
 * Find a matching communicator on our list. We check the recv context
 * as well as the address since the communicator structures may be
 * being re-allocated from a free list, in which case the same
 * address will be re-used a lot, which could confuse us.
 */
static communicator_t * find_communicator (mpich_process_info *p_info,
					   mqs_taddr_t comm_base, int recv_ctx)
{
  communicator_t * comm = p_info->communicator_list;

  for (; comm; comm=comm->next)
    {
      if (comm->comm_info.unique_id == comm_base &&
	  comm->recv_context == recv_ctx)
	return comm;
    }

  return NULL;
} /* find_communicator */

/***********************************************************************
 * Comparison function for sorting communicators.
 */
static int compare_comms (const void *a, const void *b)
{
  communicator_t * ca = *(communicator_t **)a;
  communicator_t * cb = *(communicator_t **)b;

  return cb->recv_context - ca->recv_context;
} /* compare_comms */

/***********************************************************************
 * Rebuild our list of communicators because something has changed 
 */
static int rebuild_communicator_list (mqs_process *proc)
{
  mpich_process_info *p_info = (mpich_process_info *)mqs_get_process_info (proc);
  mqs_image * image          = mqs_get_image (proc);
  mpich_image_info *i_info   = (mpich_image_info *)mqs_get_image_info (image);
  mqs_taddr_t comm_base = fetch_pointer (proc, 
					 p_info->commlist_base+i_info->comm_first_offs,
					 p_info);
  communicator_t **commp;
  int commcount = 0;

  /* Iterate over the list in the process comparing with the list
   * we already have saved. This is n**2, because we search for each
   * communicator on the existing list. I don't think it matters, though
   * because there aren't that many communicators to worry about, and
   * we only ever do this if something changed.
   */
  while (comm_base)
    {
      /* We do have one to look at, so extract the info */
      int recv_ctx = fetch_int (proc, comm_base+i_info->recv_context_offs, p_info);
      communicator_t *old = find_communicator (p_info, comm_base, recv_ctx);
      mqs_taddr_t namep = fetch_pointer (proc, comm_base+i_info->comm_name_offs,p_info);
      char *name = "--unnamed--";
      char namebuffer[64];

      if (namep)
	{
	  if (mqs_fetch_data (proc, namep, 64, namebuffer) == mqs_ok &&
	      namebuffer[0] != 0)
	    name = namebuffer;
	}

      if (old)
	{
	  old->present = TRUE;			/* We do want this communicator */
	  strncpy (old->comm_info.name, name, 64); /* Make sure the name is up to date,
						    * it might have changed and we can't tell.
						    */
	}
      else
	{
	  mqs_taddr_t group_base = fetch_pointer (proc, comm_base+i_info->lrank_to_grank_offs,
					      p_info);
	  int np     = fetch_int (proc, comm_base+i_info->np_offs,p_info);
	  group_t *g = find_or_create_group (proc, np, group_base);
	  communicator_t *nc;

	  if (!g)
	    return err_group_corrupt;

	  nc = (communicator_t *)mqs_malloc (sizeof (communicator_t));

	  /* Save the results */
	  nc->next = p_info->communicator_list;
	  p_info->communicator_list = nc;
	  nc->present = TRUE;
	  nc->group   = g;
	  nc->recv_context = recv_ctx;

	  strncpy (nc->comm_info.name, name, 64);
	  nc->comm_info.unique_id = comm_base;
	  nc->comm_info.size      = np;
	  nc->comm_info.local_rank= reverse_translate (g, mqs_get_global_rank (proc));
	}
      /* Step to the next communicator on the list */
      comm_base = fetch_pointer (proc, comm_base+i_info->comm_next_offs, p_info);
    }

  /* Now iterate over the list tidying up any communicators which
   * no longer exist, and cleaning the flags on any which do.
   */
  commp = &p_info->communicator_list;

  for (; *commp; commp = &(*commp)->next)
    {
      communicator_t *comm = *commp;

      if (comm->present)
	{
	  comm->present = FALSE;
	  commcount++;
	}
      else
	{ /* It needs to be deleted */
	  *commp = comm->next;			/* Remove from the list */
	  group_decref (comm->group);		/* Group is no longer referenced from here */
	  mqs_free (comm);
	}
    }

  if (commcount)
    {
      /* Sort the list so that it is displayed in some semi-sane order. */
      communicator_t ** comm_array = (communicator_t **) mqs_malloc (
	                               commcount * sizeof (communicator_t *));
      communicator_t *comm = p_info->communicator_list;
      int i;
      for (i=0; i<commcount; i++, comm=comm->next)
	comm_array [i] = comm;

      /* Do the sort */
      qsort (comm_array, commcount, sizeof (communicator_t *), compare_comms);

      /* Re build the list */
      p_info->communicator_list = NULL;
      for (i=0; i<commcount; i++)
	{
	  comm = comm_array[i];
	  comm->next = p_info->communicator_list;
	  p_info->communicator_list = comm;
	}

      mqs_free (comm_array);
    }

  return mqs_ok;
} /* rebuild_communicator_list */

/***********************************************************************
 * Update the list of communicators in the process if it has changed.
 */
int mqs_update_communicator_list (mqs_process *proc)
{
  if (communicators_changed (proc))
    return rebuild_communicator_list (proc);
  else
    return mqs_ok;
} /* mqs_update_communicator_list */

/***********************************************************************
 * Setup to iterate over communicators.
 * This is where we check whether our internal communicator list needs
 * updating and if so do it.
 */
int mqs_setup_communicator_iterator (mqs_process *proc)
{
  mpich_process_info *p_info = (mpich_process_info *)mqs_get_process_info (proc);

  /* Start at the front of the list again */
  p_info->current_communicator = p_info->communicator_list;
  /* Reset the operation iterator too */
  p_info->next_msg = 0;

  return p_info->current_communicator == NULL ? mqs_end_of_list : mqs_ok;
} /* mqs_setup_communicator_iterator */

/***********************************************************************
 * Fetch information about the current communicator.
 */
int mqs_get_communicator (mqs_process *proc, mqs_communicator *comm)
{
  mpich_process_info *p_info = (mpich_process_info *)mqs_get_process_info (proc);

  if (p_info->current_communicator)
    {
      *comm = p_info->current_communicator->comm_info;
  
      return mqs_ok;
    }
  else
    return err_no_current_communicator;
} /* mqs_get_communicator */

/***********************************************************************
 * Get the group information about the current communicator.
 */
int mqs_get_comm_group (mqs_process *proc, int *group_members)
{
  mpich_process_info *p_info = (mpich_process_info *)mqs_get_process_info (proc);
  communicator_t     *comm   = p_info->current_communicator;

  if (comm)
    {
      group_t * g = comm->group;
      int i;

      for (i=0; i<g->entries; i++)
	group_members[i] = g->local_to_global[i];

      return mqs_ok;
    }
  else
    return err_no_current_communicator;
} /* mqs_get_comm_group */

/***********************************************************************
 * Step to the next communicator.
 */
int mqs_next_communicator (mqs_process *proc)
{
  mpich_process_info *p_info = (mpich_process_info *)mqs_get_process_info (proc);

  p_info->current_communicator = p_info->current_communicator->next;
  
  return (p_info->current_communicator != NULL) ? mqs_ok : mqs_end_of_list;
} /* mqs_next_communicator */

/***********************************************************************
 * Setup to iterate over pending operations 
 */
int mqs_setup_operation_iterator (mqs_process *proc, int op)
{
  mpich_process_info *p_info = (mpich_process_info *)mqs_get_process_info (proc);
  mqs_image * image          = mqs_get_image (proc);
  mpich_image_info *i_info   = (mpich_image_info *)mqs_get_image_info (image);

  p_info->what = (mqs_op_class)op;

  switch (op)
    {
    case mqs_pending_sends:
      if (!p_info->has_sendq)
	return mqs_no_information;
      else
	{
	  p_info->next_msg = p_info->sendq_base + i_info->sq_head_offs;
	  return mqs_ok;
	}

    case mqs_pending_receives:
      p_info->next_msg = p_info->queue_base + i_info->posted_offs;
      return mqs_ok;

    case mqs_unexpected_messages:
      p_info->next_msg = p_info->queue_base + i_info->unexpected_offs;
      return mqs_ok;

    default:
      return err_bad_request;
    }
} /* mqs_setup_operation_iterator */

/***********************************************************************
 * Handle the unexpected queue and the pending receive queue.
 * They're very similar.
 */
static int fetch_receive (mqs_process *proc, mpich_process_info *p_info,
			  mqs_pending_operation *res, int look_for_user_buffer)
{
  mqs_image * image          = mqs_get_image (proc);
  mpich_image_info *i_info   = (mpich_image_info *)mqs_get_image_info (image);
  communicator_t   *comm   = p_info->current_communicator;
  mqs_tword_t wanted_context = comm->recv_context;
  mqs_taddr_t base           = fetch_pointer (proc, p_info->next_msg, p_info);

  while (base != 0)
    { /* Well, there's a queue, at least ! */
      mqs_tword_t actual_context = fetch_int (proc, base + i_info->context_id_offs, p_info);
      
      if (actual_context == wanted_context)
	{ /* Found a good one */
	  mqs_tword_t tag     = fetch_int (proc, base + i_info->tag_offs, p_info);
	  mqs_tword_t tagmask = fetch_int (proc, base + i_info->tagmask_offs, p_info);
	  mqs_tword_t lsrc    = fetch_int (proc, base + i_info->lsrc_offs, p_info);
	  mqs_tword_t srcmask = fetch_int (proc, base + i_info->srcmask_offs, p_info);
	  mqs_taddr_t ptr     = fetch_pointer (proc, base + i_info->ptr_offs, p_info);
	  
	  /* Fetch the fields from the MPIR_RHANDLE */
	  int is_complete = fetch_int (proc, ptr + i_info->is_complete_offs, p_info);
	  mqs_taddr_t buf     = fetch_pointer (proc, ptr + i_info->buf_offs, p_info);
	  mqs_tword_t len     = fetch_int (proc, ptr + i_info->len_offs, p_info);
	  mqs_tword_t count   = fetch_int (proc, ptr + i_info->count_offs, p_info);

	  /* If we don't have start, then use buf instead... */
	  mqs_taddr_t start;
	  if (i_info->start_offs < 0)
	    start = buf;
	  else
	    start = fetch_pointer (proc, ptr + i_info->start_offs, p_info);

	  /* Hurrah, we should now be able to fill in all the necessary fields in the
	   * result !
	   */
	  res->status = is_complete ? mqs_st_complete : mqs_st_pending; /* We can't discern matched */
	  if (srcmask == 0)
	    {
	      res->desired_local_rank  = -1;
	      res->desired_global_rank = -1;
	    }
	  else
	    {
	      res->desired_local_rank  = lsrc;
	      res->desired_global_rank = translate (comm->group, lsrc);
	      
	    }
	  res->tag_wild       = (tagmask == 0);
	  res->desired_tag    = tag;
	  
	  if (look_for_user_buffer)
	    {
	      res->system_buffer  = FALSE;
	      res->buffer         = buf;
	      res->desired_length = len;
	    }
	  else
	    {
	      res->system_buffer  = TRUE;
	      /* Correct an oddity. If the buffer length is zero then no buffer
	       * is allocated, but the descriptor is left with random data.
	       */
	      if (count == 0)
		start = 0;
	      
	      res->buffer         = start;
	      res->desired_length = count;
	    }

	  if (is_complete)
	    { /* Fill in the actual results, rather than what we were looking for */
	      mqs_tword_t mpi_source  = fetch_int (proc, ptr + i_info->MPI_SOURCE_offs, p_info);
	      mqs_tword_t mpi_tag  = fetch_int (proc, ptr + i_info->MPI_TAG_offs, p_info);

	      res->actual_length     = count;
	      res->actual_tag        = mpi_tag;
	      res->actual_local_rank = mpi_source;
	      res->actual_global_rank= translate (comm->group, mpi_source);
	    }

	  /* Don't forget to step the queue ! */
	  p_info->next_msg = base + i_info->next_offs;
	  return mqs_ok;
	}
      else
	{ /* Try the next one */
	  base = fetch_pointer (proc, base + i_info->next_offs, p_info);
	}
    }
  
  p_info->next_msg = 0;
  return mqs_end_of_list;
}  /* fetch_receive */

/***********************************************************************
 * Handle the send queue, somewhat different.
 */
static int fetch_send (mqs_process *proc, mpich_process_info *p_info,
		       mqs_pending_operation *res)
{
  mqs_image * image        = mqs_get_image (proc);
  mpich_image_info *i_info = (mpich_image_info *)mqs_get_image_info (image);
  communicator_t   *comm   = p_info->current_communicator;
  mqs_taddr_t base         = fetch_pointer (proc, p_info->next_msg, p_info);

  if (!p_info->has_sendq)
    return mqs_no_information;

  /* Say what operation it is. We can only see non blocking send operations
   * in MPICH. Other MPI systems may be able to show more here. 
   */
  strcpy ((char *)res->extra_text[0],"Non-blocking send");
  res->extra_text[1][0] = 0;

  while (base != 0)
    { /* Well, there's a queue, at least ! */
      /* Check if it's one we're interested in ? */
      mqs_taddr_t commp = fetch_pointer (proc, base+i_info->db_comm_offs, p_info);
      mqs_taddr_t next  = base+i_info->db_next_offs;

      if (commp == comm->comm_info.unique_id)
	{ /* Found one */
	  mqs_tword_t target = fetch_int (proc, base+i_info->db_target_offs,      p_info);
	  mqs_tword_t tag    = fetch_int (proc, base+i_info->db_tag_offs,         p_info);
	  mqs_tword_t length = fetch_int (proc, base+i_info->db_byte_length_offs, p_info);
	  mqs_taddr_t data   = fetch_pointer (proc, base+i_info->db_data_offs,    p_info);
	  mqs_taddr_t shandle= fetch_pointer (proc, base+i_info->db_shandle_offs, p_info);
	  mqs_tword_t complete=fetch_int (proc, shandle+i_info->is_complete_offs, p_info);

	  /* Ok, fill in the results */
	  res->status = complete ? mqs_st_complete : mqs_st_pending; /* We can't discern matched */
	  res->actual_local_rank = res->desired_local_rank = target;
	  res->actual_global_rank= res->desired_global_rank= translate (comm->group, target);
	  res->tag_wild   = FALSE;
	  res->actual_tag = res->desired_tag = tag;
	  res->desired_length = res->actual_length = length;
	  res->system_buffer  = FALSE;
	  res->buffer = data;

	  p_info->next_msg = next;
	  return mqs_ok;
	}
      
      base = fetch_pointer (proc, next, p_info);
    }

  p_info->next_msg = 0;
  return mqs_end_of_list;
} /* fetch_send */

/***********************************************************************
 * Fetch the next valid operation. 
 * Since MPICH only maintains a single queue of each type of operation,
 * we have to run over it and filter out the operations which
 * match the active communicator.
 */
int mqs_next_operation (mqs_process *proc, mqs_pending_operation *op)
{
  mpich_process_info *p_info = (mpich_process_info *)mqs_get_process_info (proc);

  switch (p_info->what)
    {
    case mqs_pending_receives:
      return fetch_receive (proc,p_info,op,TRUE);
    case mqs_unexpected_messages:
      return fetch_receive (proc,p_info,op,FALSE);
    case mqs_pending_sends:
      return fetch_send (proc,p_info,op);
    default: return err_bad_request;
    }
} /* mqs_next_operation */

/***********************************************************************
 * Destroy the info.
 */
void mqs_destroy_process_info (mqs_process_info *mp_info)
{
  mpich_process_info *p_info = (mpich_process_info *)mp_info;
  /* Need to handle the communicators and groups too */
  communicator_t *comm = p_info->communicator_list;

  while (comm)
    {
      communicator_t *next = comm->next;

      group_decref (comm->group);		/* Group is no longer referenced from here */
      mqs_free (comm);
      
      comm = next;
    }
  mqs_free (p_info);
} /* mqs_destroy_process_info */

/***********************************************************************
 * Free off the data we associated with an image. Since we malloced it
 * we just free it.
 */
void mqs_destroy_image_info (mqs_image_info *info)
{
  mqs_free (info);
} /* mqs_destroy_image_info */

/***********************************************************************/
static mqs_taddr_t fetch_pointer (mqs_process * proc, mqs_taddr_t addr, mpich_process_info *p_info)
{
  int asize = p_info->sizes.pointer_size;
  char data [8];				/* ASSUME a pointer fits in 8 bytes */
  mqs_taddr_t res = 0;

  if (mqs_ok == mqs_fetch_data (proc, addr, asize, data))
    mqs_target_to_host (proc, data, 
			((char *)&res) + (host_is_big_endian ? sizeof(mqs_taddr_t)-asize : 0), 
			asize);

  return res;
} /* fetch_pointer */

/***********************************************************************/
static mqs_tword_t fetch_int (mqs_process * proc, mqs_taddr_t addr, mpich_process_info *p_info)
{
  int isize = p_info->sizes.int_size;
  char buffer[8];				/* ASSUME an integer fits in 8 bytes */
  mqs_tword_t res = 0;

  if (mqs_ok == mqs_fetch_data (proc, addr, isize, buffer))
    mqs_target_to_host (proc, buffer, 
			((char *)&res) + (host_is_big_endian ? sizeof(mqs_tword_t)-isize : 0), 
			isize);
  
  return res;
} /* fetch_int */

/***********************************************************************/
/* Convert an error code into a printable string */
char * mqs_dll_error_string (int errcode)
{
  switch (errcode)
    {
    case err_silent_failure:
      return "";
    case err_no_current_communicator: 
      return "No current communicator in the communicator iterator";
    case err_bad_request:    
      return "Attempting to setup to iterate over an unknown queue of operations";
    case err_no_store: 
      return "Unable to allocate store";
    case err_failed_qhdr: 
      return "Failed to find type MPID_QHDR";
    case err_unexpected: 
      return "Failed to find field 'unexpected' in MPID_QHDR";
    case err_posted: 
      return "Failed to find field 'posted' in MPID_QHDR";
    case err_failed_queue: 
      return "Failed to find type MPID_QUEUE";
    case err_first: 
      return "Failed to find field 'first' in MPID_QUEUE";
    case err_failed_qel: 
      return "Failed to find type MPID_QEL";
    case err_context_id: 
      return "Failed to find field 'context_id' in MPID_QEL";
    case err_tag: 
      return "Failed to find field 'tag' in MPID_QEL";
    case err_tagmask: 
      return "Failed to find field 'tagmask' in MPID_QEL";
    case err_lsrc: 
      return "Failed to find field 'lsrc' in MPID_QEL";
    case err_srcmask: 
      return "Failed to find field 'srcmask' in MPID_QEL";
    case err_next: 
      return "Failed to find field 'next' in MPID_QEL";
    case err_ptr: 
      return "Failed to find field 'ptr' in MPID_QEL";
    case err_failed_squeue: 
      return "Failed to find type MPIR_SQUEUE";
    case err_sq_head: 
      return "Failed to find field 'sq_head' in MPIR_SQUEUE";
    case err_failed_sqel: 
      return "Failed to find type MPIR_SQEL";
    case err_db_shandle: 
      return "Failed to find field 'db_shandle' in MPIR_SQEL";
    case err_db_comm: 
      return "Failed to find field 'db_comm' in MPIR_SQEL";
    case err_db_target: 
      return "Failed to find field 'db_target' in MPIR_SQEL";
    case err_db_tag: 
      return "Failed to find field 'db_tag' in MPIR_SQEL";
    case err_db_data: 
      return "Failed to find field 'db_data' in MPIR_SQEL";
    case err_db_byte_length: 
      return "Failed to find field 'db_byte_length' in MPIR_SQEL";
    case err_db_next: 
      return "Failed to find field 'db_next' in MPIR_SQEL";
    case err_failed_rhandle: 
      return "Failed to find type MPIR_RHANDLE";
    case err_is_complete: 
      return "Failed to find field 'is_complete' in MPIR_RHANDLE";
    case err_buf: 
      return "Failed to find field 'buf' in MPIR_RHANDLE";
    case err_len: 
      return "Failed to find field 'len' in MPIR_RHANDLE";
    case err_s: 
      return "Failed to find field 's' in MPIR_RHANDLE";
    case err_failed_status: 
      return "Failed to find type MPI_Status";
    case err_count: 
      return "Failed to find field 'count' in MPIR_Status";
    case err_MPI_SOURCE: 
      return "Failed to find field 'MPI_SOURCE' in MPIR_Status";
    case err_MPI_TAG: 
      return "Failed to find field 'MPI_TAG' in MPIR_Status";
    case err_failed_commlist: 
      return "Failed to find type MPIR_Comm_list";
    case err_sequence_number: 
      return "Failed to find field 'sequence_number' in MPIR_Comm_list";
    case err_comm_first: 
      return "Failed to find field 'comm_first' in MPIR_Comm_list";
    case err_failed_communicator: 
      return "Failed to find type MPIR_Communicator";
    case err_np: 
      return "Failed to find field 'np' in MPIR_Communicator";
    case err_lrank_to_grank: 
      return "Failed to find field 'lrank_to_grank' in MPIR_Communicator";
    case err_send_context: 
      return "Failed to find field 'send_context' in MPIR_Communicator";
    case err_recv_context: 
      return "Failed to find field 'recv_context' in MPIR_Communicator";
    case err_comm_next: 
      return "Failed to find field 'comm_next' in MPIR_Communicator";
    case err_comm_name: 
      return "Failed to find field 'comm_name' in MPIR_Communicator";
    case err_all_communicators: 
      return "Failed to find the global symbol MPIR_All_communicators";
    case err_mpid_recvs: 
      return "Failed to find the global symbol MPID_recvs";
    case err_group_corrupt:
      return "Could not read a communicator's group from the process (probably a store corruption)";

    default: return "Unknown error code";
    }
} /* mqs_dll_error_string */


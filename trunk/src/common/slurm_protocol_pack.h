/****************************************************************************\
 *  slurm_protocol_pack.h - definitions for all pack and unpack functions
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>.
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifndef _SLURM_PROTOCOL_PACK_H
#define _SLURM_PROTOCOL_PACK_H

#if HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif  /* HAVE_INTTYPES_H */
#else   /* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif  /*  HAVE_CONFIG_H */

#include "src/common/pack.h"
#include "src/common/slurm_protocol_defs.h"

/****************************/
/* Message header functions */
/****************************/
/* pack_header
 * packs a slurm protocol header that proceeds every slurm message
 * IN header - the header structure to pack
 * IN/OUT buffer - destination of the pack, contains pointers that are
 *			automatically updated
 */
extern void pack_header ( header_t  * header , Buf buffer );

/* unpack_header
 * unpacks a slurm protocol header that proceeds every slurm message
 * OUT header - the header structure to unpack
 * IN/OUT buffer - source of the unpack data, contains pointers that are
 *			automatically updated
 * RET 0 or error code
 */
extern int unpack_header ( header_t * header , Buf buffer );


/****************************/
/* Stream header functions  */
/****************************/
/* size_io_stream_header - get the size of an I/O stream header
 * RET number of bytes in an I/O steam header
 */
extern int size_io_stream_header ( void );

/* pack_io_stream_header
 * packs an i/o stream protocol header used for stdin/out/err
 * IN header - the header structure to pack
 * IN/OUT buffer - destination of the pack, contains pointers that are
 *			automatically updated
 */
extern void pack_io_stream_header ( slurm_io_stream_header_t * header , 
                                    Buf buffer ) ;

/* unpack_io_stream_header
 * unpacks an i/o stream protocol header used for stdin/out/err
 * OUT header - the header structure to unpack
 * IN/OUT buffer - source of the unpack data, contains pointers that are
 *			automatically updated
 * RET 0 or error code
 */
extern int unpack_io_stream_header ( slurm_io_stream_header_t * header , 
                                     Buf buffer ) ;

/**************************************************************************/
/* generic case statement Pack / Unpack methods for slurm protocol bodies */
/**************************************************************************/

/* pack_msg
 * packs a generic slurm protocol message body
 * IN msg - the body structure to pack (note: includes message type)
 * IN/OUT buffer - destination of the pack, contains pointers that are 
 *			automatically updated
 * RET 0 or error code
 */
extern int pack_msg ( slurm_msg_t const * msg , Buf buffer );

/* unpack_msg
 * unpacks a generic slurm protocol message body
 * OUT msg - the body structure to unpack (note: includes message type)
 * IN/OUT buffer - source of the unpack, contains pointers that are 
 *			automatically updated
 * RET 0 or error code
 */
extern int unpack_msg ( slurm_msg_t * msgi , Buf buffer );

/***************************************************************************/
/* specific case statement Pack / Unpack methods for slurm protocol bodies */
/***************************************************************************/
/* pack_job_credential
 * packs a slurm job credential
 * IN cred - pointer to the credential
 * IN/OUT buffer - destination of the pack, contains pointers that are 
 *			automatically updated
 */
void pack_job_credential ( slurm_job_credential_t* cred , Buf buffer ) ;

/* unpack_job_credential
 * unpacks a slurm job credential
 * OUT cred - pointer to the credential pointer
 * IN/OUT buffer - source of the unpack, contains pointers that are 
 *			automatically updated
 * RET 0 or error code
 */
int unpack_job_credential( slurm_job_credential_t** cred , Buf buffer ) ;





void pack_job_desc ( job_desc_msg_t *job_desc_msg_ptr, Buf buffer );
int unpack_job_desc ( job_desc_msg_t **job_desc_msg_ptr, Buf buffer );

void pack_old_job_desc ( old_job_alloc_msg_t * job_desc_ptr, Buf buffer );
int unpack_old_job_desc ( old_job_alloc_msg_t **job_desc_buffer_ptr, 
                          Buf buffer );

void pack_last_update ( last_update_msg_t * msg , Buf buffer );
int unpack_last_update ( last_update_msg_t ** msg , Buf buffer );

void pack_job_step_create_response_msg (  job_step_create_response_msg_t* msg , 
                                          Buf buffer );
int unpack_job_step_create_response_msg (job_step_create_response_msg_t** msg , 
                                         Buf buffer );

void pack_return_code ( return_code_msg_t * msg , Buf buffer );
int unpack_return_code ( return_code_msg_t ** msg , Buf buffer );

void pack_slurm_ctl_conf ( slurm_ctl_conf_info_msg_t * build_ptr, Buf buffer );
int unpack_slurm_ctl_conf ( slurm_ctl_conf_info_msg_t **build_buffer_ptr, 
                            Buf buffer );

void pack_buffer_msg ( slurm_msg_t * msg , Buf buffer );

#define pack_job_info_msg(msg,buf)	pack_buffer_msg(msg,buf)
#define pack_job_step_info_msg(msg,buf)	pack_buffer_msg(msg,buf)

int unpack_job_info_msg ( job_info_msg_t ** msg , Buf buffer );
int unpack_job_info ( job_info_t ** job , Buf buffer );

int unpack_job_info_members ( job_info_t * job , Buf buffer );

/* job_step_info messages 
 * the pack_job_step_info_members is to be used programs such as slurmctld 
 * which do not use the protocol structures internally.
 */
void pack_job_step_info ( job_step_info_t* step, Buf buffer );
void pack_job_step_info_members(   uint32_t job_id, uint16_t step_id, 
		uint32_t user_id, time_t start_time, char *partition, 
		char *nodes, Buf buffer );
int unpack_job_step_info ( job_step_info_t ** step , Buf buffer );


void pack_partition_info_msg ( slurm_msg_t * msg, Buf buffer );
int unpack_partition_info_msg ( partition_info_msg_t ** , Buf buffer );
int unpack_partition_info ( partition_info_t ** part , Buf buffer );
int unpack_partition_info_members ( partition_info_t * part , Buf buffer );

void pack_cancel_job_msg ( job_id_msg_t * msg , Buf buffer );
int unpack_cancel_job_msg ( job_id_msg_t ** msg_ptr , Buf buffer );

/* job_step_id functions */
void pack_job_step_id ( job_step_id_t * msg , Buf buffer );
int unpack_job_step_id ( job_step_id_t ** msg_ptr , Buf buffer );

/* job_step_id Macros for typedefs */
#define pack_cancel_job_step_msg(msg,buffer)		\
	pack_job_step_id(msg,buffer)
#define unpack_cancel_job_step_msg(msg_ptr,buffer)	\
	unpack_job_step_id(msg_ptr,buffer)
#define pack_job_step_id_msg(msg,buffer)		\
	pack_job_step_id(msg,buffer)
#define unpack_job_step_id_msg(msg_ptr,buffer)		\
	unpack_job_step_id(msg_ptr,buffer)
#define pack_job_step_info_request_msg(msg,buffer)	\
	pack_job_step_id(msg,buffer)
#define unpack_job_step_info_request_msg(msg_ptr,buffer)\
	unpack_job_step_id(msg_ptr,buffer)
#define pack_job_info_request_msg(msg,buffer)		\
	pack_job_step_id(msg,buffer)
#define unpack_job_info_request_msg(msg_ptr,buffer)	\
	unpack_job_step_id(msg_ptr,buffer)

int unpack_job_step_info ( job_step_info_t ** step , Buf buffer );
int unpack_job_step_info_members ( job_step_info_t * step , Buf buffer );
int unpack_job_step_info_response_msg ( job_step_info_response_msg_t** msg, 
                                        Buf buffer );

void pack_complete_job_step ( complete_job_step_msg_t * msg , Buf buffer ) ;
int  unpack_complete_job_step ( complete_job_step_msg_t ** msg_ptr , 
                                Buf buffer ) ;

void pack_cancel_tasks_msg ( kill_tasks_msg_t * msg , Buf buffer );
int unpack_cancel_tasks_msg ( kill_tasks_msg_t ** msg_ptr , Buf buffer );

void pack_partition_table_msg ( partition_desc_msg_t *  msg , Buf buffer );
int unpack_partition_table_msg ( partition_desc_msg_t **  msg_ptr , Buf buffer );

void pack_shutdown_msg ( shutdown_msg_t * msg , Buf buffer );
int unpack_shutdown_msg ( shutdown_msg_t ** msg_ptr , Buf buffer );

void pack_launch_tasks_request_msg ( launch_tasks_request_msg_t * msg , 
                                     Buf buffer );
int unpack_launch_tasks_request_msg ( launch_tasks_request_msg_t ** msg_ptr , 
                                      Buf buffer );

void pack_launch_tasks_response_msg ( launch_tasks_response_msg_t * msg , 
                                      Buf buffer );
int unpack_launch_tasks_response_msg ( launch_tasks_response_msg_t ** msg_ptr , 
                                       Buf buffer );

void pack_kill_tasks_msg ( kill_tasks_msg_t * msg , Buf buffer );
int unpack_kill_tasks_msg ( kill_tasks_msg_t ** msg_ptr , Buf buffer );

void pack_slurm_addr_array ( slurm_addr * slurm_address , uint16_t size_val, 
                             Buf buffer );
int  unpack_slurm_addr_array ( slurm_addr ** slurm_address , 
                               uint16_t * size_val , Buf buffer );


extern void pack_get_job_step_info ( job_step_info_request_msg_t * msg , 
                                     Buf buffer );
extern int unpack_get_job_step_info ( job_step_info_request_msg_t ** msg , 
                                      Buf buffer );

void pack_reattach_tasks_streams_msg ( 
	reattach_tasks_streams_msg_t * msg , Buf buffer ) ;
int unpack_reattach_tasks_streams_msg ( 
	reattach_tasks_streams_msg_t ** msg_ptr , Buf buffer ) ;

void pack_task_exit_msg ( task_exit_msg_t * msg , Buf buffer ) ;
int unpack_task_exit_msg ( task_exit_msg_t ** msg_ptr , Buf buffer ) ;

void pack_batch_job_launch ( batch_job_launch_msg_t* cred , Buf buffer ) ;
int  unpack_batch_job_launch( batch_job_launch_msg_t** msg , Buf buffer ) ;

#endif

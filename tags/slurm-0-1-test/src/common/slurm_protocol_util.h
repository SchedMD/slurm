/*****************************************************************************\
 *  slurm_protocol_util.h - communication infrastructure functions
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> et. al.
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

#ifndef _SLURM_PROTOCOL_UTIL_H
#define _SLURM_PROTOCOL_UTIL_H

#if HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif			/* HAVE_INTTYPES_H */
#else				/* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif				/*  HAVE_CONFIG_H */

#include <stdio.h>

#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/slurm_protocol_common.h"

/* 
 * check_header_version checks to see that the specified header was sent 
 * from a node running the same version of the protocol as the current node 
 * IN header - the message header received
 * RET - SLURM error code
 */
extern uint32_t check_header_version(header_t * header);

/*
 * init_header - simple function to create a header, always insuring that 
 * an accurate version string is inserted
 * OUT header - the message header to be send
 * IN msg_type - type of message to be send
 * IN flags - message flags to be send
 */
extern void 
init_header(header_t * header, slurm_msg_type_t msg_type, uint16_t flags);

/*
 * check_io_stream_header_version checks to see that the specified header 
 *	was sent from a node running the same version of the protocol as 
 *	the current node
 * IN header - the message recevied
 * RET - SLURM error code
 */
extern uint32_t 
check_io_stream_header_version(slurm_io_stream_header_t * header);

/*
 * init_io_stream_header - simple function to create a header, always 
 *	insuring that an accurate version string is inserted
 * OUT header - the message header to be sent
 * IN key - authentication signature
 * IN task_id - message's task_id
 * IN type - message type
 */
extern void 
init_io_stream_header(slurm_io_stream_header_t * header, char *key,
                      uint32_t task_id, uint16_t type);

/*
 * update_header - update a message header with the credential and message len
 * OUT header - the message header to update
 * IN cred_length - credential length
 * IN msg_length - length of message to be send 
 */
extern void 
update_header(header_t * header, uint32_t cred_length, uint32_t msg_length);

/*
 * read an i/o stream header from the supplied slurm stream
 *	data is copied in a block
 * IN fd - the file descriptor to read from
 * OUT header - the header read
 * RET number of bytes read
 */
extern int read_io_stream_header(slurm_io_stream_header_t * header, slurm_fd fd);

/*
 * read an i/o stream header from the supplied slurm stream
 *	data is unpacked field by field
 * IN fd - the file descriptor to read from
 * OUT header - the header read
 * RET number of bytes read
 */
extern int read_io_stream_header2(slurm_io_stream_header_t * header, slurm_fd fd);

/*
 * write an i/o stream header to the supplied slurm stream
 *	data is copied in a block
 * IN fd - the file descriptor to read from
 * IN header - the header read
 * RET number of bytes written
 */
extern int write_io_stream_header(slurm_io_stream_header_t * header, slurm_fd fd);

/* log the supplied slurm credential as debug3() level */
extern void slurm_print_job_credential(slurm_job_credential_t * credential);

/*
 * write an i/o stream header to the supplied slurm stream
 *	data is packed field by field
 * IN fd - the file descriptor to read from
 * IN header - the header read
 * RET number of bytes written
 */
extern int write_io_stream_header2(slurm_io_stream_header_t * header, slurm_fd fd);

/* log the supplied slurm task launch message as debug3() level */
extern void slurm_print_launch_task_msg(launch_tasks_request_msg_t * msg);

#endif /* !_SLURM_PROTOCOL_UTIL_H */

/*****************************************************************************\
 *  slurm_errno.h - error codes and functions for slurm
 * This implementation relies on "overloading" the libc errno by 
 *  partitioning its domain into system (<1000) and SLURM (>=1000) values.
 *  SLURM API functions should call slurm_seterrno() to set errno to a value.
 *  API users should call slurm_strerror() to convert all errno values to
 *  their description strings.
 ******************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>, Jim Garlick <garlick@llnl.gov>, et. al.
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
#ifndef _SLURM_ERRNO_H
#define _SLURM_ERRNO_H

#include <errno.h>
/* set errno to the specified value - then return -1 */ 
#define slurm_seterrno_ret(errnum) do { slurm_seterrno(errnum); return (-1); } while (0)

/* general return codes */
#define SLURM_SUCCESS 0
#define SLURM_ERROR -1 
#define SLURM_FAILURE -1

/* to mimick bash on task launch failure*/
#define SLURM_EXIT_FAILURE_CODE 127

/* general communication layer return codes */
#define SLURM_SOCKET_ERROR -1
#define SLURM_PROTOCOL_SUCCESS 0
#define SLURM_PROTOCOL_ERROR -1

enum {
	/* General Message error codes */
	SLURM_UNEXPECTED_MSG_ERROR = 			1000,
	SLURM_COMMUNICATIONS_CONNECTION_ERROR,
	SLURM_COMMUNICATIONS_SEND_ERROR,
	SLURM_COMMUNICATIONS_RECEIVE_ERROR,
	SLURM_COMMUNICATIONS_SHUTDOWN_ERROR,
	SLURM_PROTOCOL_VERSION_ERROR,
	SLURM_PROTOCOL_IO_STREAM_VERSION_ERROR,

	/* _info.c/ocommuncation layer RESPONSE_SLURM_RC message codes */
	SLURM_NO_CHANGE_IN_DATA =			1900,

	/* slurmctld error codes */
	ESLURM_INVALID_PARTITION_NAME = 		2000,
	ESLURM_DEFAULT_PARTITION_NOT_SET,
	ESLURM_ACCESS_DENIED,
	ESLURM_JOB_MISSING_REQUIRED_PARTITION_GROUP,
	ESLURM_REQUESTED_NODES_NOT_IN_PARTITION,
	ESLURM_TOO_MANY_REQUESTED_CPUS,
	ESLURM_TOO_MANY_REQUESTED_NODES,
	ESLURM_ERROR_ON_DESC_TO_RECORD_COPY,
	ESLURM_JOB_MISSING_SIZE_SPECIFICATION,
	ESLURM_JOB_SCRIPT_MISSING,
	ESLURM_USER_ID_MISSING,
	ESLURM_JOB_NAME_TOO_LONG,
	ESLURM_DUPLICATE_JOB_ID,
	ESLURM_PATHNAME_TOO_LONG,
	ESLURM_NOT_TOP_PRIORITY,
	ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE,
	ESLURM_NODES_BUSY,
	ESLURM_INVALID_JOB_ID,
	ESLURM_INVALID_NODE_NAME,
	ESLURM_WRITING_TO_FILE,
	ESLURM_TRANSITION_STATE_NO_UPDATE,
	ESLURM_ALREADY_DONE,

	/* Quadrics Elan routine error codes */
	ENOSLURM =					3000,
	EBADMAGIC_QSWLIBSTATE,
	EBADMAGIC_QSWJOBINFO,
	EINVAL_PRGCREATE,
	ECHILD_PRGDESTROY,
	EEXIST_PRGDESTROY,
	EELAN3INIT,
	EELAN3CONTROL,
	EELAN3CREATE,
	ESRCH_PRGADDCAP,
	EFAULT_PRGADDCAP,
	EINVAL_SETCAP,
	EFAULT_SETCAP,
	EGETNODEID,
	EGETNODEID_BYHOST,
	EGETHOST_BYNODEID,
	ESRCH_PRGSIGNAL,
	EINVAL_PRGSIGNAL,

	/* slurmd error codes */
	ESLRUMD_PIPE_ERROR_ON_TASK_SPAWN =		4000,
	ESLURMD_KILL_TASK_FAILED,
	ESLURMD_OPENSSL_ERROR,
	ESLURMD_NO_AVAILABLE_JOB_STEP_SLOTS_IN_SHMEM,
	ESLURMD_NO_AVAILABLE_TASK_SLOTS_IN_SHMEM,
	ESLURMD_INVALID_JOB_CREDENTIAL,
	ESLURMD_NODE_NAME_NOT_PRESENT_IN_CREDENTIAL,
	ESLURMD_CREDENTIAL_EXPIRED,
	ESLURMD_CREDENTIAL_REVOKED,
	ESLURMD_CREDENTIAL_TO_EXPIRE_DOESNOT_EXIST,
	ESLURMD_ERROR_SIGNING_CREDENTIAL,
	ESLURMD_ERROR_FINDING_JOB_STEP_IN_SHMEM,
	ESLURMD_CIRBUF_POINTER_0,
	ESLURMD_PIPE_DISCONNECT,
	ESLURMD_EOF_ON_SOCKET,
	ESLURMD_SOCKET_DISCONNECT,
	ESLURMD_UNKNOWN_SOCKET_ERROR,
	ESLURMD_SIGNATURE_FIELD_TOO_SMALL,
	/* socket specific SLURM communications error */
	SLURM_PROTOCOL_SOCKET_IMPL_ZERO_RECV_LENGTH =	5000,
	SLURM_PROTOCOL_SOCKET_IMPL_NEGATIVE_RECV_LENGTH,
	SLURM_PROTOCOL_SOCKET_IMPL_NOT_ALL_DATA_SENT,
};

/* look up an errno value */
extern char * slurm_strerror(int errnum);

/* set an errno value */
extern void slurm_seterrno(int errnum);

/* get an errno value */
extern inline int slurm_get_errno();

/* print message: error string for current errno value */
extern void slurm_perror(char *msg);

#endif

/*****************************************************************************\
 *  slurm_errno.h - error codes and functions for slurm
 ******************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>, 
 *	Jim Garlick <garlick@llnl.gov>, et. al.
 *  LLNL-CODE-402394.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission 
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and 
 *  distribute linked combinations including the two. You must obey the GNU 
 *  General Public License in all respects for all of the code used other than 
 *  OpenSSL. If you modify file(s) with this exception, you may extend this 
 *  exception to your version of the file(s), but you are not obligated to do 
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in 
 *  the program, then also delete it here.
 *
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/
#ifndef _SLURM_ERRNO_H
#define _SLURM_ERRNO_H

/* BEGIN_C_DECLS should be used at the beginning of your declarations,
   so that C++ compilers don't mangle their names.  Use _END_C_DECLS at
   the end of C declarations. */
#undef BEGIN_C_DECLS
#undef END_C_DECLS
#ifdef __cplusplus
# define BEGIN_C_DECLS	extern "C" {
# define END_C_DECLS	}
#else
# define BEGIN_C_DECLS	/* empty */
# define END_C_DECLS	/* empty */
#endif

/* PARAMS is a macro used to wrap function prototypes, so that compilers
   that don't understand ANSI C prototypes still work, and ANSI C
   compilers can issue warnings about type mismatches.  */
#undef PARAMS
#if defined (__STDC__) || defined (_AIX) \
	|| (defined (__mips) && defined (_SYSTYPE_SVR4)) \
	|| defined(WIN32) || defined(__cplusplus)
# define PARAMS(protos)	protos
#else
# define PARAMS(protos)	()
#endif

BEGIN_C_DECLS

#include <errno.h>

/* set errno to the specified value - then return -1 */ 
#define slurm_seterrno_ret(errnum) do { \
	slurm_seterrno(errnum);         \
	return (errnum ? -1 : 0);       \
        } while (0)

/* general return codes */
#define SLURM_SUCCESS   0
#define SLURM_ERROR    -1 
#define SLURM_FAILURE  -1

/* general communication layer return codes */
#define SLURM_SOCKET_ERROR     -1
#define SLURM_PROTOCOL_SUCCESS  0
#define SLURM_PROTOCOL_ERROR   -1

enum {
	/* General Message error codes */
	SLURM_UNEXPECTED_MSG_ERROR = 			1000,
	SLURM_COMMUNICATIONS_CONNECTION_ERROR,
	SLURM_COMMUNICATIONS_SEND_ERROR,
	SLURM_COMMUNICATIONS_RECEIVE_ERROR,
	SLURM_COMMUNICATIONS_SHUTDOWN_ERROR,
	SLURM_PROTOCOL_VERSION_ERROR,
	SLURM_PROTOCOL_IO_STREAM_VERSION_ERROR,
	SLURM_PROTOCOL_AUTHENTICATION_ERROR,
	SLURM_PROTOCOL_INSANE_MSG_LENGTH,
	SLURM_MPI_PLUGIN_NAME_INVALID,
	SLURM_MPI_PLUGIN_PRELAUNCH_SETUP_FAILED,
	SLURM_PLUGIN_NAME_INVALID,

	/* communication failures to/from slurmctld */
	SLURMCTLD_COMMUNICATIONS_CONNECTION_ERROR =     1800,
	SLURMCTLD_COMMUNICATIONS_SEND_ERROR,
	SLURMCTLD_COMMUNICATIONS_RECEIVE_ERROR,
	SLURMCTLD_COMMUNICATIONS_SHUTDOWN_ERROR,

	/* _info.c/communcation layer RESPONSE_SLURM_RC message codes */
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
	ESLURM_DUPLICATE_JOB_ID,
	ESLURM_PATHNAME_TOO_LONG,
	ESLURM_NOT_TOP_PRIORITY,
	ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE,
	ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE,
	ESLURM_NODES_BUSY,
	ESLURM_INVALID_JOB_ID,
	ESLURM_INVALID_NODE_NAME,
	ESLURM_WRITING_TO_FILE,
	ESLURM_TRANSITION_STATE_NO_UPDATE,
	ESLURM_ALREADY_DONE,
	ESLURM_INTERCONNECT_FAILURE,
	ESLURM_BAD_DIST,
	ESLURM_JOB_PENDING,
	ESLURM_BAD_TASK_COUNT,
	ESLURM_INVALID_JOB_CREDENTIAL,
	ESLURM_IN_STANDBY_MODE,
	ESLURM_INVALID_NODE_STATE,
	ESLURM_INVALID_FEATURE,
	ESLURM_INVALID_AUTHTYPE_CHANGE,
	ESLURM_INVALID_CHECKPOINT_TYPE_CHANGE,
	ESLURM_INVALID_SCHEDTYPE_CHANGE,
	ESLURM_INVALID_SELECTTYPE_CHANGE,
	ESLURM_INVALID_SWITCHTYPE_CHANGE,
	ESLURM_FRAGMENTATION,
	ESLURM_NOT_SUPPORTED,
	ESLURM_DISABLED,
	ESLURM_DEPENDENCY,
	ESLURM_BATCH_ONLY,
	ESLURM_TASKDIST_ARBITRARY_UNSUPPORTED,
	ESLURM_TASKDIST_REQUIRES_OVERCOMMIT,
	ESLURM_JOB_HELD,
	ESLURM_INVALID_CRYPTO_TYPE_CHANGE,
	ESLURM_INVALID_BANK_ACCOUNT,
	ESLURM_INVALID_TASK_MEMORY,
	ESLURM_INVALID_ACCOUNT,
	ESLURM_INVALID_LICENSES,
	ESLURM_NEED_RESTART,
	ESLURM_ACCOUNTING_POLICY,
	ESLURM_INVALID_TIME_LIMIT,
	ESLURM_RESERVATION_ACCESS,
	ESLURM_RESERVATION_INVALID,
	ESLURM_INVALID_TIME_VALUE,
	ESLURM_RESERVATION_BUSY,
	ESLURM_RESERVATION_NOT_USABLE,

	/* switch specific error codes, specific values defined in plugin module */
	ESLURM_SWITCH_MIN = 3000,
	ESLURM_SWITCH_MAX = 3099,
	ESLURM_JOBCOMP_MIN = 3100,
	ESLURM_JOBCOMP_MAX = 3199,
	ESLURM_SCHED_MIN = 3200,
	ESLURM_SCHED_MAX = 3299,
	/* reserved for other plugin specific error codes up to 3999 */

	/* slurmd error codes */
	ESLRUMD_PIPE_ERROR_ON_TASK_SPAWN =		4000,
	ESLURMD_KILL_TASK_FAILED,
	ESLURMD_KILL_JOB_ALREADY_COMPLETE,
	ESLURMD_INVALID_JOB_CREDENTIAL,
	ESLURMD_UID_NOT_FOUND,
	ESLURMD_GID_NOT_FOUND,
	ESLURMD_CREDENTIAL_EXPIRED,
	ESLURMD_CREDENTIAL_REVOKED,
	ESLURMD_CREDENTIAL_REPLAYED,
	ESLURMD_CREATE_BATCH_DIR_ERROR,
	ESLURMD_MODIFY_BATCH_DIR_ERROR,
	ESLURMD_CREATE_BATCH_SCRIPT_ERROR,
	ESLURMD_MODIFY_BATCH_SCRIPT_ERROR,
	ESLURMD_SETUP_ENVIRONMENT_ERROR,
	ESLURMD_SHARED_MEMORY_ERROR,
	ESLURMD_SET_UID_OR_GID_ERROR,
	ESLURMD_SET_SID_ERROR,
	ESLURMD_CANNOT_SPAWN_IO_THREAD,
	ESLURMD_FORK_FAILED,
	ESLURMD_EXECVE_FAILED,
	ESLURMD_IO_ERROR,
	ESLURMD_PROLOG_FAILED,
	ESLURMD_EPILOG_FAILED,
	ESLURMD_SESSION_KILLED,
	ESLURMD_TOOMANYSTEPS,
	ESLURMD_STEP_EXISTS,
	ESLURMD_JOB_NOTRUNNING,
	ESLURMD_STEP_SUSPENDED,
	ESLURMD_STEP_NOTSUSPENDED,

	/* slurmd errors in user batch job */
	ESCRIPT_CHDIR_FAILED =			4100,
	ESCRIPT_OPEN_OUTPUT_FAILED,
	ESCRIPT_NON_ZERO_RETURN,

	/* socket specific SLURM communications error */
	SLURM_PROTOCOL_SOCKET_IMPL_ZERO_RECV_LENGTH =	5000,
	SLURM_PROTOCOL_SOCKET_IMPL_NEGATIVE_RECV_LENGTH,
	SLURM_PROTOCOL_SOCKET_IMPL_NOT_ALL_DATA_SENT,
	ESLURM_PROTOCOL_INCOMPLETE_PACKET ,
	SLURM_PROTOCOL_SOCKET_IMPL_TIMEOUT ,
	SLURM_PROTOCOL_SOCKET_ZERO_BYTES_SENT,

        /* slurm_auth errors */
        ESLURM_AUTH_CRED_INVALID	= 6000,
	ESLURM_AUTH_FOPEN_ERROR,
	ESLURM_AUTH_NET_ERROR,
        ESLURM_AUTH_UNABLE_TO_SIGN
};

/* look up an errno value */
char * slurm_strerror PARAMS((int errnum));

/* set an errno value */
void slurm_seterrno PARAMS((int errnum));

/* get an errno value */
int slurm_get_errno PARAMS((void));

/* print message: error string for current errno value */
void slurm_perror PARAMS((char *msg));

END_C_DECLS

#endif /* !_SLURM_ERRNO_H */

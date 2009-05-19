/*****************************************************************************\
 * src/slurmd/slurmd/slurmd.h - header for slurmd
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
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
#ifndef _SLURMD_H
#define _SLURMD_H

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

#include <pthread.h>
#include <sys/types.h>

#include "src/common/log.h"
#include "src/common/list.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_cred.h"

#ifndef __USE_XOPEN_EXTENDED
extern pid_t getsid(pid_t pid);		/* missing from <unistd.h> */
extern pid_t getpgid(pid_t pid);
#endif

extern int devnull;

/*
 * Global config type
 */
typedef struct slurmd_config {
	char         *prog;		/* Program basename		   */
	char         ***argv;           /* pointer to argument vector      */
	int          *argc;             /* pointer to argument count       */
	char         *hostname;	 	/* local hostname		   */
	uint16_t     cpus;              /* lowest-level logical processors */
	uint16_t     sockets;           /* sockets count                   */
	uint16_t     cores;             /* core count                      */
	uint16_t     threads;           /* thread per core count           */
	uint16_t     conf_cpus;         /* conf file logical processors    */
	uint16_t     conf_sockets;      /* conf file sockets count         */
	uint16_t     conf_cores;        /* conf file core count            */
	uint16_t     conf_threads;      /* conf file thread per core count */
	uint16_t     actual_cpus;       /* actual logical processors       */
	uint16_t     actual_sockets;    /* actual sockets count            */
	uint16_t     actual_cores;      /* actual core count               */
	uint16_t     actual_threads;    /* actual thread per core count    */
	uint32_t     real_memory_size;  /* amount of real memory	   */
	uint32_t     tmp_disk_space;    /* size of temporary disk	   */
	uint32_t     up_time;		/* seconds since last boot time    */
	uint16_t     block_map_size;	/* size of block map               */
	uint16_t     *block_map;	/* abstract->machine block map     */
	uint16_t     *block_map_inv;	/* machine->abstract (inverse) map */
	uint16_t      cr_type;           /* Consumable Resource Type:       *
					 * CR_SOCKET, CR_CORE, CR_MEMORY,  *
					 * CR_DEFAULT, etc.                */
	char         *node_name;	/* node name                       */
	char         *node_addr;	/* node's address                  */
	char         *conffile;		/* config filename                 */
	char         *logfile;		/* slurmd logfile, if any          */
	char         *spooldir;		/* SlurmdSpoolDir	           */
	char         *pidfile;		/* PidFile location		   */
	char         *health_check_program;	/* run on RPC request      */
	char         *tmpfs;		/* directory of tmp FS             */
	char         *pubkey;		/* location of job cred public key */
	char         *epilog;		/* Path to Epilog script	   */
	char         *prolog;		/* Path to prolog script           */
	char         *stepd_loc;	/* Non-standard slurmstepd path    */
	char         *task_prolog;	/* per-task prolog script          */
	char         *task_epilog;	/* per-task epilog script          */
	int           port;	        /* local slurmd port               */
	slurm_fd      lfd;		/* slurmd listen file descriptor   */
	pid_t         pid;		/* server pid                      */
	log_options_t log_opts;         /* current logging options         */
	int           debug_level;	/* logging detail level            */ 
	int           daemonize:1;	/* daemonize flag	           */ 
	int	      cleanstart:1;     /* clean start requested (-c)      */
	int           mlock_pages:1;	/* mlock() slurmd  */

	slurm_cred_ctx_t vctx;          /* slurm_cred_t verifier context   */

	uid_t           slurm_user_id;	/* UID that slurmctld runs as      */
	pthread_mutex_t config_mutex;	/* lock for slurmd_config access   */
	uint16_t        job_acct_gather_freq;
	uint16_t	use_pam;
	uint16_t	task_plugin_param; /* TaskPluginParams, expressed
					 * using cpu_bind_type_t flags */
	uint16_t	propagate_prio;	/* PropagatePrioProcess flag       */
} slurmd_conf_t;

extern slurmd_conf_t * conf;

/* Send node registration message with status to controller
 * IN status - same values slurm error codes (for node shutdown)
 * IN startup - non-zero if slurmd just restarted
 */
int send_registration_msg(uint32_t status, bool startup);

/*
 * save_cred_state - save the current credential list to a file
 * IN list - list of credentials
 * RET int - zero or error code
 */
int save_cred_state(slurm_cred_ctx_t vctx);


#endif /* !_SLURMD_H */

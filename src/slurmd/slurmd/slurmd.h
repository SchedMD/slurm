/*****************************************************************************\
 * src/slurmd/slurmd/slurmd.h - header for slurmd
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  UCRL-CODE-217948.
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


/*
 * Global config type
 */
typedef struct slurmd_config {

	slurm_ctl_conf_t cf;            /* slurm.conf configuration        */

	char         *prog;		/* Program basename		   */
	char         ***argv;           /* pointer to argument vector      */
	int 	     *argc;             /* pointer to argument count       */
	char         *hostname;		/* local hostname		   */
        char         *node_name;        /* node name                       */
	char         *conffile;		/* config filename                 */
	char         *logfile;		/* slurmd logfile, if any          */
	char         *spooldir;		/* SlurmdSpoolDir	           */
	char         *pidfile;		/* PidFile location		   */

	char         *tmpfs;		/* directory of tmp FS             */
	char         *pubkey;		/* location of job cred public key */
	char         *epilog;		/* Path to Epilog script	   */
	char         *prolog;		/* Path to prolog script           */
	char         *task_prolog;	/* per-task prolog script          */
	char         *task_epilog;	/* per-task epilog script          */
	int           port;	        /* local slurmd port               */
	int           hbeat;		/* heartbeat interval		   */
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
} slurmd_conf_t;

slurmd_conf_t * conf;

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

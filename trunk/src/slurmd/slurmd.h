/*****************************************************************************\
 * src/slurmd/slurmd.h - header for slurmd
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
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
#ifndef _SLURMD_H
#define _SLURMD_H

#if HAVE_CONFIG_H
#  include <config.h>
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

#include <src/common/log.h>

/*
 * Global config type
 */
typedef slurm_ssl_key_ctx_t slurm_ssl_ctx;
typedef struct slurmd_config {
	char         *prog;		/* Program basename		   */
	char         *hostname;		/* local hostname		   */
	char         *conffile;		/* config filename                 */
	char         *logfile;		/* slurmd logfile, if any          */
	char         *savedir;		/* SaveStateLocation	           */
	char         *nodename;		/* this node's hostname            */
	char         *tmpfs;		/* directory of tmp FS             */
	char         *pubkey;		/* location of job cred public key */
	char         *epilog;		/* Path to Epilog script	   */
	char         *prolog;		/* Path to prolog script           */
	int           port;	        /* local slurmd port               */
	int           hbeat;		/* heartbeat interval		   */
	slurm_fd      lfd;		/* slurmd listen file descriptor   */
	pid_t         pid;		/* server pid                      */
	log_options_t log_opts;         /* current logging options         */
	int           daemonize:1;	/* daemonize flag	           */ 

	List          cred_state_list;  /* credential stat list            */
	List          threads;		/* list of active threads	   */
	slurm_ssl_ctx vctx;		/* ssl context for cred utils      */
} slurmd_conf_t;

slurmd_conf_t * conf;

/* Send node registration message to controller
 */
int send_registration_msg(void);

#endif /* !_SLURMD_H */

/*****************************************************************************\
 *  agent.h - data structures and function definitions for parallel 
 *	background communications
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette@llnl.gov>, et. al.
 *  Derived from dsh written by Jim Garlick <garlick1@llnl.gov>
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

#ifndef _AGENT_H
#define _AGENT_H

#include "src/slurmctld/agent.h"
#include "src/slurmctld/slurmctld.h"

#define AGENT_IS_THREAD  	1	/* set if agent itself a thread of 
					 * slurmctld, 0 for function call */
#define AGENT_THREAD_COUNT	10	/* maximum active agent threads */
#define COMMAND_TIMEOUT 	5	/* seconds */

typedef struct agent_arg {
	uint32_t	node_count;	/* number of nodes to communicate 
					 * with */
	uint16_t	retry;		/* if set, keep trying */
	slurm_addr	*slurm_addr;	/* array of network addresses */
	char		*node_names;	/* array with MAX_NAME_LEN bytes
					 * per node */
	slurm_msg_type_t msg_type;	/* RPC to be issued */
	void		*msg_args;	/* RPC data to be transmitted */
} agent_arg_t;

/*
 * agent - party responsible for transmitting an common RPC in parallel 
 *	across a set of nodes
 * IN pointer to agent_arg_t, which is xfree'd (including slurm_addr, 
 *	node_names and msg_args) upon completion if AGENT_IS_THREAD is set
 * RET always NULL (function format just for use as pthread)
 */
extern void *agent (void *args);

/*
 * agent_retry - Agent for retrying pending RPCs (top one on the queue), 
 * IN args - unused
 * RET always NULL (function format just for use as pthread)
 */
extern void *agent_retry (void *args);

/* retry_pending - retry all pending RPCs for the given node name
 * IN node_name - name of a node to executing pending RPCs for */
extern void retry_pending (char *node_name);

/* agent_purge - purge all pending RPC requests */
extern void agent_purge (void);

#endif /* !_AGENT_H */

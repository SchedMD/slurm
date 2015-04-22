/*****************************************************************************\
 *  agent.h - data structures and function definitions for parallel
 *	background communications
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov>, et. al.
 *  Derived from dsh written by Jim Garlick <garlick1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#ifndef _AGENT_H
#define _AGENT_H

#include "src/slurmctld/slurmctld.h"

#define AGENT_THREAD_COUNT	10	/* maximum active threads per agent */
#define COMMAND_TIMEOUT 	30	/* command requeue or error, seconds */
#define MAX_AGENT_CNT		(MAX_SERVER_THREADS / (AGENT_THREAD_COUNT + 2))
					/* maximum simultaneous agents, note
					 *   total thread count is product of
					 *   MAX_AGENT_CNT and
					 *   (AGENT_THREAD_COUNT + 2) */
#define LOTS_OF_AGENTS_CNT 50
#define LOTS_OF_AGENTS ((get_agent_count() <= LOTS_OF_AGENTS_CNT) ? 0 : 1)

typedef struct agent_arg {
	uint32_t	node_count;	/* number of nodes to communicate
					 * with */
	uint16_t	retry;		/* if set, keep trying */
	slurm_addr_t    *addr;          /* if set will send to this
					   addr not hostlist */
	hostlist_t	hostlist;	/* hostlist containing the
					 * nodes we are sending to */
	uint16_t        protocol_version; /* protocol version to use */
	slurm_msg_type_t msg_type;	/* RPC to be issued */
	void		*msg_args;	/* RPC data to be transmitted */
} agent_arg_t;

/*
 * agent - party responsible for transmitting an common RPC in parallel
 *	across a set of nodes. agent_queue_request() if immediate
 *	execution is not essential.
 * IN pointer to agent_arg_t, which is xfree'd (including addr,
 *	hostlist and msg_args) upon completion if AGENT_IS_THREAD is set
 * RET always NULL (function format just for use as pthread)
 */
extern void *agent (void *args);

/*
 * agent_queue_request - put a request on the queue for later execution or
 *	execute now if not too busy
 * IN agent_arg_ptr - the request to enqueue
 */
extern void agent_queue_request(agent_arg_t *agent_arg_ptr);

/*
 * agent_retry - Agent for retrying pending RPCs. One pending request is
 *	issued if it has been pending for at least min_wait seconds
 * IN min_wait - Minimum wait time between re-issue of a pending RPC
 * IN mai_too - Send pending email too, note this performed using a
 *		fork/waitpid, so it can take longer than just creating
 *		a pthread to send RPCs
 * RET count of queued requests remaining
 */
extern int agent_retry (int min_wait, bool mail_too);

/* agent_purge - purge all pending RPC requests */
extern void agent_purge (void);

/* get_agent_count - find out how many active agents we have */
extern int get_agent_count(void);

/*
 * mail_job_info - Send e-mail notice of job state change
 * IN job_ptr - job identification
 * IN state_type - job transition type, see MAIL_JOB in slurm.h
 */
extern void mail_job_info (struct job_record *job_ptr, uint16_t mail_type);

/* Return length of agent's retry_list */
extern int retry_list_size(void);

/* slurmctld_free_batch_job_launch_msg is a variant of
 *	slurm_free_job_launch_msg because all environment variables currently
 *	loaded in one xmalloc buffer (see get_job_env()), which is different
 *	from how slurmd assembles the data from a message
 */
extern void slurmctld_free_batch_job_launch_msg(batch_job_launch_msg_t * msg);

#endif /* !_AGENT_H */

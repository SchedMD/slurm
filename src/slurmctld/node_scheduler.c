/*****************************************************************************\
 *  node_scheduler.c - select and allocated nodes to jobs 
 *	Note: there is a global node table (node_record_table_ptr) 
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette1@llnl.gov>
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <src/common/slurm_errno.h>
#include <src/common/xmalloc.h>
#include <src/slurmctld/agent.h>
#include <src/slurmctld/slurmctld.h>

#define BUF_SIZE 1024

struct node_set {		/* set of nodes with same configuration */
	uint32_t cpus_per_node;
	uint32_t nodes;
	uint32_t weight;
	int feature;
	bitstr_t *my_bitmap;
};

int pick_best_quadrics (bitstr_t *bitmap, bitstr_t *req_bitmap, int req_nodes,
		    int req_cpus, int consecutive);
int pick_best_nodes (struct node_set *node_set_ptr, int node_set_size,
		     bitstr_t **req_bitmap, uint32_t req_cpus, uint32_t req_nodes,
		     int contiguous, int shared, uint32_t max_nodes);
void slurm_revoke_job_cred (struct node_record * node_ptr, revoke_credential_msg_t * revoke_job_cred_ptr);
int valid_features (char *requested, char *available);


/* allocate_nodes - for a given bitmap, change the state of specified nodes to NODE_STATE_ALLOCATED
 * globals: node_record_count - number of nodes in the system
 *	node_record_table_ptr - pointer to global node table
 *	last_node_update - last update time of node table
 */
void 
allocate_nodes (unsigned *bitmap) 
{
	int i;

	last_node_update = time (NULL);

	for (i = 0; i < node_record_count; i++) {
		if (bit_test (bitmap, i) == 0)
			continue;
		node_record_table_ptr[i].node_state = NODE_STATE_ALLOCATED;
		bit_clear (idle_node_bitmap, i);
	}
	return;
}


/*
 * count_cpus - report how many cpus are associated with the identified nodes 
 * input: bitmap - a node bitmap
 * output: returns a cpu count
 * globals: node_record_count - number of nodes configured
 *	node_record_table_ptr - pointer to global node table
 */
int 
count_cpus (unsigned *bitmap) 
{
	int i, sum;

	sum = 0;
 	for (i = 0; i < node_record_count; i++) {
		if (bit_test (bitmap, i) != 1)
			continue;
		sum += node_record_table_ptr[i].cpus;
	}
	return sum;
}


/* deallocate_nodes - for a given job, deallocate its nodes and make their state NODE_STATE_IDLE
 * globals: node_record_count - number of nodes in the system
 *	node_record_table_ptr - pointer to global node table
 */
void 
deallocate_nodes (struct job_record  * job_ptr) 
{
	int i;
	revoke_credential_msg_t *revoke_job_cred;
	agent_arg_t *agent_args;
	pthread_attr_t attr_agent;
	pthread_t thread_agent;
	int buf_rec_size = 0;
	uint16_t no_resp_flag, base_state;

	agent_args = xmalloc (sizeof (agent_arg_t));
	agent_args->msg_type = REQUEST_REVOKE_JOB_CREDENTIAL;
	agent_args->retry = 1;
	revoke_job_cred = xmalloc (sizeof (revoke_credential_msg_t));
	last_node_update = time (NULL);
	revoke_job_cred->job_id = job_ptr->job_id;
	revoke_job_cred->expiration_time = job_ptr->details->credential.expiration_time ;
	memset ( (void *)revoke_job_cred->signature, 0, sizeof (revoke_job_cred->signature));

	for (i = 0; i < node_record_count; i++) {
		if (bit_test (job_ptr->node_bitmap, i) == 0)
			continue;
		if ((agent_args->addr_count+1) > buf_rec_size) {
			buf_rec_size += 32;
			xrealloc ((agent_args->slurm_addr), 
			          (sizeof (struct sockaddr_in) * buf_rec_size));
			xrealloc ((agent_args->node_names), 
			          (MAX_NAME_LEN * buf_rec_size));
		}
		agent_args->slurm_addr[agent_args->addr_count] = 
							node_record_table_ptr[i].slurm_addr;
		strncpy (&agent_args->node_names[MAX_NAME_LEN*agent_args->addr_count],
		         node_record_table_ptr[i].name, MAX_NAME_LEN);
		agent_args->addr_count++;
		base_state = node_record_table_ptr[i].node_state & (~NODE_STATE_NO_RESPOND);
		no_resp_flag = node_record_table_ptr[i].node_state & NODE_STATE_NO_RESPOND;
		if (base_state == NODE_STATE_DRAINING) {
			node_record_table_ptr[i].node_state = NODE_STATE_DRAINED;
			bit_clear (idle_node_bitmap, i);
			bit_clear (up_node_bitmap, i);
		}
		else {
			node_record_table_ptr[i].node_state = NODE_STATE_IDLE | no_resp_flag;
			if (no_resp_flag == 0)
				bit_set (idle_node_bitmap, i);
		}
	}

	agent_args->msg_args = revoke_job_cred;
	debug ("Spawning revoke credential agent");
	if (pthread_attr_init (&attr_agent))
		fatal ("pthread_attr_init error %m");
	if (pthread_attr_setdetachstate (&attr_agent, PTHREAD_CREATE_DETACHED))
		error ("pthread_attr_setdetachstate error %m");
#ifdef PTHREAD_SCOPE_SYSTEM
	if (pthread_attr_setscope (&attr_agent, PTHREAD_SCOPE_SYSTEM))
		error ("pthread_attr_setscope error %m");
#endif
	if (pthread_create (&thread_agent, &attr_agent, agent, (void *)agent_args)) {
		error ("pthread_create error %m");
		sleep (1); /* sleep and try once more */
		if (pthread_create (&thread_agent, &attr_agent, agent, (void *)agent_args))
			fatal ("pthread_create error %m");
	}
	return;
}

/* slurm_revoke_job_cred - send RPC for slurmd to revoke a credential */
void
slurm_revoke_job_cred(struct node_record *node_ptr, 
		      revoke_credential_msg_t *revoke_job_cred_ptr)
{
	int msg_size;
	int rc;
	slurm_fd sockfd;
	slurm_msg_t request_msg;
	slurm_msg_t response_msg;
	return_code_msg_t * slurm_rc_msg;

	/* init message connection for message communication with slurmd */
	if ((sockfd = slurm_open_msg_conn(&node_ptr->slurm_addr)) < 0) {
		error("revoke_job_cred: unable to connect to %s: %m", 
				node_ptr->name);
		return;
	}

	/* send request message */
	request_msg.msg_type = REQUEST_REVOKE_JOB_CREDENTIAL;
	request_msg.data = revoke_job_cred_ptr; 
	if ((rc = slurm_send_node_msg(sockfd, &request_msg)) < 0) {
		error ("revoke_job_cred: unable to send revoke msg to %s: %m", 
				node_ptr->name);
		return;
	}

	/* receive message */
	if ((msg_size = slurm_receive_msg(sockfd, &response_msg)) < 0) {
		error ("revoke_job_cred: error in recv from %s: %m", 
				node_ptr->name);
		return;
	}

	/* shutdown message connection */
	if ((rc = slurm_shutdown_msg_conn(sockfd)) < 0)
		error ("revoke_job_cred/shutdown_msg_conn error for %s", 
				node_ptr->name);
	if (msg_size)
		error ("revoke_job_cred/msg_size error %d for %s", 
				msg_size, node_ptr->name);
	/* XXX: why was this here??? */
	/* return; */

	switch ( response_msg . msg_type )
	{
		case RESPONSE_SLURM_RC:
			slurm_rc_msg = ( return_code_msg_t * ) response_msg . data ;
			rc = slurm_rc_msg->return_code;
			slurm_free_return_code_msg ( slurm_rc_msg );	
			if (rc)
				error ("slurm_revoke_job_cred/rc error %d for %s", rc, node_ptr->name);
			break ;
		default:
				error ("slurm_revoke_job_cred/msg_type error %d for %s",
				       response_msg.msg_type, node_ptr->name);
			break ;
	}
	return;
}

/* 
 * is_key_valid - determine if supplied partition key is valid
 * input: key - a slurm key acquired by user root
 * output: returns 1 if key is valid, 0 otherwise
 * NOTE: this is only a placeholder for a future function
 *	the format of the key is TBD
 */
int 
is_key_valid (void * key) 
{
	if (key)
		return 1;
	return 0;
}


/*
 * match_feature - determine if the desired feature is one of those available
 * input: seek - desired feature
 *        available - comma separated list of features
 * output: returns 1 if found, 0 otherwise
 */
int 
match_feature (char *seek, char *available) 
{
	char *tmp_available, *str_ptr3, *str_ptr4;
	int found;

	if (seek == NULL)
		return 1;	/* nothing to look for */
	if (available == NULL)
		return SLURM_SUCCESS;	/* nothing to find */

	tmp_available = xmalloc (strlen (available) + 1);
	strcpy (tmp_available, available);

	found = 0;
	str_ptr3 = (char *) strtok_r (tmp_available, ",", &str_ptr4);
	while (str_ptr3) {
		if (strcmp (seek, str_ptr3) == 0) {	/* we have a match */
			found = 1;
			break;
		}		
		str_ptr3 = (char *) strtok_r (NULL, ",", &str_ptr4);
	}

	xfree (tmp_available);
	return found;
}


/*
 * pick_best_quadrics - Given a bitmap of nodes to select from (bitmap), a bitmap of 
 *	nodes required by the job (req_bitmap), a count of required node (req_nodes), 
 *	a count of required processors (req_cpus) and a flag indicating if consecutive nodes 
 *	are required (0|1, consecutive), identify the nodes which "best" satify the request.
 * 	"best" is defined as either single set of consecutive nodes satisfying 
 *	the request and leaving the minimum number of unused nodes OR 
 *	the fewest number of consecutive node sets
 * output: bitmap - nodes not required to satisfy the request are cleared, other left set
 *         returns zero on success, EINVAL otherwise
 * globals: node_record_count - count of nodes configured
 *	node_record_table_ptr - pointer to global node table
 * NOTE: bitmap must be a superset of req_nodes at the time that pick_best_quadrics is called
 */
int
pick_best_quadrics (bitstr_t *bitmap, bitstr_t *req_bitmap, int req_nodes,
		int req_cpus, int consecutive) 
{
	int i, index, error_code, sufficient;
	int *consec_nodes;	/* how many nodes we can add from this consecutive set of nodes */
	int *consec_cpus;	/* how many nodes we can add from this consecutive set of nodes */
	int *consec_start;	/* where this consecutive set starts (index) */
	int *consec_end;	/* where this consecutive set ends (index) */
	int *consec_req;	/* are nodes from this set required (in req_bitmap) */
	int consec_index, consec_size;
	int rem_cpus, rem_nodes;	/* remaining resources required */
	int best_fit_nodes, best_fit_cpus, best_fit_req;
	int best_fit_location = 0, best_fit_sufficient;

	if (bitmap == NULL)
		fatal ("pick_best_quadrics: bitmap pointer is NULL\n");

	error_code   = EINVAL;	/* default is no fit */
	consec_index = 0;
	consec_size  = 50;	/* start allocation for 50 sets of consecutive nodes */
	consec_cpus  = xmalloc (sizeof (int) * consec_size);
	consec_nodes = xmalloc (sizeof (int) * consec_size);
	consec_start = xmalloc (sizeof (int) * consec_size);
	consec_end   = xmalloc (sizeof (int) * consec_size);
	consec_req   = xmalloc (sizeof (int) * consec_size);

	/* Build table with information about sets of consecutive nodes */
	consec_cpus[consec_index] = consec_nodes[consec_index] = 0;
	consec_req[consec_index] = -1;	/* no required nodes here by default */
	rem_cpus = req_cpus;
	rem_nodes = req_nodes;
	for (index = 0; index < node_record_count; index++) {
		if (bit_test (bitmap, index)) {
			if (consec_nodes[consec_index] == 0)
				consec_start[consec_index] = index;
			if (slurmctld_conf.fast_schedule) 	/* don't bother checking each node */
				i = node_record_table_ptr[index].config_ptr->cpus;
			else
				i = node_record_table_ptr[index].cpus;
			if (req_bitmap && bit_test (req_bitmap, index)) {
				if (consec_req[consec_index] == -1) 
					/* first required node in set */
					consec_req[consec_index] = index; 
				rem_cpus -= i;
				rem_nodes--;
			}
			else {
				bit_clear (bitmap, index);
				consec_cpus[consec_index] += i;
				consec_nodes[consec_index]++;
			}	
		}
		else if (consec_nodes[consec_index] == 0) {
			consec_req[consec_index] = -1;	
			/* already picked up any required nodes */
			/* re-use this record */
		}
		else {
			consec_end[consec_index] = index - 1;
			if (++consec_index >= consec_size) {
				consec_size *= 2;
				xrealloc (consec_cpus,  sizeof (int) * consec_size);
				xrealloc (consec_nodes, sizeof (int) * consec_size);
				xrealloc (consec_start, sizeof (int) * consec_size);
				xrealloc (consec_end,   sizeof (int) * consec_size);
				xrealloc (consec_req,   sizeof (int) * consec_size);
			}	
			consec_cpus[consec_index] = 0;
			consec_nodes[consec_index] = 0;
			consec_req[consec_index] = -1;
		}		
	}
	if (consec_nodes[consec_index] != 0)
		consec_end[consec_index++] = index - 1;

#ifdef EXTREME_DEBUG
	/* don't compile this, slows things down too much */
	debug3 ("rem_cpus=%d, rem_nodes=%d", rem_cpus, rem_nodes);
	for (i = 0; i < consec_index; i++) {
		if (consec_req[i] != -1)
			debug3 ("start=%s, end=%s, nodes=%d, cpus=%d, req=%s",
				node_record_table_ptr[consec_start[i]].name,
				node_record_table_ptr[consec_end[i]].name,
				consec_nodes[i], consec_cpus[i],
				node_record_table_ptr[consec_req[i]].name);
		else
			debug3 ("start=%s, end=%s, nodes=%d, cpus=%d",
				node_record_table_ptr[consec_start[i]].name,
				node_record_table_ptr[consec_end[i]].name,
				consec_nodes[i], consec_cpus[i]);
	}			
#endif

	/* accumulate nodes from these sets of consecutive nodes until */
	/*   sufficient resources have been accumulated */
	while (consec_index) {
		best_fit_cpus = best_fit_nodes = best_fit_sufficient = 0;
		best_fit_req = -1;	/* first required node, -1 if none */
		for (i = 0; i < consec_index; i++) {
			if (consec_nodes[i] == 0)
				continue;
			sufficient = ((consec_nodes[i] >= rem_nodes)
				      && (consec_cpus[i] >= rem_cpus));
			if ((best_fit_nodes == 0) ||	/* first possibility */
			    ((best_fit_req == -1) && (consec_req[i] != -1)) ||	/* required nodes */
			    (sufficient && (best_fit_sufficient == 0)) ||	/* first large enough */
			    (sufficient && (consec_cpus[i] < best_fit_cpus)) ||	/* less waste option */
			    ((sufficient == 0) && (consec_cpus[i] > best_fit_cpus))) {	/* larger option */
				best_fit_cpus = consec_cpus[i];
				best_fit_nodes = consec_nodes[i];
				best_fit_location = i;
				best_fit_req = consec_req[i];
				best_fit_sufficient = sufficient;
			}	
		}
		if (best_fit_nodes == 0)
			break;
		if (consecutive && ((best_fit_nodes < rem_nodes) || (best_fit_cpus < rem_cpus)))
			break;	/* no hole large enough */
		if (best_fit_req != -1) {	/* work out from required nodes */
			for (i = best_fit_req;
			     i <= consec_end[best_fit_location]; i++) {
				if ((rem_nodes <= 0) && (rem_cpus <= 0))
					break;
				if (bit_test (bitmap, i))
					continue;
				bit_set (bitmap, i);
				rem_nodes--;
				rem_cpus -= node_record_table_ptr[i].cpus;
			}
			for (i = (best_fit_req - 1);
			     i >= consec_start[best_fit_location]; i--) {
				if ((rem_nodes <= 0) && (rem_cpus <= 0))
					break;
				/* if (bit_test(bitmap, i)) continue;  nothing set earlier */
				bit_set (bitmap, i);
				rem_nodes--;
				rem_cpus -= node_record_table_ptr[i].cpus;
			}
		}
		else {
			for (i = consec_start[best_fit_location];
			     i <= consec_end[best_fit_location]; i++) {
				if ((rem_nodes <= 0) && (rem_cpus <= 0))
					break;
				if (bit_test (bitmap, i))
					continue;
				bit_set (bitmap, i);
				rem_nodes--;
				rem_cpus -= node_record_table_ptr[i].cpus;
			}
		}		
		if ((rem_nodes <= 0) && (rem_cpus <= 0)) {
			error_code = 0;
			break;
		}		
		consec_cpus[best_fit_location] = 0;
		consec_nodes[best_fit_location] = 0;
	}			

	if (consec_cpus)
		xfree (consec_cpus);
	if (consec_nodes)
		xfree (consec_nodes);
	if (consec_start)
		xfree (consec_start);
	if (consec_end)
		xfree (consec_end);
	if (consec_req)
		xfree (consec_req);
	return error_code;
}


/*
 * pick_best_nodes - from a weigh order table of all nodes satisfying a job's specifications, 
 *	select the "best" for use
 * input: node_set_ptr - pointer to node specification information
 *        node_set_size - number of entries in records pointed to by node_set_ptr
 *        req_bitmap - pointer to bitmap of specific nodes required by the job, could be NULL
 *        req_cpus - count of cpus required by the job
 *        req_nodes - count of nodes required by the job
 *        contiguous - set to 1 if allocated nodes must be contiguous, 0 otherwise
 *        shared - set to 1 if nodes may be shared, 0 otherwise
 *        max_nodes - maximum number of nodes permitted for job, 
 *		INFIITE for no limit (partition limit)
 * output: req_bitmap - pointer to bitmap of selected nodes
 *         returns 0 on success, EAGAIN if request can not be satisfied now, 
 *		EINVAL if request can never be satisfied (insufficient contiguous nodes)
 * NOTE: the caller must xfree memory pointed to by req_bitmap
 * Notes: The algorithm is
 *	1) If required node list is specified, determine implicitly required processor and node count 
 *	2) Determine how many disjoint required "features" are represented (e.g. "FS1|FS2")
 *	3) For each feature: find matching node table entries, identify nodes that are up and 
 *	   available (idle or shared) and add them to a bit map, call pick_best_quadrics() to 
 *	   select the "best" of those based upon topology
 *	4) If request can't be satified now, execute pick_best_quadrics() against the list 
 *	   of nodes that exist in any state (perhaps down or busy) to determine if the 
 *	   request can every be satified.
 */
int
pick_best_nodes (struct node_set *node_set_ptr, int node_set_size,
		 bitstr_t **req_bitmap, uint32_t req_cpus, uint32_t req_nodes,
		 int contiguous, int shared, uint32_t max_nodes) 
{
	int error_code, i, j, pick_code;
	int total_nodes, total_cpus;	/* total resources configured in partition */
	int avail_nodes, avail_cpus;	/* resources available for use now */
	bitstr_t *avail_bitmap, *total_bitmap;
	int max_feature, min_feature;
	int avail_set, total_set, runable;

	if (node_set_size == 0) {
		info ("pick_best_nodes: empty node set for selection");
		return EINVAL;
	}
	if ((max_nodes != INFINITE) && (req_nodes > max_nodes)) {
		info ("pick_best_nodes: more nodes required than possible in partition");
		return EINVAL;
	}
	error_code = 0;
	avail_bitmap = total_bitmap = NULL;
	avail_nodes = avail_cpus = 0;
	total_nodes = total_cpus = 0;
	if (req_bitmap[0]) {	/* specific nodes required */
		/* NOTE: we have already confirmed that all of these nodes have a usable */
		/*       configuration and are in the proper partition */
		if (req_nodes != 0)
			total_nodes = bit_set_count (req_bitmap[0]);
		if (req_cpus != 0)
			total_cpus = count_cpus (req_bitmap[0]);
		if (total_nodes > max_nodes) {
			info ("pick_best_nodes: more nodes required than possible in partition");
			return EINVAL;
		}
		if ((req_nodes <= total_nodes) && (req_cpus <= total_cpus)) {
			if (bit_super_set (req_bitmap[0], up_node_bitmap) != 1)
				return EAGAIN;
			if ((shared != 1) &&
			    (bit_super_set (req_bitmap[0], idle_node_bitmap) != 1))
				return EAGAIN;
			return SLURM_SUCCESS;	/* user can have selected nodes, we're done! */
		}		
		total_nodes = total_cpus = 0;	/* reinitialize */
	}			

	/* identify how many feature sets we have (e.g. "[fs1|fs2|fs3|fs4]" */
	max_feature = min_feature = node_set_ptr[0].feature;
	for (i = 1; i < node_set_size; i++) {
		if (node_set_ptr[i].feature > max_feature)
			max_feature = node_set_ptr[i].feature;
		if (node_set_ptr[i].feature < min_feature)
			min_feature = node_set_ptr[i].feature;
	}

	runable = 0;	/* assume not runable until otherwise demonstrated */
	for (j = min_feature; j <= max_feature; j++) {
		avail_set = total_set = 0;
		for (i = 0; i < node_set_size; i++) {
			if (node_set_ptr[i].feature != j)
				continue;
			if (runable == 0) {
				if (total_set)
					bit_or (total_bitmap, node_set_ptr[i].my_bitmap);
				else {
					total_bitmap = bit_copy (node_set_ptr[i].my_bitmap);
					if (total_bitmap == NULL) 
						fatal ("bit_copy failed to allocate memory");
					total_set = 1;
				}	
				total_nodes += node_set_ptr[i].nodes;
				total_cpus += (node_set_ptr[i].nodes * node_set_ptr[i].cpus_per_node);
			}	
			bit_and (node_set_ptr[i].my_bitmap, up_node_bitmap);
			if (shared != 1)
				bit_and (node_set_ptr[i].my_bitmap, idle_node_bitmap);
			node_set_ptr[i].nodes = bit_set_count (node_set_ptr[i].my_bitmap);
			if (avail_set)
				bit_or (avail_bitmap, node_set_ptr[i].my_bitmap);
			else {
				avail_bitmap = bit_copy (node_set_ptr[i].my_bitmap);
				if (avail_bitmap == NULL) 
					fatal ("bit_copy memory allocation failure");
				avail_set = 1;
			}	
			avail_nodes += node_set_ptr[i].nodes;
			avail_cpus += (node_set_ptr[i].nodes * node_set_ptr[i].cpus_per_node);
			if ((req_bitmap[0]) && 
			    (bit_super_set (req_bitmap[0], avail_bitmap) == 0))
				continue;
			if (avail_nodes < req_nodes)
				continue;
			if (avail_cpus < req_cpus)
				continue;
			pick_code = pick_best_quadrics (avail_bitmap, req_bitmap[0], req_nodes, req_cpus, contiguous);
			if ((pick_code == 0) && (max_nodes != INFINITE)
			    && (bit_set_count (avail_bitmap) > max_nodes)) {
				info ("pick_best_nodes: too many nodes selected %u partition maximum is %u",
					bit_set_count (avail_bitmap), max_nodes);
				error_code = EINVAL;
				break;
			}	
			if (pick_code == 0) {
				if (total_bitmap)
					bit_free (total_bitmap);
				if (req_bitmap[0])
					bit_free (req_bitmap[0]);
				req_bitmap[0] = avail_bitmap;
				return SLURM_SUCCESS;
			}
		}

		/* determine if job could possibly run (if configured nodes all available) */
		if ((error_code == 0) && (runable == 0) &&
		    (total_nodes >= req_nodes) && (total_cpus >= req_cpus) &&
		    ((req_bitmap[0] == NULL) || (bit_super_set (req_bitmap[0], total_bitmap) == 1)) &&
		    ((max_nodes == INFINITE) || (req_nodes <= max_nodes))) {
			pick_code = pick_best_quadrics (total_bitmap, req_bitmap[0], req_nodes, req_cpus, contiguous);
			if ((pick_code == 0) && (max_nodes != INFINITE)
			    && (bit_set_count (total_bitmap) > max_nodes)) {
				error_code = EINVAL;
				info ("pick_best_nodes: %u nodes selected, max is %u",
					bit_set_count (avail_bitmap), max_nodes);
			}
			if (pick_code == 0)
				runable = 1;
		}		
		if (avail_bitmap)
			bit_free (avail_bitmap);
		if (total_bitmap)
			bit_free (total_bitmap);
		avail_bitmap = total_bitmap = NULL;
		if (error_code != 0)
			break;
	}

	if (runable == 0) {
		error_code = EINVAL;
		info ("pick_best_nodes: job never runnable");
	}
	if (error_code == 0)
		error_code = EAGAIN;
	return error_code;
}


/*
 * select_nodes - select and allocate nodes to a specific job
 * input: job_ptr - pointer to the job record
 *	test_only - do not allocate nodes, just confirm they could be allocated now
 * output: returns 0 on success, ESLURM code from slurm_errno.h otherwise)
 * globals: list_part - global list of partition info
 *	default_part_loc - pointer to default partition 
 *	config_list - global list of node configuration info
 * Notes: The algorithm is
 *	1) Build a table (node_set_ptr) of nodes with the requisite configuration
 *	   Each table entry includes their weight, node_list, features, etc.
 *	2) Call pick_best_nodes() to select those nodes best satisfying the request, 
 *	   (e.g. best-fit or other criterion)
 *	3) Call allocate_nodes() to perform the actual allocation
 */
int
select_nodes (struct job_record *job_ptr, int test_only) 
{
	int error_code, i, node_set_index, node_set_size = 0;
	bitstr_t *req_bitmap, *scratch_bitmap;
	ListIterator config_record_iterator;
	struct config_record *config_record_point;
	struct node_set *node_set_ptr;
	struct part_record *part_ptr;
	int tmp_feature, check_node_config;

	error_code = SLURM_SUCCESS;
	req_bitmap = scratch_bitmap = NULL;
	config_record_iterator = (ListIterator) NULL;
	node_set_ptr = NULL;
	part_ptr = NULL;

	if (job_ptr == NULL)
		fatal("select_nodes: NULL job pointer value");
	if (job_ptr->magic != JOB_MAGIC)
		fatal("select_nodes: bad job pointer value");

	/* pick up nodes from the weight ordered configuration list */
	if (job_ptr->details->req_node_bitmap)	/* insure selected nodes in partition */
		req_bitmap = bit_copy (job_ptr->details->req_node_bitmap);
	part_ptr = find_part_record(job_ptr->partition);
	if (part_ptr == NULL)
		fatal("select_nodes: invalid partition name %s for job %u", 
			job_ptr->partition, job_ptr->job_id);
	node_set_index = 0;
	node_set_size = 0;
	node_set_ptr = (struct node_set *) xmalloc (sizeof (struct node_set));
	node_set_ptr[node_set_size++].my_bitmap = NULL;

	config_record_iterator = list_iterator_create (config_list);
	if (config_record_iterator == NULL)
		fatal ("select_nodes: ListIterator_create unable to allocate memory");

	while ((config_record_point =
	        (struct config_record *) list_next (config_record_iterator))) {

		tmp_feature = valid_features (job_ptr->details->features,
					config_record_point->feature);
		if (tmp_feature == 0)
			continue;

		/* since nodes can register with more resources than defined */
		/* in the configuration, we want to use those higher values */
		/* for scheduling, but only as needed */
		if (slurmctld_conf.fast_schedule) 	/* don't bother checking each node */
			check_node_config = 0;
		else if ((job_ptr->details->min_procs > config_record_point->cpus) ||
		    (job_ptr->details->min_memory > config_record_point->real_memory) ||
		    (job_ptr->details->min_tmp_disk > config_record_point->tmp_disk)) {
			check_node_config = 1;
		}
		else
			check_node_config = 0;

		node_set_ptr[node_set_index].my_bitmap =
			bit_copy (config_record_point->node_bitmap);
		if (node_set_ptr[node_set_index].my_bitmap == NULL)
			fatal ("bit_copy memory allocation failure");
		bit_and (node_set_ptr[node_set_index].my_bitmap,
			    part_ptr->node_bitmap);
		node_set_ptr[node_set_index].nodes =
			bit_set_count (node_set_ptr[node_set_index].my_bitmap);

		/* check configuration of individual nodes only if the check */
		/* of baseline values in the configuration file are too low. */
		/* this will slow the scheduling for very large cluster. */
		if (check_node_config && (node_set_ptr[node_set_index].nodes != 0)) {
			for (i = 0; i < node_record_count; i++) {
				if (bit_test
				    (node_set_ptr[node_set_index].my_bitmap, i) == 0)
					continue;
				if ((job_ptr->details->min_procs <= 
					node_record_table_ptr[i].cpus)
				    && (job_ptr->details->min_memory <=
					node_record_table_ptr[i].real_memory)
				    && (job_ptr->details->min_tmp_disk <=
					node_record_table_ptr[i].tmp_disk))
					continue;
				bit_clear (node_set_ptr[node_set_index].my_bitmap, i);
				if ((--node_set_ptr[node_set_index].nodes) == 0)
					break;
			}
		}		
		if (node_set_ptr[node_set_index].nodes == 0) {
			bit_free (node_set_ptr[node_set_index].my_bitmap);
			node_set_ptr[node_set_index].my_bitmap = NULL;
			continue;
		}		
		if (req_bitmap) {
			if (scratch_bitmap)
				bit_or (scratch_bitmap,
					   node_set_ptr[node_set_index].my_bitmap);
			else {
				scratch_bitmap =
					bit_copy (node_set_ptr[node_set_index].my_bitmap);
				if (scratch_bitmap == NULL)
					fatal ("bit_copy memory allocation failure");
			}	
		}		
		node_set_ptr[node_set_index].cpus_per_node = config_record_point->cpus;
		node_set_ptr[node_set_index].weight = config_record_point->weight;
		node_set_ptr[node_set_index].feature = tmp_feature;
		debug ("found %d usable nodes from configuration containing nodes %s",
			node_set_ptr[node_set_index].nodes,
			config_record_point->nodes);

		node_set_index++;
		xrealloc (node_set_ptr, sizeof (struct node_set) * (node_set_index + 1));
		node_set_ptr[node_set_size++].my_bitmap = NULL;
	}			
	if (node_set_index == 0) {
		info ("select_nodes: no node configurations satisfy requirements procs=%u:mem=%u:disk=%u:feature=%s",
			job_ptr->details->min_procs, job_ptr->details->min_memory, 
			job_ptr->details->min_tmp_disk, job_ptr->details->features);
		error_code = ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
		goto cleanup;
	}
	/* eliminate last (incomplete) node_set record */	
	if (node_set_ptr[node_set_index].my_bitmap)
		bit_free (node_set_ptr[node_set_index].my_bitmap);
	node_set_ptr[node_set_index].my_bitmap = NULL;
	node_set_size = node_set_index;

	if (req_bitmap) {
		if ((scratch_bitmap == NULL)
		    || (bit_super_set (req_bitmap, scratch_bitmap) != 1)) {
			info ("select_nodes: requested nodes do not satisfy configurations requirements procs=%u:mem=%u:disk=%u:feature=%s",
			    job_ptr->details->min_procs, job_ptr->details->min_memory, 
			    job_ptr->details->min_tmp_disk, job_ptr->details->features);
			error_code = ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
			goto cleanup;
		}		
	}			


	/* pick the nodes providing a best-fit */
	error_code = pick_best_nodes (node_set_ptr, node_set_size,
				      &req_bitmap, job_ptr->details->num_procs, 
				      job_ptr->details->num_nodes,
				      job_ptr->details->contiguous, 
				      job_ptr->details->shared,
				      part_ptr->max_nodes);
	if (error_code == EAGAIN) {
		error_code = ESLURM_NODES_BUSY;
		goto cleanup;
	}
	if (error_code == EINVAL) {
		error_code = ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
		info ("select_nodes: no nodes can satisfy job request");
		goto cleanup;
	}		
	if (test_only) {
		error_code = SLURM_SUCCESS;
		goto cleanup;
	}	

	/* assign the nodes and stage_in the job */
	job_ptr->nodes = bitmap2node_name (req_bitmap);
	build_node_details (req_bitmap, 
		&(job_ptr->num_cpu_groups),
		&(job_ptr->cpus_per_node),
		&(job_ptr->cpu_count_reps));
	allocate_nodes (req_bitmap);
	job_ptr->node_bitmap = req_bitmap;
	req_bitmap = NULL;
	job_ptr->job_state = JOB_STAGE_IN;
	job_ptr->start_time = time(NULL);
	if (job_ptr->time_limit == INFINITE)
		job_ptr->end_time = INFINITE;
	else
		job_ptr->end_time = job_ptr->start_time + (job_ptr->time_limit * 60);

      cleanup:
	if (req_bitmap)
		bit_free (req_bitmap);
	if (scratch_bitmap)
		bit_free (scratch_bitmap);
	if (node_set_ptr) {
		for (i = 0; i < node_set_size; i++) {
			if (node_set_ptr[i].my_bitmap)
				bit_free (node_set_ptr[i].my_bitmap);
		}
		xfree (node_set_ptr);
	}			
	if (config_record_iterator)
		list_iterator_destroy (config_record_iterator);
	return error_code;
}


/*
 * build_node_details - given a bitmap, report the number of cpus per node and their distribution
 * input: bitstr_t *node_bitmap - the map of nodes
 * output: num_cpu_groups - element count in arrays cpus_per_node and cpu_count_reps
 *	cpus_per_node - array of cpus per node allocated
 *	cpu_count_reps - array of consecutive nodes with same cpu count
 * NOTE: the arrays cpus_per_node and cpu_count_reps must be xfreed by the caller
 */
void 
build_node_details (bitstr_t *node_bitmap, 
		uint16_t * num_cpu_groups, uint32_t ** cpus_per_node, uint32_t **cpu_count_reps)
{
	int array_size, array_pos, i;
	int first_bit, last_bit;

	*num_cpu_groups = 0;
	if (node_bitmap == NULL) 
		return;

	first_bit = bit_ffs(node_bitmap);
	last_bit  = bit_fls(node_bitmap);
	array_pos = -1;

	/* assume relatively homogeneous array for array allocations */
	/* we can grow or shrink the arrays as needed */
	array_size = (last_bit - first_bit) / 100 + 2;
	cpus_per_node[0]  = xmalloc (sizeof(uint32_t *) * array_size);
	cpu_count_reps[0] = xmalloc (sizeof(uint32_t *) * array_size);

	for (i = first_bit; i <= last_bit; i++) {
		if (bit_test (node_bitmap, i) != 1)
			continue;
		if ((array_pos == -1) ||
		    (cpus_per_node[0][array_pos] != node_record_table_ptr[i].cpus)) {
			array_pos++;
			if (array_pos >= array_size) { /* grow arrays */
				array_size *= 2;
				xrealloc (cpus_per_node[0],  (sizeof(uint32_t *) * array_size));
				xrealloc (cpu_count_reps[0], (sizeof(uint32_t *) * array_size));
			}
			cpus_per_node [0][array_pos] = node_record_table_ptr[i].cpus;
			cpu_count_reps[0][array_pos] = 1;
		}
		else {
			cpu_count_reps[0][array_pos]++;
		}
	}
	array_size = array_pos + 1;
	*num_cpu_groups = array_size;
	xrealloc (cpus_per_node[0],  (sizeof(uint32_t *) * array_size));
	xrealloc (cpu_count_reps[0], (sizeof(uint32_t *) * array_size));
}

/*
 * valid_features - determine if the requested features are satisfied by those available
 * input: requested - requested features (by a job)
 *        available - available features (on a node)
 * output: returns 0 if request is not satisfied, otherwise an integer indicating 
 *		which mutually exclusive feature is satisfied. for example
 *		valid_features("[fs1|fs2|fs3|fs4]", "fs3") returns 3. see the 
 *		slurm administrator and user guides for details. returns 1 if 
 *		requirements are satisfied without mutually exclusive feature list.
 */
int
valid_features (char *requested, char *available) 
{
	char *tmp_requested, *str_ptr1;
	int bracket, found, i, option, position, result;
	int last_op;		/* last operation 0 for or, 1 for and */
	int save_op = 0, save_result = 0;	/* for bracket support */

	if (requested == NULL)
		return 1;	/* no constraints */
	if (available == NULL)
		return 0;	/* no features */

	tmp_requested = xmalloc (strlen (requested) + 1);
	strcpy (tmp_requested, requested);

	bracket = option = position = 0;
	str_ptr1 = tmp_requested;	/* start of feature name */
	result = last_op = 1;	/* assume good for now */
	for (i = 0;; i++) {
		if (tmp_requested[i] == (char) NULL) {
			if (strlen (str_ptr1) == 0)
				break;
			found = match_feature (str_ptr1, available);
			if (last_op == 1)	/* and */
				result &= found;
			else			/* or */
				result |= found;
			break;
		}		

		if (tmp_requested[i] == '&') {
			if (bracket != 0) {
				info ("valid_features: parsing failure 1 on %s", requested);
				result = 0;
				break;
			}	
			tmp_requested[i] = (char) NULL;
			found = match_feature (str_ptr1, available);
			if (last_op == 1)	/* and */
				result &= found;
			else	/* or */
				result |= found;
			str_ptr1 = &tmp_requested[i + 1];
			last_op = 1;	/* and */

		}
		else if (tmp_requested[i] == '|') {
			tmp_requested[i] = (char) NULL;
			found = match_feature (str_ptr1, available);
			if (bracket != 0) {
				if (found)
					option = position;
				position++;
			}
			if (last_op == 1)	/* and */
				result &= found;
			else	/* or */
				result |= found;
			str_ptr1 = &tmp_requested[i + 1];
			last_op = 0;	/* or */

		}
		else if (tmp_requested[i] == '[') {
			bracket++;
			position = 1;
			save_op = last_op;
			save_result = result;
			last_op = result = 1;
			str_ptr1 = &tmp_requested[i + 1];

		}
		else if (tmp_requested[i] == ']') {
			tmp_requested[i] = (char) NULL;
			found = match_feature (str_ptr1, available);
			if (found)
				option = position;
			result |= found;
			if (save_op == 1)	/* and */
				result &= save_result;
			else	/* or */
				result |= save_result;
			if ((tmp_requested[i + 1] == '&') && (bracket == 1)) {
				last_op = 1;
				str_ptr1 = &tmp_requested[i + 2];
			}
			else if ((tmp_requested[i + 1] == '|')
				 && (bracket == 1)) {
				last_op = 0;
				str_ptr1 = &tmp_requested[i + 2];
			}
			else if ((tmp_requested[i + 1] == (char) NULL)
				 && (bracket == 1)) {
				break;
			}
			else {
				error ("valid_features: parsing failure 2 on %s",
					 requested);
				result = 0;
				break;
			}	
			bracket = 0;
		}		
	}

	if (position)
		result *= option;
	xfree (tmp_requested);
	return result;
}

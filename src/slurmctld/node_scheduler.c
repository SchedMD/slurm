/* 
 * node_scheduler.c - select and allocated nodes to jobs 
 * see slurm.h for documentation on external functions and data structures
 *
 * NOTE: DEBUG_MODULE mode test with execution line
 *	node_scheduler ../../etc/slurm.conf2 ../../etc/slurm.jobs
 *
 * author: moe jette, jette@llnl.gov
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "slurm.h"

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
int valid_features (char *requested, char *available);

#if DEBUG_MODULE
/* main is used here for testing purposes only */
int 
main (int argc, char *argv[]) 
{
	int error_code, error_count = 0, line_num, i;
	FILE *command_file;
	char in_line[BUF_SIZE], *job_id, *node_list;
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	clock_t start_time;

	log_init(argv[0], opts, SYSLOG_FACILITY_DAEMON, NULL);

	if (argc < 3) {
		printf ("usage: %s <SLURM_CONF_file> <slurm_job_file>\n",
			argv[0]);
		exit (1);
	}			

	error_code = init_slurm_conf ();
	if (error_code) {
		printf ("controller: error %d from init_slurm_conf\n",
			error_code);
		error_count++;
		exit (error_count);
	}			

	error_code = read_slurm_conf (argv[1]);
	if (error_code) {
		printf ("controller: error %d from read_slurm_conf\n",
			error_code);
		error_count++;
		exit (error_count);
	}			

	/* mark everything up and idle for testing */
	bit_nset (idle_node_bitmap, 0, node_record_count-1);
	bit_nset (up_node_bitmap,   0, node_record_count-1);

	command_file = fopen (argv[2], "r");
	if (command_file == NULL) {
		fprintf (stderr,
			 "node_scheduler: error %d opening command file %s\n",
			 errno, argv[2]);
		error_count++;
		exit (error_count);
	}			

	i = valid_features ("fs1&fs2", "fs1");
	if (i != 0) {
		printf ("valid_features error 1\n");
		error_count++;
	}
	i = valid_features ("fs1|fs2", "fs1");
	if (i != 1) {
		printf ("valid_features error 2\n");
		error_count++;
	}
	i = valid_features ("fs1|fs2&fs3", "fs1,fs3");
	if (i != 1) {
		printf ("valid_features error 3\n");
		error_count++;
	}
	i = valid_features ("[fs1|fs2]&fs3", "fs2,fs3");
	if (i != 2) {
		printf ("valid_features error 4\n");
		error_count++;
	}
	i = valid_features ("fs0&[fs1|fs2]&fs3", "fs2,fs3");
	if (i != 0) {
		printf ("valid_features error 5\n");
		error_count++;
	}
	i = valid_features ("fs3&[fs1|fs2]&fs3", "fs2,fs3");
	if (i != 2) {
		printf ("valid_features error 6\n");
		error_count++;
	}

	line_num = 0;
	printf ("\n");
	while (fgets (in_line, BUF_SIZE, command_file)) {
		start_time = clock ();
		job_id = node_list = NULL;
		if (in_line[strlen (in_line) - 1] == '\n')
			in_line[strlen (in_line) - 1] = (char) NULL;
		line_num++;
		error_code = job_allocate(in_line, &job_id, &node_list);
		if (error_code) {
			if (strncmp (in_line, "JobName=FAIL", 12) != 0) {
				printf ("ERROR:");
				error_count++;
			}
			printf ("for job: %s\n", in_line);
			printf ("node_scheduler: error %d from job_allocate on line %d\n", 
				error_code, line_num);
		}
		else {
			if (strncmp (in_line, "JobName=FAIL", 12) == 0) {
				printf ("ERROR: ");
				error_count++;
			}
			printf ("for job: %s\n  nodes selected %s\n",
				in_line, node_list);
			if (job_id)
				xfree (job_id);
			if (node_list)
				xfree (node_list);
		}
		printf("time = %ld usec\n\n", (long) (clock() - start_time));
	}
	exit (error_count);		
}
#endif


/* allocate_nodes - for a given bitmap, change the state of specified nodes to stage_in
 * this is a simple prototype for testing 
 * globals: node_record_count - number of nodes in the system
 *	node_record_table_ptr - pointer to global node table
 */
void 
allocate_nodes (unsigned *bitmap) 
{
	int i;

	for (i = 0; i < node_record_count; i++) {
		if (bit_test (bitmap, i) == 0)
			continue;
		node_record_table_ptr[i].node_state = STATE_BUSY;
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


/* deallocate_nodes - for a given bitmap, change the state of specified nodes to idle
 * this is a simple prototype for testing 
 * globals: node_record_count - number of nodes in the system
 *	node_record_table_ptr - pointer to global node table
 */
void 
deallocate_nodes (unsigned *bitmap) 
{
	int i;

	for (i = 0; i < node_record_count; i++) {
		if (bit_test (bitmap, i) == 0)
			continue;
		node_record_table_ptr[i].node_state = STATE_IDLE;
		bit_set (idle_node_bitmap, i);
	}
	return;
}


/* 
 * is_key_valid - determine if supplied key is valid
 * input: key - a slurm key acquired by user root
 * output: returns 1 if key is valid, 0 otherwise
 * NOTE: this is only a placeholder for a future function
 */
int 
is_key_valid (int key) 
{
	if (key == NO_VAL)
		return 0;
	return 1;
}


/*
 * match_feature - determine if the desired feature (seek) is one of those available
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
		return 0;	/* nothing to find */

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
 * match_group - determine if the user is a member of any groups permitted to use this partition
 * input: allow_groups - comma delimited list of groups permitted to use the partition, 
 *			NULL is for all groups
 *        user_groups - comma delimited list of groups the user belongs to
 * output: returns 1 if user is member, 0 otherwise
 */
int
match_group (char *allow_groups, char *user_groups) 
{
	char *tmp_allow_group, *str_ptr1, *str_ptr2;
	char *tmp_user_group, *str_ptr3, *str_ptr4;

	if ((allow_groups == NULL) ||	/* anybody can use it */
	    (strcmp (allow_groups, "all") == 0))
		return 1;
	if (user_groups == NULL)
		return 0;	/* empty group list */

	tmp_allow_group = xmalloc (strlen (allow_groups) + 1);
	strcpy (tmp_allow_group, allow_groups);

	tmp_user_group = xmalloc (strlen (user_groups) + 1);

	str_ptr1 = (char *) strtok_r (tmp_allow_group, ",", &str_ptr2);
	while (str_ptr1) {
		strcpy (tmp_user_group, user_groups);
		str_ptr3 = (char *) strtok_r (tmp_user_group, ",", &str_ptr4);
		while (str_ptr3) {
			if (strcmp (str_ptr1, str_ptr3) == 0) {	/* we have a match */
				xfree (tmp_allow_group);
				xfree (tmp_user_group);
				return 1;
			}	
			str_ptr3 = (char *) strtok_r (NULL, ",", &str_ptr4);
		}	
		str_ptr1 = (char *) strtok_r (NULL, ",", &str_ptr2);
	}		
	xfree (tmp_allow_group);
	xfree (tmp_user_group);
	return 0;		/* no match */
}


/*
 * pick_best_quadrics - identify the nodes which best fit the req_nodes and 
 *	req_cpus counts for a system with Quadrics elan interconnect.
 * 	"best" is defined as either single set of consecutive nodes satisfying 
 *	the request and leaving the minimum number of unused nodes OR 
 *	the fewest number of consecutive node sets
 * input: bitmap - the bit map to search
 *        req_bitmap - the bit map of nodes that must be selected, if not NULL these 
 *                     have already been confirmed to be in the input bitmap
 *        req_nodes - number of nodes required
 *        req_cpus - number of cpus required
 *        consecutive - nodes must be consecutive is 1, otherwise 0
 * output: bitmap - nodes not required to satisfy the request are cleared, 
 *		other left set
 *         returns zero on success, EINVAL otherwise
 * globals: node_record_count - count of nodes configured
 *	node_record_table_ptr - pointer to global node table
 * NOTE: bitmap must be a superset of req_nodes at function call time
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

	consec_cpus[consec_index] = consec_nodes[consec_index] = 0;
	consec_req[consec_index] = -1;	/* no required nodes here by default */
	rem_cpus = req_cpus;
	rem_nodes = req_nodes;
	for (index = 0; index < node_record_count; index++) {
		if (bit_test (bitmap, index)) {
			if (consec_nodes[consec_index] == 0)
				consec_start[consec_index] = index;
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

#if DEBUG_SYSTEM > 1
	info ("rem_cpus=%d, rem_nodes=%d", rem_cpus, rem_nodes);
	for (i = 0; i < consec_index; i++) {
		if (consec_req[i] != -1)
			info ("start=%s, end=%s, nodes=%d, cpus=%d, req=%s",
				node_record_table_ptr[consec_start[i]].name,
				node_record_table_ptr[consec_end[i]].name,
				consec_nodes[i], consec_cpus[i],
				node_record_table_ptr[consec_req[i]].name);
		else
			info ("start=%s, end=%s, nodes=%d, cpus=%d",
				node_record_table_ptr[consec_start[i]].name,
				node_record_table_ptr[consec_end[i]].name,
				consec_nodes[i], consec_cpus[i]);
	}			
#endif


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
 * pick_best_nodes - from nodes satisfying partition and configuration specifications, 
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
			return 0;	/* user can have selected nodes, we're done! */
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
					bit_or (total_bitmap,
						   node_set_ptr[i].my_bitmap);
				else {
					total_bitmap = bit_copy (node_set_ptr[i].my_bitmap);
					if (total_bitmap == NULL) 
						fatal ("bit_copy failed to allocate memory");
					total_set = 1;
				}	
				total_nodes += node_set_ptr[i].nodes;
				total_cpus +=
					(node_set_ptr[i].nodes * node_set_ptr[i].cpus_per_node);
			}	
			bit_and (node_set_ptr[i].my_bitmap,
				    up_node_bitmap);
			if (shared != 1)
				bit_and (node_set_ptr[i].my_bitmap,
					    idle_node_bitmap);
			node_set_ptr[i].nodes =
				bit_set_count (node_set_ptr[i].my_bitmap);
			if (avail_set)
				bit_or (avail_bitmap,
					   node_set_ptr[i].my_bitmap);
			else {
				avail_bitmap = bit_copy (node_set_ptr[i].my_bitmap);
				if (avail_bitmap == NULL) 
					fatal ("bit_copy memory allocation failure");
				avail_set = 1;
			}	
			avail_nodes += node_set_ptr[i].nodes;
			avail_cpus +=
				(node_set_ptr[i].nodes *
				 node_set_ptr[i].cpus_per_node);
			if ((req_bitmap[0]) && 
			    (bit_super_set (req_bitmap[0], avail_bitmap) == 0))
				continue;
			if (avail_nodes < req_nodes)
				continue;
			if (avail_cpus < req_cpus)
				continue;
			pick_code =
				pick_best_quadrics (avail_bitmap, req_bitmap[0],
						req_nodes, req_cpus,
						contiguous);
			if ((pick_code == 0) && (max_nodes != INFINITE)
			    && (bit_set_count (avail_bitmap) > max_nodes)) {
				info ("pick_best_nodes: too many nodes selected %u of %u",
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
				return 0;
			}
		}
		if ((error_code == 0) && (runable == 0) &&
		    (total_nodes > req_nodes) && (total_cpus > req_cpus) &&
		    ((req_bitmap[0] == NULL)
		     || (bit_super_set (req_bitmap[0], total_bitmap) == 1))
		    && ((max_nodes == INFINITE) || (req_nodes <= max_nodes))) {
			/* determine if job could possibly run */
			/* (if configured nodes all available) */
			pick_code =
				pick_best_quadrics (total_bitmap, req_bitmap[0],
						req_nodes, req_cpus,
						contiguous);
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
 * output: returns 0 on success, EINVAL if not possible to satisfy request, 
 *		or EAGAIN if resources are presently busy
 *	job_ptr->nodes is set to the node list (on success)
 * globals: list_part - global list of partition info
 *	default_part_loc - pointer to default partition 
 *	config_list - global list of node configuration info
 */
int
select_nodes (struct job_record *job_ptr) 
{
	int error_code, i, node_set_index, node_set_size = 0;
	bitstr_t *req_bitmap, *scratch_bitmap;
	ListIterator config_record_iterator;
	struct config_record *config_record_point;
	struct node_set *node_set_ptr;
	struct part_record *part_ptr;
	int tmp_feature, check_node_config;

	req_bitmap = scratch_bitmap = NULL;
	config_record_iterator = (ListIterator) NULL;
	node_set_ptr = NULL;
	part_ptr = NULL;

	if (job_ptr == NULL)
		fatal("select_nodes: NULL job pointer value");
	if (job_ptr->magic != JOB_MAGIC)
		fatal("select_nodes: bad job pointer value");

	/* pick up nodes from the weight ordered configuration list */
	if (job_ptr->details->nodes) {	/* insure that selected nodes are in this partition */
		error_code = node_name2bitmap (job_ptr->details->nodes, &req_bitmap);
		if (error_code == EINVAL)
			goto cleanup;
	}
	part_ptr = find_part_record(job_ptr->partition);
	if (part_ptr == NULL)
		fatal("select_nodes: invalid partition name %s for job %s", 
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
		if ((job_ptr->details->min_procs > config_record_point->cpus) ||
		    (job_ptr->details->min_memory > config_record_point->real_memory) ||
		    (job_ptr->details->min_tmp_disk > config_record_point->tmp_disk)) {
			if (FAST_SCHEDULE) 	/* don't bother checking each node */
				continue;
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
#if DEBUG_SYSTEM > 1
		info ("found %d usable nodes from configuration with %s",
			node_set_ptr[node_set_index].nodes,
			config_record_point->nodes);
#endif
		node_set_index++;
		xrealloc (node_set_ptr, sizeof (struct node_set) * (node_set_index + 1));
		node_set_ptr[node_set_size++].my_bitmap = NULL;
	}			
	if (node_set_index == 0) {
		info ("select_nodes: no node configurations satisfy requirements %d:%d:%d:%s",
			job_ptr->details->min_procs, job_ptr->details->min_memory, 
			job_ptr->details->min_tmp_disk, job_ptr->details->features);
		error_code = EINVAL;
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
			info ("select_nodes: requested nodes do not satisfy configurations requirements %d:%d:%d:%s",
			    job_ptr->details->min_procs, job_ptr->details->min_memory, 
			    job_ptr->details->min_tmp_disk, job_ptr->details->features);
			error_code = EINVAL;
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
	if (error_code == EAGAIN)
		goto cleanup;
	if (error_code == EINVAL) {
		info ("select_nodes: no nodes can satisfy job request");
		goto cleanup;
	}			

	/* assign the nodes and stage_in the job */
	error_code = bitmap2node_name (req_bitmap, &(job_ptr->nodes));
	if (error_code) {
		error ("bitmap2node_name error %d", error_code);
		goto cleanup;
	}
	build_node_list (req_bitmap, 
		&job_ptr->details->node_list, 
		&job_ptr->details->total_procs);
	allocate_nodes (req_bitmap);
	job_ptr->job_state = JOB_STAGE_IN;
	job_ptr->start_time = time(NULL);
	if (job_ptr->time_limit == INFINITE)
		job_ptr->end_time = INFINITE;
	else
		job_ptr->end_time = 
			job_ptr->start_time + (job_ptr->time_limit * 60);

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

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
#include <syslog.h>

#include "slurm.h"

#define BUF_SIZE 1024
#define NO_VAL (-99)

struct node_set {		/* set of nodes with same configuration that could be allocated */
	int cpus_per_node;
	int nodes;
	int weight;
	int feature;
	unsigned *my_bitmap;
};

int is_key_valid (int key);
int match_group (char *allow_groups, char *user_groups);
int match_feature (char *seek, char *available);
int parse_job_specs (char *job_specs, char **req_features,
		     char **req_node_list, char **job_name, char **req_group,
		     char **req_partition, int *contiguous, int *req_cpus,
		     int *req_nodes, int *min_cpus, int *min_memory,
		     int *min_tmp_disk, int *key, int *shared);
int pick_best_cpus (unsigned *bitmap, unsigned *req_bitmap, int req_nodes,
		    int req_cpus, int consecutive);
int pick_best_nodes (struct node_set *node_set_ptr, int node_set_size,
		     unsigned **req_bitmap, int req_cpus, int req_nodes,
		     int contiguous, int shared, int max_nodes);
int valid_features (char *requested, char *available);

#if DEBUG_MODULE
/* main is used here for testing purposes only */
main (int argc, char *argv[]) {
	int error_code, line_num, i;
	FILE *command_file;
	char in_line[BUF_SIZE], *node_list;

	if (argc < 3) {
		printf ("usage: %s <SLURM_CONF_file> <slurm_job_file>\n",
			argv[0]);
		exit (1);
	}			

	error_code = init_slurm_conf ();
	if (error_code) {
		printf ("controller: error %d from init_slurm_conf\n",
			error_code);
		exit (error_code);
	}			

	error_code = read_slurm_conf (argv[1]);
	if (error_code) {
		printf ("controller: error %d from read_slurm_conf\n",
			error_code);
		exit (error_code);
	}			

	/* mark everything up and idle for testing */
	for (i = 0; i < node_record_count; i++) {
		bitmap_set (idle_node_bitmap, i);
		bitmap_set (up_node_bitmap, i);
	}			/* for */


	command_file = fopen (argv[2], "r");
	if (command_file == NULL) {
		fprintf (stderr,
			 "node_scheduler: error %d opening command file %s\n",
			 errno, argv[2]);
		exit (1);
	}			

	i = valid_features ("fs1&fs2", "fs1");
	if (i != 0)
		printf ("valid_features error 1\n");
	i = valid_features ("fs1|fs2", "fs1");
	if (i != 1)
		printf ("valid_features error 2\n");
	i = valid_features ("fs1|fs2&fs3", "fs1,fs3");
	if (i != 1)
		printf ("valid_features error 3\n");
	i = valid_features ("[fs1|fs2]&fs3", "fs2,fs3");
	if (i != 2)
		printf ("valid_features error 4\n");
	i = valid_features ("fs0&[fs1|fs2]&fs3", "fs2,fs3");
	if (i != 0)
		printf ("valid_features error 5\n");
	i = valid_features ("fs3&[fs1|fs2]&fs3", "fs2,fs3");
	if (i != 2)
		printf ("valid_features error 6\n");

	line_num = 0;
	printf ("\n");
	while (fgets (in_line, BUF_SIZE, command_file)) {
		if (in_line[strlen (in_line) - 1] == '\n')
			in_line[strlen (in_line) - 1] = (char) NULL;
		line_num++;
		error_code = select_nodes (in_line, &node_list);
		if (error_code) {
			if (strncmp (in_line, "JobName=FAIL", 12) != 0)
				printf ("ERROR:");
			printf ("for job: %s\n", in_line, node_list);
			printf ("node_scheduler: error %d from select_nodes on line %d\n\n", 
				error_code, line_num);
		}
		else {
			if (strncmp (in_line, "job_name=fail", 12) == 0)
				printf ("ERROR: ");
			printf ("for job: %s\n  nodes selected %s\n\n",
				in_line, node_list);
			free (node_list);
		}		
	}			
}
#endif


/* for a given bitmap, change the state of specified nodes to stage_in */
/* this is a simple prototype for testing */
void allocate_nodes (unsigned *bitmap) {
	int i;

	for (i = 0; i < node_record_count; i++) {
		if (bitmap_value (bitmap, i) == 0)
			continue;
		node_record_table_ptr[i].node_state = STATE_STAGE_IN;
		bitmap_clear (idle_node_bitmap, i);
	}			/* for */
	return;
}


/* 
 * count_cpus - report how many cpus are associated with the identified nodes 
 * input: bitmap - a node bitmap
 * output: returns a cpu count
 */
int count_cpus (unsigned *bitmap) {
	int i, sum;

	sum = 0;
	for (i = 0; i < node_record_count; i++) {
		if (bitmap_value (bitmap, i) != 1)
			continue;
		sum += node_record_table_ptr[i].cpus;
	}			/* for */
	return sum;
}


/* 
 * is_key_valid - determine if supplied key is valid
 * input: key - a slurm key acquired by user root
 * output: returns 1 if key is valid, 0 otherwise
 * NOTE: this is only a placeholder for a future function
 */
int is_key_valid (int key) {
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
int match_feature (char *seek, char *available) {
	char *tmp_available, *str_ptr3, *str_ptr4;
	int found;

	if (seek == NULL)
		return 1;	/* nothing to look for */
	if (available == NULL)
		return 0;	/* nothing to find */

	tmp_available = malloc (strlen (available) + 1);
	if (tmp_available == NULL) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "match_feature: unable to allocate memory\n");
#else
		syslog (LOG_ALERT,
			"match_feature: unable to allocate memory\n");
#endif
		return 1;	/* assume good for now */
	}			
	strcpy (tmp_available, available);

	found = 0;
	str_ptr3 = (char *) strtok_r (tmp_available, ",", &str_ptr4);
	while (str_ptr3) {
		if (strcmp (seek, str_ptr3) == 0) {	/* we have a match */
			found = 1;
			break;
		}		
		str_ptr3 = (char *) strtok_r (NULL, ",", &str_ptr4);
	}			/* while (str_ptr3) */

	free (tmp_available);
	return found;
}


/*
 * match_group - determine if the user is a member of any groups permitted to use this partition
 * input: allow_groups - comma delimited list of groups permitted to use the partition, 
 *			NULL is for all groups
 *        user_groups - comma delimited list of groups the user belongs to
 * output: returns 1 if user is member, 0 otherwise
 */
int match_group (char *allow_groups, char *user_groups) {
	char *tmp_allow_group, *str_ptr1, *str_ptr2;
	char *tmp_user_group, *str_ptr3, *str_ptr4;

	if ((allow_groups == NULL) ||	/* anybody can use it */
	    (strcmp (allow_groups, "all") == 0))
		return 1;
	if (user_groups == NULL)
		return 0;	/* empty group list */

	tmp_allow_group = malloc (strlen (allow_groups) + 1);
	if (tmp_allow_group == NULL) {
#if DEBUG_SYSTEM
		fprintf (stderr, "match_group: unable to allocate memory\n");
#else
		syslog (LOG_ALERT,
			"match_group: unable to allocate memory\n");
#endif
		return 1;	/* assume good for now */
	}			
	strcpy (tmp_allow_group, allow_groups);

	tmp_user_group = malloc (strlen (user_groups) + 1);
	if (tmp_user_group == NULL) {
#if DEBUG_SYSTEM
		fprintf (stderr, "match_group: unable to allocate memory\n");
#else
		syslog (LOG_ALERT,
			"match_group: unable to allocate memory\n");
#endif
		free (tmp_allow_group);
		return 1;	/* assume good for now */
	}			

	str_ptr1 = (char *) strtok_r (tmp_allow_group, ",", &str_ptr2);
	while (str_ptr1) {
		strcpy (tmp_user_group, user_groups);
		str_ptr3 = (char *) strtok_r (tmp_user_group, ",", &str_ptr4);
		while (str_ptr3) {
			if (strcmp (str_ptr1, str_ptr3) == 0) {	/* we have a match */
				free (tmp_allow_group);
				free (tmp_user_group);
				return 1;
			}	
			str_ptr3 = (char *) strtok_r (NULL, ",", &str_ptr4);
		}	
		str_ptr1 = (char *) strtok_r (NULL, ",", &str_ptr2);
	}		
	free (tmp_allow_group);
	free (tmp_user_group);
	return 0;		/* no match */
}


/* 
 * parse_job_specs - pick the appropriate fields out of a job request specification
 * input: job_specs - string containing the specification
 *        req_features, etc. - pointers to storage for the specifications
 * output: req_features, etc. - the job's specifications
 *         returns 0 if no error, errno otherwise
 * NOTE: the calling function must free memory at req_features[0], req_node_list[0],
 *	job_name[0], req_group[0], and req_partition[0]
 */
int parse_job_specs (char *job_specs, char **req_features, char **req_node_list,
		 char **job_name, char **req_group, char **req_partition,
		 int *contiguous, int *req_cpus, int *req_nodes,
		 int *min_cpus, int *min_memory, int *min_tmp_disk, int *key,
		 int *shared) {
	int bad_index, error_code, i;
	char *temp_specs;

	req_features[0] = req_node_list[0] = req_group[0] = req_partition[0] =
		job_name[0] = NULL;
	*contiguous = *req_cpus = *req_nodes = *min_cpus = *min_memory =
		*min_tmp_disk = NO_VAL;
	*key = *shared = NO_VAL;

	temp_specs = malloc (strlen (job_specs) + 1);
	if (temp_specs == NULL) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "parse_job_specs: unable to allocate memory\n");
#else
		syslog (LOG_ALERT,
			"parse_job_specs: unable to allocate memory\n");
#endif
		abort ();
	}			
	strcpy (temp_specs, job_specs);

	error_code = load_string (job_name, "JobName=", temp_specs);
	if (error_code)
		goto cleanup;

	error_code = load_string (req_features, "Features=", temp_specs);
	if (error_code)
		goto cleanup;

	error_code = load_string (req_node_list, "NodeList=", temp_specs);
	if (error_code)
		goto cleanup;

	error_code = load_string (req_group, "Groups=", temp_specs);
	if (error_code)
		goto cleanup;

	error_code = load_string (req_partition, "Partition=", temp_specs);
	if (error_code)
		goto cleanup;

	error_code = load_integer (contiguous, "Contiguous", temp_specs);
	if (error_code)
		goto cleanup;

	error_code = load_integer (req_cpus, "TotalCPUs=", temp_specs);
	if (error_code)
		goto cleanup;

	error_code = load_integer (req_nodes, "TotalNodes=", temp_specs);
	if (error_code)
		goto cleanup;

	error_code = load_integer (min_cpus, "MinCPUs=", temp_specs);
	if (error_code)
		goto cleanup;

	error_code = load_integer (min_memory, "MinMemory=", temp_specs);
	if (error_code)
		goto cleanup;

	error_code = load_integer (min_tmp_disk, "MinTmpDisk=", temp_specs);
	if (error_code)
		goto cleanup;

	error_code = load_integer (key, "Key=", temp_specs);
	if (error_code)
		goto cleanup;

	error_code = load_integer (shared, "Shared=", temp_specs);
	if (error_code)
		goto cleanup;

	bad_index = -1;
	for (i = 0; i < strlen (temp_specs); i++) {
		if (isspace ((int) temp_specs[i]) || (temp_specs[i] == '\n'))
			continue;
		bad_index = i;
		break;
	}			

	if (bad_index != -1) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "parse_job_specs: bad job specification input: %s\n",
			 &temp_specs[bad_index]);
#else
		syslog (LOG_ERR,
			"parse_job_specs: bad job specification input: %s\n",
			&temp_specs[bad_index]);
#endif
		error_code = EINVAL;
	}			

	free (temp_specs);
	return error_code;

      cleanup:
	free (temp_specs);
	if (req_features[0])
		free (req_features[0]);
	if (req_node_list[0])
		free (req_node_list[0]);
	if (req_group[0])
		free (req_group[0]);
	if (req_partition[0])
		free (req_partition[0]);
	if (job_name[0])
		free (job_name[0]);
	req_features[0] = req_node_list[0] = req_group[0] = req_partition[0] =
		job_name[0] = NULL;
}


/*
 * pick_best_cpus - identify the nodes which best fit the req_nodes and req_cpus counts
 * input: bitmap - the bit map to search
 *        req_bitmap - the bit map of nodes that must be selected, if not NULL these 
 *                     have already been confirmed to be in the input bitmap
 *        req_nodes - number of nodes required
 *        req_cpus - number of cpus required
 *        consecutive - nodes must be consecutive is 1, otherwise 0
 * output: bitmap - nodes not required to satisfy the request are cleared, other left set
 *         returns zero on success, EINVAL otherwise
 * NOTE: bitmap must be a superset of req_nodes at function call time
 */
int pick_best_cpus (unsigned *bitmap, unsigned *req_bitmap, int req_nodes,
		int req_cpus, int consecutive) {
	int i, index, error_code, sufficient;
	int *consec_nodes;	/* how many nodes we can add from this consecutive set of nodes */
	int *consec_cpus;	/* how many nodes we can add from this consecutive set of nodes */
	int *consec_start;	/* where this consecutive set starts (index) */
	int *consec_end;	/* where this consecutive set ends (index) */
	int *consec_req;	/* are nodes from this set required (in req_bitmap) */
	int consec_index, consec_size;
	int rem_cpus, rem_nodes;	/* remaining resources required */
	int best_fit_nodes, best_fit_cpus, best_fit_req, best_fit_location,
		best_fit_sufficient;

	if (bitmap == NULL) {
#if DEBUG_SYSTEM
		fprintf (stderr, "pick_best_cpus: bitmap pointer is NULL\n");
#else
		syslog (LOG_ALERT,
			"pick_best_cpus: bitmap pointer is NULL\n");
#endif
		abort ();
	}			

	error_code = EINVAL;	/* default is no fit */
	consec_index = 0;
	consec_size = 50;	/* start allocation for 50 sets of consecutive nodes */
	consec_cpus = malloc (sizeof (int) * consec_size);
	consec_nodes = malloc (sizeof (int) * consec_size);
	consec_start = malloc (sizeof (int) * consec_size);
	consec_end = malloc (sizeof (int) * consec_size);
	consec_req = malloc (sizeof (int) * consec_size);
	if ((consec_cpus == NULL) || (consec_nodes == NULL) ||
	    (consec_start == NULL) || (consec_end == NULL)
	    || (consec_req == NULL)) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "pick_best_cpus: unable to allocate memory\n");
#else
		syslog (LOG_ALERT,
			"pick_best_cpus: unable to allocate memory\n");
#endif
		goto cleanup;
	}			

	consec_cpus[consec_index] = consec_nodes[consec_index] = 0;
	consec_req[consec_index] = -1;	/* no required nodes here by default */
	rem_cpus = req_cpus;
	rem_nodes = req_nodes;
	for (index = 0; index < node_record_count; index++) {
		if (bitmap_value (bitmap, index)) {
			if (consec_nodes[consec_index] == 0)
				consec_start[consec_index] = index;
			i = node_record_table_ptr[index].cpus;
			if (req_bitmap && bitmap_value (req_bitmap, index)) {
				if (consec_req[consec_index] == -1)
					consec_req[consec_index] = index;	/* first required node in set */
				rem_cpus -= i;	/* reduce count of additional resources required */
				rem_nodes--;	/* reduce count of additional resources required */
			}
			else {
				bitmap_clear (bitmap, index);
				consec_cpus[consec_index] += i;
				consec_nodes[consec_index]++;
			}	
		}
		else if (consec_nodes[consec_index] == 0) {
			consec_req[consec_index] = -1;	/* already picked up any required nodes */
			/* re-use this record */
		}
		else {
			consec_end[consec_index] = index - 1;
			if (++consec_index >= consec_size) {
				consec_size *= 2;
				consec_cpus =
					realloc (consec_cpus,
						 sizeof (int) * consec_size);
				consec_nodes =
					realloc (consec_nodes,
						 sizeof (int) * consec_size);
				consec_start =
					realloc (consec_start,
						 sizeof (int) * consec_size);
				consec_end =
					realloc (consec_end,
						 sizeof (int) * consec_size);
				consec_req =
					realloc (consec_req,
						 sizeof (int) * consec_size);
				if ((consec_cpus == NULL)
				    || (consec_nodes == NULL)
				    || (consec_start == NULL)
				    || (consec_end == NULL)
				    || (consec_req == NULL)) {
#if DEBUG_SYSTEM
					fprintf (stderr,
						 "pick_best_cpus: unable to allocate memory\n");
#else
					syslog (LOG_ALERT,
						"pick_best_cpus: unable to allocate memory\n");
#endif
					goto cleanup;
				}	
			}	
			consec_cpus[consec_index] = 0;
			consec_nodes[consec_index] = 0;
			consec_req[consec_index] = -1;
		}		
	}
	if (consec_nodes[consec_index] != 0)
		consec_end[consec_index++] = index - 1;

#if DEBUG_SYSTEM > 1
	printf ("rem_cpus=%d, rem_nodes=%d\n", rem_cpus, rem_nodes);
	for (i = 0; i < consec_index; i++) {
		printf ("start=%s, end=%s, nodes=%d, cpus=%d",
			node_record_table_ptr[consec_start[i]].name,
			node_record_table_ptr[consec_end[i]].name,
			consec_nodes[i], consec_cpus[i]);
		if (consec_req[i] != -1)
			printf (", req=%s\n",
				node_record_table_ptr[consec_req[i]].name);
		else
			printf ("\n");
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
		}		/* for */
		if (best_fit_nodes == 0)
			break;
		if (consecutive
		    && ((best_fit_nodes < rem_nodes)
			|| (best_fit_cpus < rem_cpus)))
			break;	/* no hole large enough */
		if (best_fit_req != -1) {	/* work out from required nodes */
			for (i = best_fit_req;
			     i <= consec_end[best_fit_location]; i++) {
				if ((rem_nodes <= 0) && (rem_cpus <= 0))
					break;
				if (bitmap_value (bitmap, i))
					continue;
				bitmap_set (bitmap, i);
				rem_nodes--;
				rem_cpus -= node_record_table_ptr[i].cpus;
			}	/* for */
			for (i = (best_fit_req - 1);
			     i >= consec_start[best_fit_location]; i--) {
				if ((rem_nodes <= 0) && (rem_cpus <= 0))
					break;
				/* if (bitmap_value(bitmap, i)) continue;  nothing set earlier */
				bitmap_set (bitmap, i);
				rem_nodes--;
				rem_cpus -= node_record_table_ptr[i].cpus;
			}	/* for */
		}
		else {
			for (i = consec_start[best_fit_location];
			     i <= consec_end[best_fit_location]; i++) {
				if ((rem_nodes <= 0) && (rem_cpus <= 0))
					break;
				if (bitmap_value (bitmap, i))
					continue;
				bitmap_set (bitmap, i);
				rem_nodes--;
				rem_cpus -= node_record_table_ptr[i].cpus;
			}	/* for */
		}		
		if ((rem_nodes <= 0) && (rem_cpus <= 0)) {
			error_code = 0;
			break;
		}		
		consec_cpus[best_fit_location] = 0;
		consec_nodes[best_fit_location] = 0;
	}			

      cleanup:
	if (consec_cpus)
		free (consec_cpus);
	if (consec_nodes)
		free (consec_nodes);
	if (consec_start)
		free (consec_start);
	if (consec_end)
		free (consec_end);
	if (consec_req)
		free (consec_req);
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
 *        max_nodes - maximum number of nodes permitted for job, -1 for none (partition limit)
 * output: req_bitmap - pointer to bitmap of selected nodes
 *         returns 0 on success, EAGAIN if request can not be satisfied now, 
 *		EINVAL if request can never be satisfied (insufficient contiguous nodes)
 * NOTE: the caller must free memory pointed to by req_bitmap
 */
int
pick_best_nodes (struct node_set *node_set_ptr, int node_set_size,
		 unsigned **req_bitmap, int req_cpus, int req_nodes,
		 int contiguous, int shared, int max_nodes) {
	int error_code, i, j, size;
	int total_nodes, total_cpus;	/* total resources configured in partition */
	int avail_nodes, avail_cpus;	/* resources available for use now */
	unsigned *avail_bitmap, *total_bitmap;
	int max_feature, min_feature;
	int *cpus_per_node;
	int avail_set, total_set, runable;

	if (node_set_size == 0)
		return EINVAL;
	if ((max_nodes != -1) && (req_nodes > max_nodes))
		return EINVAL;
	error_code = 0;
	avail_bitmap = total_bitmap = NULL;
	avail_nodes = avail_cpus = 0;
	total_nodes = total_cpus = 0;
	if (req_bitmap[0]) {	/* specific nodes required */
		/* NOTE: we have already confirmed that all of these nodes have a usable */
		/*       configuration and are in the proper partition */
		if (req_nodes != 0)
			total_nodes = bitmap_count (req_bitmap[0]);
		if (req_cpus != 0)
			total_cpus = count_cpus (req_bitmap[0]);
		if (total_nodes > max_nodes)
			return EINVAL;
		if ((req_nodes <= total_nodes) && (req_cpus <= total_cpus)) {
			if (bitmap_is_super (req_bitmap[0], up_node_bitmap) !=
			    1)
				return EAGAIN;
			if ((shared != 1)
			    &&
			    (bitmap_is_super (req_bitmap[0], idle_node_bitmap)
			     != 1))
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
	}			/* for */

	runable = 0;		/* assume not runable until otherwise demonstrated */
	for (j = min_feature; j <= max_feature; j++) {
		avail_set = total_set = 0;
		for (i = 0; i < node_set_size; i++) {
			if (node_set_ptr[i].feature != j)
				continue;
			if (runable == 0) {
				if (total_set)
					bitmap_or (total_bitmap,
						   node_set_ptr[i].my_bitmap);
				else {
					total_bitmap =
						bitmap_copy (node_set_ptr[i].
							     my_bitmap);
					if (total_bitmap == NULL) {	/* no memory */
						if (avail_bitmap)
							free (avail_bitmap);
						return EAGAIN;
					}	
					total_set = 1;
				}	
				total_nodes += node_set_ptr[i].nodes;
				total_cpus +=
					(node_set_ptr[i].nodes *
					 node_set_ptr[i].cpus_per_node);
			}	
			bitmap_and (node_set_ptr[i].my_bitmap,
				    up_node_bitmap);
			if (shared != 1)
				bitmap_and (node_set_ptr[i].my_bitmap,
					    idle_node_bitmap);
			node_set_ptr[i].nodes =
				bitmap_count (node_set_ptr[i].my_bitmap);
			if (avail_set)
				bitmap_or (avail_bitmap,
					   node_set_ptr[i].my_bitmap);
			else {
				avail_bitmap =
					bitmap_copy (node_set_ptr[i].
						     my_bitmap);
				if (avail_bitmap == NULL) {	/* no memory */
					if (total_bitmap)
						free (total_bitmap);
					return EAGAIN;
				}	
				avail_set = 1;
			}	
			avail_nodes += node_set_ptr[i].nodes;
			avail_cpus +=
				(node_set_ptr[i].nodes *
				 node_set_ptr[i].cpus_per_node);
			if ((req_bitmap[0])
			    && (bitmap_is_super (req_bitmap[0], avail_bitmap)
				== 0))
				continue;
			if (avail_nodes < req_nodes)
				continue;
			if (avail_cpus < req_cpus)
				continue;
			error_code =
				pick_best_cpus (avail_bitmap, req_bitmap[0],
						req_nodes, req_cpus,
						contiguous);
			if ((error_code == 0) && (max_nodes != -1)
			    && (bitmap_count (avail_bitmap) > max_nodes)) {
				error_code = EINVAL;
				break;
			}	
			if (error_code == 0) {
				if (total_bitmap)
					free (total_bitmap);
				if (req_bitmap[0])
					free (req_bitmap[0]);
				req_bitmap[0] = avail_bitmap;
				return 0;
			}	
		}		/* for (i */
		if ((error_code == 0) && (runable == 0) &&
		    (total_nodes > req_nodes) && (total_cpus > req_cpus) &&
		    ((req_bitmap[0] == NULL)
		     || (bitmap_is_super (req_bitmap[0], avail_bitmap) == 1))
		    && ((max_nodes == -1) || (req_nodes <= max_nodes))) {
			/* determine if job could possibly run (if configured nodes all available) */
			error_code =
				pick_best_cpus (total_bitmap, req_bitmap[0],
						req_nodes, req_cpus,
						contiguous);
			if ((error_code == 0) && (max_nodes != -1)
			    && (bitmap_count (avail_bitmap) > max_nodes))
				error_code = EINVAL;
			if (error_code == 0)
				runable = 1;
		}		
		if (avail_bitmap)
			free (avail_bitmap);
		if (total_bitmap)
			free (total_bitmap);
		avail_bitmap = total_bitmap = NULL;
		if (error_code != 0)
			break;
	}			/* for (j */

	if (runable == 0)
		error_code = EINVAL;
	if (error_code == 0)
		error_code = EAGAIN;
	return error_code;
}


/*
 * select_nodes - select and allocate nodes to a job with the given specifications
 * input: job_specs - job specifications
 *        node_list - pointer to node list returned
 * output: node_list - list of allocated nodes
 *         returns 0 on success, EINVAL if not possible to satisfy request, 
 *		or EAGAIN if resources are presently busy
 * NOTE: the calling program must free the memory pointed to by node_list
 */
int
select_nodes (char *job_specs, char **node_list) {
	char *req_features, *req_node_list, *job_name, *req_group,
		*req_partition, *out_line;
	int contiguous, req_cpus, req_nodes, min_cpus, min_memory,
		min_tmp_disk;
	int error_code, cpu_tally, node_tally, key, shared;
	struct part_record *part_ptr;
	unsigned *req_bitmap, *scratch_bitmap;
	ListIterator config_record_iterator;	/* for iterating through config_list */
	struct config_record *config_record_point;	/* pointer to config_record */
	int i;
	struct node_set *node_set_ptr;
	int node_set_index, node_set_size;

	req_features = req_node_list = job_name = req_group = req_partition =
		NULL;
	req_bitmap = scratch_bitmap = NULL;
	contiguous = req_cpus = req_nodes = min_cpus = min_memory =
		min_tmp_disk = NO_VAL;
	key = shared = NO_VAL;
	node_set_ptr = NULL;
	config_record_iterator = NULL;
	node_list[0] = NULL;
	config_record_iterator = (ListIterator) NULL;
	node_lock ();
	part_lock ();

	/* setup and basic parsing */
	error_code =
		parse_job_specs (job_specs, &req_features, &req_node_list,
				 &job_name, &req_group, &req_partition,
				 &contiguous, &req_cpus, &req_nodes,
				 &min_cpus, &min_memory, &min_tmp_disk, &key,
				 &shared);
	if (error_code != 0) {
		error_code = EINVAL;	/* permanent error, invalid parsing */
#if DEBUG_SYSTEM
		fprintf (stderr, "select_nodes: parsing failure on %s\n",
			 job_specs);
#else
		syslog (LOG_NOTICE, "select_nodes: parsing failure on %s\n",
			job_specs);
#endif
		goto cleanup;
	}			
	if ((req_cpus == NO_VAL) && (req_nodes == NO_VAL)
	    && (req_node_list == NULL)) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "select_nodes: job failed to specify node_list, cpu or node count\n");
#else
		syslog (LOG_NOTICE,
			"select_nodes: job failed to specify node_list, cpu or node count\n");
#endif
		error_code = EINVAL;
		goto cleanup;
	}			
	if (contiguous == NO_VAL)
		contiguous = 0;	/* default not contiguous */
	if (req_cpus == NO_VAL)
		req_cpus = 0;	/* default no cpu count requirements */
	if (req_nodes == NO_VAL)
		req_nodes = 0;	/* default no node count requirements */


	/* find selected partition */
	if (req_partition) {
		part_ptr =
			list_find_first (part_list, &list_find_part,
					 req_partition);
		if (part_ptr == NULL) {
#if DEBUG_SYSTEM
			fprintf (stderr,
				 "select_nodes: invalid partition specified: %s\n",
				 req_partition);
#else
			syslog (LOG_NOTICE,
				"select_nodes: invalid partition specified: %s\n",
				req_partition);
#endif
			error_code = EINVAL;
			goto cleanup;
		}		
	}
	else {
		if (default_part_loc == NULL) {
#if DEBUG_SYSTEM
			fprintf (stderr,
				 "select_nodes: default partition not set.\n");
#else
			syslog (LOG_ERR,
				"select_nodes: default partition not set.\n");
#endif
			error_code = EINVAL;
			goto cleanup;
		}		
		part_ptr = default_part_loc;
	}			


	/* can this user access this partition */
	if (part_ptr->key && (is_key_valid (key) == 0)) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "select_nodes: job lacks key required of partition %s\n",
			 part_ptr->name);
#else
		syslog (LOG_NOTICE,
			"select_nodes: job lacks key required of partition %s\n",
			part_ptr->name);
#endif
		error_code = EINVAL;
		goto cleanup;
	}			
	if (match_group (part_ptr->allow_groups, req_group) == 0) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "select_nodes: job lacks group required of partition %s\n",
			 part_ptr->name);
#else
		syslog (LOG_NOTICE,
			"select_nodes: job lacks group required of partition %s\n",
			part_ptr->name);
#endif
		error_code = EINVAL;
		goto cleanup;
	}			


	/* check if select partition has sufficient resources to satisfy request */
	if (req_node_list) {	/* insure that selected nodes are in this partition */
		error_code = node_name2bitmap (req_node_list, &req_bitmap);
		if (error_code == EINVAL)
			goto cleanup;
		if (error_code != 0) {
			error_code = EAGAIN;	/* no memory */
			goto cleanup;
		}		
		if (contiguous == 1)
			bitmap_fill (req_bitmap);
		if (bitmap_is_super (req_bitmap, part_ptr->node_bitmap) != 1) {
#if DEBUG_SYSTEM
			fprintf (stderr,
				 "select_nodes: requested nodes %s not in partition %s\n",
				 req_node_list, part_ptr->name);
#else
			syslog (LOG_NOTICE,
				"select_nodes: requested nodes %s not in partition %s\n",
				req_node_list, part_ptr->name);
#endif
			error_code = EINVAL;
			goto cleanup;
		}		
		i = count_cpus (req_bitmap);
		if (i > req_cpus)
			req_cpus = i;
		i = bitmap_count (req_bitmap);
		if (i > req_nodes)
			req_nodes = i;
	}			
	if (req_cpus > part_ptr->total_cpus) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "select_nodes: too many cpus (%d) requested of partition %s(%d)\n",
			 req_cpus, part_ptr->name, part_ptr->total_cpus);
#else
		syslog (LOG_NOTICE,
			"select_nodes: too many cpus (%d) requested of partition %s(%d)\n",
			req_cpus, part_ptr->name, part_ptr->total_cpus);
#endif
		error_code = EINVAL;
		goto cleanup;
	}			
	if ((req_nodes > part_ptr->total_nodes)
	    || (req_nodes > part_ptr->max_nodes)) {
		if (part_ptr->total_nodes > part_ptr->max_nodes)
			i = part_ptr->max_nodes;
		else
			i = part_ptr->total_nodes;
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "select_nodes: too many nodes (%d) requested of partition %s(%d)\n",
			 req_nodes, part_ptr->name, i);
#else
		syslog (LOG_NOTICE,
			"select_nodes: too many nodes (%d) requested of partition %s(%d)\n",
			req_nodes, part_ptr->name, i);
#endif
		error_code = EINVAL;
		goto cleanup;
	}			
	if (part_ptr->shared == 2)	/* shared=force */
		shared = 1;
	else if ((shared != 1) || (part_ptr->shared == 0))	/* user or partition want no sharing */
		shared = 0;


	/* pick up nodes from the weight ordered configuration list */
	node_set_index = 0;
	node_set_size = 0;
	node_set_ptr = (struct node_set *) malloc (sizeof (struct node_set));
	if (node_set_ptr == NULL) {
#if DEBUG_SYSTEM
		fprintf (stderr, "select_nodes: unable to allocate memory\n");
#else
		syslog (LOG_ALERT,
			"select_nodes: unable to allocate memory\n");
#endif
		error_code = EAGAIN;
		goto cleanup;
	}			
	node_set_ptr[node_set_size++].my_bitmap = NULL;

	config_record_iterator = list_iterator_create (config_list);
	if (config_record_iterator == NULL) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "select_nodes: ListIterator_create unable to allocate memory\n");
#else
		syslog (LOG_ALERT,
			"select_nodes: ListIterator_create unable to allocate memory\n");
#endif
		error_code = EAGAIN;
		goto cleanup;
	}			

	while (config_record_point =
	       (struct config_record *) list_next (config_record_iterator)) {
		int tmp_feature, check_node_config;

		tmp_feature =
			valid_features (req_features,
					config_record_point->feature);
		if (tmp_feature == 0)
			continue;

		/* since nodes can register with more resources than defined in the configuration,    */
		/* we want to use those higher values for scheduling, but only as needed */
		if ((min_cpus > config_record_point->cpus) ||
		    (min_memory > config_record_point->real_memory) ||
		    (min_tmp_disk > config_record_point->tmp_disk))
			check_node_config = 1;
		else
			check_node_config = 0;
		node_set_ptr[node_set_index].my_bitmap =
			bitmap_copy (config_record_point->node_bitmap);
		if (node_set_ptr[node_set_index].my_bitmap == NULL) {
			error_code = EAGAIN;	/* no memory */
			goto cleanup;
		}		
		bitmap_and (node_set_ptr[node_set_index].my_bitmap,
			    part_ptr->node_bitmap);
		node_set_ptr[node_set_index].nodes =
			bitmap_count (node_set_ptr[node_set_index].my_bitmap);
		/* check configuration of individual nodes only if the check of baseline values in the */
		/*  configuration file are too low. this will slow the scheduling for very large cluster. */
		if (check_node_config
		    && (node_set_ptr[node_set_index].nodes != 0)) {
			for (i = 0; i < node_record_count; i++) {
				if (bitmap_value
				    (node_set_ptr[node_set_index].my_bitmap,
				     i) == 0)
					continue;
				if ((min_cpus <=
				     node_record_table_ptr[i].cpus)
				    && (min_memory <=
					node_record_table_ptr[i].real_memory)
				    && (min_tmp_disk <=
					node_record_table_ptr[i].tmp_disk))
					continue;
				bitmap_clear (node_set_ptr[node_set_index].
					      my_bitmap, i);
				if ((--node_set_ptr[node_set_index].nodes) ==
				    0)
					break;
			}	/* for */
		}		
		if (node_set_ptr[node_set_index].nodes == 0) {
			free (node_set_ptr[node_set_index].my_bitmap);
			node_set_ptr[node_set_index].my_bitmap = NULL;
			continue;
		}		
		if (req_bitmap) {
			if (scratch_bitmap)
				bitmap_or (scratch_bitmap,
					   node_set_ptr[node_set_index].
					   my_bitmap);
			else {
				scratch_bitmap =
					bitmap_copy (node_set_ptr
						     [node_set_index].
						     my_bitmap);
				if (scratch_bitmap == NULL) {
					error_code = EAGAIN;
					goto cleanup;
				}	
			}	
		}		
		node_set_ptr[node_set_index].cpus_per_node =
			config_record_point->cpus;
		node_set_ptr[node_set_index].weight =
			config_record_point->weight;
		node_set_ptr[node_set_index].feature = tmp_feature;
#if DEBUG_MODULE > 1
		printf ("found %d usable nodes from configuration with %s\n",
			node_set_ptr[node_set_index].nodes,
			config_record_point->nodes);
#endif
		node_set_index++;
		node_set_ptr = (struct node_set *) realloc (node_set_ptr,
							    sizeof (struct
								    node_set)
							    *
							    (node_set_index +
							     1));
		if (node_set_ptr == 0) {
#if DEBUG_SYSTEM
			fprintf (stderr,
				 "select_nodes: unable to allocate memory\n");
#else
			syslog (LOG_ALERT,
				"select_nodes: unable to allocate memory\n");
#endif
			error_code = EAGAIN;	/* no memory */
			goto cleanup;
		}		
		node_set_ptr[node_set_size++].my_bitmap = NULL;
	}			
	if (node_set_index == 0) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "select_nodes: no node configurations satisfy requirements %d:%d:%d:%s\n",
			 min_cpus, min_memory, min_tmp_disk, req_features);
#else
		syslog (LOG_NOTICE,
			"select_nodes: no node configurations satisfy requirements %d:%d:%d:%s\n",
			min_cpus, min_memory, min_tmp_disk, req_features);
#endif
		error_code = EINVAL;
		goto cleanup;
	}			
	if (node_set_ptr[node_set_index].my_bitmap)
		free (node_set_ptr[node_set_index].my_bitmap);
	node_set_ptr[node_set_index].my_bitmap = NULL;
	node_set_size = node_set_index;

	if (req_bitmap) {
		if ((scratch_bitmap == NULL)
		    || (bitmap_is_super (req_bitmap, scratch_bitmap) != 1)) {
#if DEBUG_SYSTEM
			fprintf (stderr,
				 "select_nodes: requested nodes do not satisfy configurations requirements %d:%d:%d:%s\n",
				 min_cpus, min_memory, min_tmp_disk,
				 req_features);
#else
			syslog (LOG_NOTICE,
				"select_nodes: requested nodes do not satisfy configurations requirements %d:%d:%d:%s\n",
				min_cpus, min_memory, min_tmp_disk,
				req_features);
#endif
			error_code = EINVAL;
			goto cleanup;
		}		
	}			


	/* pick the nodes providing a best-fit */
	error_code = pick_best_nodes (node_set_ptr, node_set_size,
				      &req_bitmap, req_cpus, req_nodes,
				      contiguous, shared,
				      part_ptr->max_nodes);
	if (error_code == EAGAIN)
		goto cleanup;
	if (error_code == EINVAL) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "select_nodes: no nodes can satisfy job request\n");
#else
		syslog (LOG_NOTICE,
			"select_nodes: no nodes can satisfy job request\n");
#endif
		goto cleanup;
	}			

	/* mark the selected nodes as STATE_STAGE_IN */
	allocate_nodes (req_bitmap);
	error_code = bitmap2node_name (req_bitmap, node_list);
	if (error_code)
		printf ("bitmap2node_name error %d\n", error_code);


      cleanup:
	part_unlock ();
	node_unlock ();
	if (req_features)
		free (req_features);
	if (req_node_list)
		free (req_node_list);
	if (job_name)
		free (job_name);
	if (req_group)
		free (req_group);
	if (req_partition)
		free (req_partition);
	if (req_bitmap)
		free (req_bitmap);
	if (scratch_bitmap)
		free (scratch_bitmap);
	if (node_set_ptr) {
		for (i = 0; i < node_set_size; i++) {
			if (node_set_ptr[i].my_bitmap)
				free (node_set_ptr[i].my_bitmap);
		}		/* for */
		free (node_set_ptr);
	}			
	if (config_record_iterator)
		list_iterator_destroy (config_record_iterator);
	return error_code;
}


/* valid_features - determine if the requested features are satisfied by those available
 * input: requested - requested features (by a job)
 *        available - available features (on a node)
 * output: returns 0 if request is not satisfied, otherwise an integer indicating 
 *		which mutually exclusive feature is satisfied. for example
 *		valid_features("[fs1|fs2|fs3|fs4]", "fs3") returns 3. see the 
 *		slurm administrator and user guides for details. returns 1 if 
 *		requirements are satisfied without mutually exclusive feature list.
 */
int
valid_features (char *requested, char *available) {
	char *tmp_requested, *str_ptr1;
	int bracket, found, i, option, position, result;
	int last_op;		/* last operation 0 for or, 1 for and */
	int save_op, save_result;	/* for bracket support */

	if (requested == NULL)
		return 1;	/* no constraints */
	if (available == NULL)
		return 0;	/* no features */

	tmp_requested = malloc (strlen (requested) + 1);
	if (tmp_requested == NULL) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "valid_features: unable to allocate memory\n");
#else
		syslog (LOG_ALERT,
			"valid_features: unable to allocate memory\n");
#endif
		return 1;	/* assume good for now */
	}			
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
			else	/* or */
				result |= found;
			break;
		}		

		if (tmp_requested[i] == '&') {
			if (bracket != 0) {
#if DEBUG_SYSTEM
				fprintf (stderr,
					 "valid_features: parsing failure 1 on %s\n",
					 requested);
#else
				syslog (LOG_NOTICE,
					"valid_features: parsing failure 1 on %s\n",
					requested);
#endif
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
#if DEBUG_SYSTEM
				fprintf (stderr,
					 "valid_features: parsing failure 2 on %s\n",
					 requested);
#else
				syslog (LOG_NOTICE,
					"valid_features: parsing failure 2 on %s\n",
					requested);
#endif
				result = 0;
				break;
			}	
			bracket = 0;
		}		
	}			/* for */

	if (position)
		result *= option;
	free (tmp_requested);
	return result;
}

/*****************************************************************************\
 *  sinfo.c - Report overall state the system
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Joey Ekstrom <ekstrom1@llnl.gov>, Moe Jette <jette1@llnl.gov>
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

#include "src/sinfo/sinfo.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"

#define NODE_SIZE_PART		9
#define NODE_SIZE_STATE		6
#define NODE_SIZE_STATE_LONG	11
#define NODE_SIZE_NODES		5
#define NODE_SIZE_CPUS		4
#define NODE_SIZE_MEM		6
#define NODE_SIZE_DISK		8
#define NODE_SIZE_WEIGHT	6
#define NODE_SIZE_FEATURES	8
#define NODE_SIZE_NAME		9

#define PART_SIZE_NODES		0
#define PART_SIZE_NUM		5
#define PART_SIZE_PART		9
#define PART_SIZE_STATE		6
#define PART_SIZE_STATE_LONG	11
#define PART_SIZE_AVAIL		5
#define PART_SIZE_TIME		8
#define PART_SIZE_JOB_SIZE	8
#define PART_SIZE_ROOT_ONLY	4
#define PART_SIZE_SHARE		5
#define PART_SIZE_GROUPS	20

/********************
 * Global Variables *
 ********************/
static char *command_name;
struct sinfo_parameters params =
    {	partition:NULL, state_flag:false,
	node_flag:false, nodes:NULL, summarize:false, long_output:false,
	line_wrap:false, verbose:false, iterate:0, exact_match:false
};
static int node_sz_cpus, node_sz_name, node_sz_mem, node_sz_state;
static int node_sz_disk, node_sz_part, node_sz_weight, node_sz_features;
static int node_sz_nodes;
static int part_sz_num, part_sz_nodes, part_sz_part, part_sz_state;
static int part_sz_avail, part_sz_time, part_sz_job_nodes;
static int part_sz_root, part_sz_share, part_size_groups;
static const char dash_line_20[] = "--------------------";
static const char dash_line_40[] = "----------------------------------------";
 
/************
 * Funtions *
 ************/
static int _query_server(partition_info_msg_t ** part_pptr,
			 node_info_msg_t ** node_pptr);


/* Node Functions */
static void _display_all_nodes(node_info_msg_t * node_msg, int node_rec_cnt);
static void _display_node_info_header(void);
static void _display_node_info(List nodes);
static void _filter_nodes(node_info_msg_t *node_msg, int *node_rec_cnt);
static bool _exact_node_match(node_info_t *node1, node_info_t *node2);
static List _group_node_list(node_info_msg_t * msg, int node_rec_cnt);
static void _node_cpus_string_from_list(List nodes, char *buffer);
static void _node_mem_string_from_list(List nodes, char *buffer);
static void _node_disk_string_from_list(List nodes, char *buffer);
static void _node_weight_string_from_list(List nodes, char *buffer);
static int  _node_name_string_from_list(List nodes, char *buffer, 
					int buf_size, int *node_count);
static void _swap_char(char **from, char **to);
static void _swap_node_rec(node_info_t *from_node, node_info_t *to_node);

/* Partition Functions */
static void _display_all_partition_summary(partition_info_msg_t * part_ptr,
					   node_info_msg_t * node_ptr, 
					   int node_rec_cnt);
static void _display_partition_node_info(struct partition_summary
					 *partition);
static void _display_all_partition_info(List partitions);
static void _print_summary_header(void);
static void _display_partition_node_summary(struct partition_summary 
					    *partition);
static void _display_partition_summaries(List partitions);

/* Misc Display functions */
static int  _build_min_max_string(char *buffer, int max, int min, bool range);
static void _print_date(void);
static int  _print_int(int number, int width, bool right);
static int  _print_str(char *number, int width, bool right);
static void _set_node_field_sizes(List nodes);
static void _set_part_field_sizes(void);

/* Display partition functions */
static struct partition_summary *_find_partition_summary(List l, char *name);
static struct node_state_summary *_find_node_state_summary(
		List l, node_info_t *ninfo);
static List _setup_partition_summary(partition_info_msg_t * part_ptr, 
				     node_info_msg_t * node_ptr, 
				     int node_rec_cnt);
static void _print_partition_header(void);


int main(int argc, char *argv[])
{
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	partition_info_msg_t *partition_msg = NULL;
	node_info_msg_t *node_msg = NULL;
	int node_rec_cnt;

	command_name = argv[0];

	log_init("sinfo", opts, SYSLOG_FACILITY_DAEMON, NULL);
	parse_command_line(argc, argv);

	while (1) {
		if (params.iterate
		    && (params.verbose || params.long_output))
			_print_date();

		if (_query_server(&partition_msg, &node_msg) != 0)
			exit(1);

		_filter_nodes(node_msg, &node_rec_cnt);

		if (params.node_flag && (!params.summarize))
			_display_all_nodes(node_msg, node_rec_cnt);
		else
			_display_all_partition_summary(partition_msg,
						       node_msg, node_rec_cnt);

		if (params.iterate) {
			printf("\n");
			sleep(params.iterate);
		} else
			break;
	}

	exit(0);
}

/* download the current server state */
static int
_query_server(partition_info_msg_t ** part_pptr,
	      node_info_msg_t ** node_pptr)
{
	static partition_info_msg_t *old_part_ptr = NULL, *new_part_ptr;
	static node_info_msg_t *old_node_ptr = NULL, *new_node_ptr;
	int error_code;

	if (old_part_ptr) {
		error_code =
		    slurm_load_partitions(old_part_ptr->last_update,
					  &new_part_ptr);
		if (error_code == SLURM_SUCCESS)
			slurm_free_partition_info_msg(old_part_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_part_ptr = old_part_ptr;
		}
	} else
		error_code =
		    slurm_load_partitions((time_t) NULL, &new_part_ptr);
	if (error_code) {
		slurm_perror("slurm_load_part");
		return error_code;
	}


	old_part_ptr = new_part_ptr;
	*part_pptr = new_part_ptr;

	if (old_node_ptr) {
		error_code =
		    slurm_load_node(old_node_ptr->last_update,
				    &new_node_ptr);
		if (error_code == SLURM_SUCCESS)
			slurm_free_node_info_msg(old_node_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_node_ptr = old_node_ptr;
		}
	} else
		error_code = slurm_load_node((time_t) NULL, &new_node_ptr);
	if (error_code) {
		slurm_perror("slurm_load_node");
		return error_code;
	}
	old_node_ptr = new_node_ptr;
	*node_pptr = new_node_ptr;

	return 0;
}

/* Filter the node list based upon user options */
static void _filter_nodes(node_info_msg_t *node_msg, int *node_rec_cnt)
{
	int i, new_rec_cnt = 0;
	hostlist_t hosts = NULL;

	if (((params.nodes == NULL) && 
	     (params.partition 	== NULL) && 
	     (!params.state_flag)) ||
	     params.summarize) {
		/* Nothing to filter out */
		*node_rec_cnt = node_msg->record_count;
		return;
	}

	if (params.nodes)
		hosts = hostlist_create(params.nodes);

	for (i = 0; i < node_msg->record_count; i++) {
		if (params.nodes && hostlist_find
		      (hosts, node_msg->node_array[i].name) == -1)
			continue;
		if (params.partition && strcmp
			 (node_msg->node_array[i].partition,
			  params.partition))
			continue;
		if (params.state_flag && 
		    (node_msg->node_array[i].node_state !=
			    params.state) && 
		    ((node_msg->node_array[i].node_state & 
		      (~NODE_STATE_NO_RESPOND)) != params.state)) 
			continue;
		_swap_node_rec(&node_msg->node_array[i], 
			       &node_msg->node_array[new_rec_cnt]);
		new_rec_cnt++;
	}

	if (hosts)
		hostlist_destroy(hosts);
	*node_rec_cnt = new_rec_cnt;
}

static void _swap_char(char **from, char **to) 
{
	char *tmp;
	tmp   = *to;
	*to   = *from;
 	*from = tmp;
}

/* Swap *char values, just overwrite the numbers in moving node info */
static void _swap_node_rec(node_info_t *from_node, node_info_t *to_node)
{
	if (from_node != to_node) {
		_swap_char(&from_node->name,      &to_node->name);
		_swap_char(&from_node->features,  &to_node->features);
		_swap_char(&from_node->partition, &to_node->partition);
		to_node->node_state	= from_node->node_state;
		to_node->cpus		= from_node->cpus;
		to_node->real_memory	= from_node->real_memory;
		to_node->tmp_disk	= from_node->tmp_disk;
		to_node->weight	= from_node->weight;
	}
}


/*****************************************************************************
 *                        DISPLAY NODE INFO FUNCTIONS
 *****************************************************************************/

static void _display_all_nodes(node_info_msg_t * node_msg, int node_rec_cnt)
{
	List nodes = _group_node_list(node_msg, node_rec_cnt);
	ListIterator i = list_iterator_create(nodes);
	List current;

	_set_node_field_sizes(nodes);
	_display_node_info_header();

	while ((current = list_next(i)) != NULL) {
		_display_node_info(current);
		list_destroy(current);
	}
	list_iterator_destroy(i);
	list_destroy(nodes);
}

static void _set_node_field_sizes(List nodes)
{
	int node_cnt, len;
	char node_names[1024];
	List current;
	ListIterator i = list_iterator_create(nodes);

	node_sz_features  = NODE_SIZE_FEATURES;
	node_sz_part      = NODE_SIZE_PART;
	node_sz_weight    = NODE_SIZE_WEIGHT;
	node_sz_cpus	  = NODE_SIZE_CPUS;
	node_sz_disk	  = NODE_SIZE_DISK;
	node_sz_mem	  = NODE_SIZE_MEM;
	node_sz_nodes     = NODE_SIZE_NODES;
	if (params.long_output)
		node_sz_state	= NODE_SIZE_STATE_LONG;
	else
		node_sz_state	= NODE_SIZE_STATE;

	node_sz_name      = NODE_SIZE_NAME;
	while ((current = list_next(i)) != NULL) {
		_node_name_string_from_list(current, node_names, 
					sizeof(node_names), &node_cnt);
		len = strlen(node_names);
		if (len > node_sz_name)
			node_sz_name = len;
	}
	list_iterator_destroy(i);
}

static void _display_node_info_header()
{
	_print_str("NODE_LIST", node_sz_name, false);
	printf(" ");
	_print_str("NODES", node_sz_nodes, false);
	printf(" ");
	_print_str("PARTITION", node_sz_part, false);
	printf(" ");
	_print_str("STATE", node_sz_state, false);

	if (params.long_output) {
		printf(" ");
		_print_str("CPUS", node_sz_cpus, true);
		printf(" ");
		_print_str("MEMORY", node_sz_mem, true);
		printf(" ");
		_print_str("TMP_DISK", node_sz_disk, true);
		printf(" ");
		_print_str("WEIGHT", node_sz_weight, true);
		printf(" ");
		_print_str("FEATURES", node_sz_features, false);
	}
	printf("\n%s%s\n", dash_line_40, dash_line_40);

}

static void _display_node_info(List nodes)
{
	char node_names[64];
	int node_cnt;
	node_info_t *node = list_peek(nodes);

	_node_name_string_from_list(nodes, node_names, sizeof(node_names), 
					&node_cnt);
	_print_str(node_names, node_sz_name, false);
	printf(" ");
	_print_int(node_cnt, node_sz_nodes, true);
	printf(" ");
	_print_str(node->partition, node_sz_part, false);
	printf(" ");
	if (params.long_output)
		_print_str(node_state_string(node->node_state), 
			   node_sz_state, false);
	else
		_print_str(node_state_string_compact(node->node_state), 
			   node_sz_state, false);

	if (params.long_output) {
		int tmp_feature_size;
		char str_buf[64];

		printf(" ");
		_node_cpus_string_from_list(nodes, str_buf);
		_print_str(str_buf, node_sz_cpus, true);
		printf(" ");
		_node_mem_string_from_list(nodes, str_buf);
		_print_str(str_buf, node_sz_mem, true);
		printf(" ");
		_node_disk_string_from_list(nodes, str_buf);
		_print_str(str_buf, node_sz_disk, true);
		printf(" ");
		_node_weight_string_from_list(nodes, str_buf);
		_print_str(str_buf, node_sz_weight, true);

		printf(" ");
		tmp_feature_size = node_sz_features;
		if (node->features && 
		    (strlen(node->features) > tmp_feature_size))
			tmp_feature_size = 0;
		_print_str(node->features, tmp_feature_size, false);
	}

	printf("\n");
}


/* group similar nodes together, return a list of lists containing nodes 
 * with similar configurations */
static List _group_node_list(node_info_msg_t * msg, int node_rec_cnt)
{
	List node_lists = list_create(NULL);
	node_info_t *nodes = msg->node_array;
	int i;

	for (i = 0; i < node_rec_cnt; i++) {
		ListIterator list_i = NULL;
		List curr_list = NULL;

		list_i = list_iterator_create(node_lists);
		while ((curr_list = list_next(list_i)) != NULL) {
			node_info_t *curr = list_peek(curr_list);

			if ((curr->partition    != NULL) &&
			    (nodes[i].partition != NULL) && 
			    (strcmp(nodes[i].partition, 
				    curr->partition) != 0))
				continue;

			if (!_exact_node_match(curr, &nodes[i]))
				continue;

			list_append(curr_list, &(nodes[i]));
			break;
		}
		list_iterator_destroy(list_i);

		if (curr_list == NULL) {
			List temp = list_create(NULL);
			list_append(temp, &(nodes[i]));
			list_append(node_lists, temp);
		}
	}

	return node_lists;
}

/* Return true if all of the nodes' configurations match */
static bool _exact_node_match(node_info_t *node1, node_info_t *node2)
{
	if (node1->node_state  != node2->node_state)
		return false;
	else if ((!params.exact_match) || (!params.long_output))
		return true;

	if (node1->features && node2->features &&
	    strcmp(node1->features, node2->features))
		return false;
	else if (node1->features != node2->features)
		return false;

	if ((node1->cpus        != node2->cpus)        ||
	    (node1->real_memory != node2->real_memory) ||
	    (node1->tmp_disk    != node2->tmp_disk))
		return false;
	else
		return true;
}

/*****************************************************************************
 *                     DISPLAY PARTITION INFO FUNCTIONS
 *****************************************************************************/

static struct partition_summary *_find_partition_summary(List l, char *name)
{
	ListIterator i;
	struct partition_summary *current = NULL;

	if (name == NULL)
		return current;

	i = list_iterator_create(l);
	while ((current = list_next(i)) != NULL) {
		if (strcmp(current->info->name, name) == 0)
			break;
	}

	list_iterator_destroy(i);
	return current;
}

/* Return a pointer to the node in the supplied list that contains 
 * the same configuration (just node_state at present). If none 
 * found then return NULL */
static struct node_state_summary *
_find_node_state_summary(List l, node_info_t *ninfo)
{
	ListIterator i = list_iterator_create(l);
	struct node_state_summary *current;

	while ((current = list_next(i)) != NULL) {
		if ((params.summarize) ||
		    (ninfo->node_state == current->state))
			break;
	}

	list_iterator_destroy(i);
	return current;
}

/* Construct a list of partitions containing the configuration 
 * of that partition along with configuration about the nodes 
 * in that partition */
static List
_setup_partition_summary(partition_info_msg_t * part_ptr,
			 node_info_msg_t * node_ptr, int node_rec_cnt)
{
	int i = 0;
	List partitions = list_create(NULL);

	/* create a data structure for each partition */
	for (i = 0; i < part_ptr->record_count; i++) {
		struct partition_summary *sum;

		sum = (struct partition_summary *)
		    malloc(sizeof(struct partition_summary));
		sum->info = &part_ptr->partition_array[i];
		sum->states = list_create(NULL);
		list_append(partitions, sum);
	}

	for (i = 0; i < node_rec_cnt; i++) {
		node_info_t *ninfo = &node_ptr->node_array[i];
		struct partition_summary *part_sum;
		struct node_state_summary *node_sum = NULL;

		if (ninfo->partition == NULL) {
			if (params.verbose)
				fprintf(stderr, 
					"Node %s is not in any partition\n\n", 
					ninfo->name);
			continue;
		}
		part_sum = _find_partition_summary(partitions, 
						   ninfo->partition);
		if (part_sum == NULL) {
			/* This should never happen */
			fprintf(stderr, "Couldn't find partition %s", 
				ninfo->partition);
			fprintf(stderr,"Please notify system administators\n");
			continue;
		}

		if ((node_sum = _find_node_state_summary(part_sum->states, 
							 ninfo))) {
			node_sum->state = (enum node_states) ninfo->node_state;
			hostlist_push(node_sum->nodes, ninfo->name);
			node_sum->node_count++;
		} else {
			node_sum = (struct node_state_summary *)
			    malloc(sizeof(struct node_state_summary));
			node_sum->state =
			    (enum node_states) ninfo->node_state;
			node_sum->node_count = 1;
			node_sum->nodes = hostlist_create(ninfo->name);
			list_append(part_sum->states, node_sum);
		}
	}

	return partitions;
}

static void
_display_all_partition_summary(partition_info_msg_t * part_ptr,
			       node_info_msg_t * node_ptr, int node_rec_cnt)
{
	List partitions = _setup_partition_summary(part_ptr, 
						   node_ptr, node_rec_cnt);
	_set_part_field_sizes();
	if (params.summarize)
		_display_partition_summaries(partitions);
	else
		_display_all_partition_info(partitions);
	list_destroy(partitions);
}

static void _set_part_field_sizes(void)
{
	part_sz_part  = PART_SIZE_PART;
	part_sz_num   = PART_SIZE_NUM;
	part_sz_nodes = PART_SIZE_NODES;
	if (params.long_output)
		part_sz_state	= PART_SIZE_STATE_LONG;
	else
		part_sz_state	= PART_SIZE_STATE;
	part_sz_avail		= PART_SIZE_AVAIL;
	part_sz_time		= PART_SIZE_TIME;
	part_sz_job_nodes	= PART_SIZE_JOB_SIZE;
	part_sz_root		= PART_SIZE_ROOT_ONLY;
	part_sz_share		= PART_SIZE_SHARE;
	part_size_groups	= PART_SIZE_GROUPS;
}

/* Formating for partiton display headers... */
static void _print_summary_header(void)
{
	_print_str("PARTITION", part_sz_part, false);
	printf(" ");
	_print_str("AVAIL", part_sz_avail, false);
	printf(" ");
	_print_str("NODES", part_sz_num, true);
	printf(" ");
	_print_str("NODE_LIST", part_sz_nodes, false);
	printf("\n%s%s\n", dash_line_40, dash_line_20);
}

static void
_display_partition_node_summary(struct partition_summary *partition)
{
	int  line_cnt = 0, name_len = 1024;
	char *name_buf, part_name[64];

	ListIterator node_i = list_iterator_create(partition->states);
	struct node_state_summary *state_sum = NULL;

	strcpy(part_name, partition->info->name);
	if (partition->info->default_part)
		strcat(part_name, "*");

	name_buf = malloc(name_len);
	while ((state_sum = list_next(node_i)) != NULL) {
		line_cnt++;
		while ((int)hostlist_ranged_string(state_sum->nodes, 
						name_len, name_buf) < 0) {
			name_len *= 2;
			name_buf = realloc(name_buf, name_len);
		}
		_print_str(part_name, part_sz_part, false);
		printf(" ");
		_print_str(partition->info->state_up ? "UP" : "DOWN", 
			   part_sz_avail, false);
		printf(" ");
		_print_int(state_sum->node_count, part_sz_num, true);
		printf(" ");
		_print_str(name_buf, part_sz_nodes, false);
		printf("\n");
	}
	list_iterator_destroy(node_i);
	free(name_buf);

	if (line_cnt == 0) {
		_print_str(part_name, part_sz_part, false);
		printf(" ");
		_print_str(partition->info->state_up ? "UP" : "DOWN", 
			   part_sz_avail, false);
		printf(" ");
		_print_int(0, part_sz_num, true);
		printf("\n");
	}
}

static void _display_partition_summaries(List partitions)
{
	struct partition_summary *partition;
	ListIterator part_i = list_iterator_create(partitions);

	_print_summary_header();
	while ((partition = list_next(part_i)) != NULL) {
		if (params.partition == NULL
		    || strcmp(partition->info->name,
			      params.partition) == 0)
			_display_partition_node_summary(partition);
	}
	list_iterator_destroy(part_i);
}


/* Formating for partiton display headers... */
static void _print_partition_header(void)
{
	_print_str("PARTITION", part_sz_part, false);
	printf(" ");
	_print_str("AVAIL", part_sz_avail, false);
	printf(" ");
	_print_str("NODES", part_sz_num, true);
	printf(" ");
	_print_str("STATE", part_sz_state, false);
	if (params.long_output) {
		printf(" ");
		_print_str("MAX_TIME", part_sz_time, true);
		printf(" ");
		_print_str("JOB_SIZE", part_sz_job_nodes, true);
		printf(" ");
		_print_str("ROOT", part_sz_root, false);
		printf(" ");
		_print_str("SHARE", part_sz_share, false);
		printf(" ");
		_print_str("GROUPS", part_size_groups, false);
	}
	printf(" ");
	_print_str("NODE_LIST", part_sz_nodes, false);
	if (params.long_output)
		printf("\n%s%s%s\n", dash_line_40, dash_line_40, dash_line_20);
	else
		printf("\n%s%s\n", dash_line_40, dash_line_40);
}

static void _display_all_partition_info(List partitions)
{
	struct partition_summary *partition;
	ListIterator part_i = list_iterator_create(partitions);

	_print_partition_header();
	while ((partition = list_next(part_i)) != NULL) {
		if (params.partition == NULL
		    || strcmp(partition->info->name,
			      params.partition) == 0)
			_display_partition_node_info(partition);
	}
	list_iterator_destroy(part_i);
}


static void
_display_partition_node_info(struct partition_summary *partition)
{
	char *no_name = "";
	char *name_buf = NULL, part_name[64], part_time[20], part_job_size[30];
	char part_root[10], part_share[10], *part_groups;
	int  line_cnt = 0, name_len = 1024;

	ListIterator node_i = list_iterator_create(partition->states);
	struct node_state_summary *state_sum = NULL;
	char *part_state = partition->info->state_up ? "UP" : "DOWN";
 
	strcpy(part_name, partition->info->name);
	if (partition->info->default_part)
		strcat(part_name, "*");
	if (partition->info->max_time == -1)
		strcpy(part_time, "NONE");
	else
		sprintf(part_time, "%d", partition->info->max_time);
	(void) _build_min_max_string(part_job_size, 
				     partition->info->min_nodes, 
				     partition->info->max_nodes, true);
	if (partition->info->root_only)
		strcpy(part_root, "YES");
	else
		strcpy(part_root, "NO");
	if (partition->info->shared == 2)
		strcpy(part_share, "FORCE");
	else if(partition->info->shared)
		strcpy(part_share, "YES");
	else
		strcpy(part_share, "NO");
	if (partition->info->allow_groups)
		part_groups = partition->info->allow_groups;
	else 
		part_groups = "ALL";

	name_buf = malloc(name_len);
	while ((state_sum = list_next(node_i)) != NULL) {
		line_cnt++;
		while ((int)hostlist_ranged_string(state_sum->nodes, 
						name_len, name_buf) < 0) {
			name_len *= 2;
			name_buf = realloc(name_buf, name_len);
		}

		_print_str(part_name, part_sz_part, false);
		printf(" ");
		_print_str(part_state, part_sz_avail, false);
		printf(" ");
		_print_int(state_sum->node_count, part_sz_num, true);
		printf(" ");
		if (params.long_output)
			_print_str(node_state_string(state_sum->state),
				   part_sz_state, false);
		else
			_print_str(node_state_string_compact(state_sum->state),
				   part_sz_state, false);
		if (params.long_output) {
			printf(" ");
			_print_str(part_time, part_sz_time, true);
			printf(" ");
			_print_str(part_job_size, part_sz_job_nodes, true);
			printf(" ");
			_print_str(part_root, part_sz_root, false);
			printf(" ");
			_print_str(part_share, part_sz_share, false);
			printf(" ");
			_print_str(part_groups, part_size_groups, false);
		}
		printf(" ");
		_print_str(name_buf, part_sz_nodes, false);
		printf("\n");
		strcpy(part_name,	"");
		strcpy(part_time,	"");
		strcpy(part_job_size,	"");
		strcpy(part_root,	"");
		strcpy(part_share,	"");
		part_state  = no_name;
		part_groups = no_name;
	}
	list_iterator_destroy(node_i);

	if (line_cnt == 0) {
		_print_str(part_name, part_sz_part, false);
		printf(" ");
		_print_str(part_state, part_sz_avail, false);
		printf(" ");
		_print_int(0, part_sz_num, true);
		printf(" ");
		_print_str("N/A", part_sz_state, false);
		if (params.long_output) {
			printf(" ");
			_print_str(part_time, part_sz_time, true);
			printf(" ");
			_print_str(part_job_size, part_sz_job_nodes, true);
			printf(" ");
			_print_str(part_root, part_sz_root, false);
			printf(" ");
			_print_str(part_share, part_sz_share, false);
			printf(" ");
			_print_str(part_groups, part_size_groups, false);
		}
		printf(" ");
		_print_str("", part_sz_nodes, false);
		printf("\n");
	}
	free(name_buf);
}

static int _build_min_max_string(char *buffer, int min, int max, bool range)
{
	if (max == min)
		return sprintf(buffer, "%d", max);
	else if (range)
		return sprintf(buffer, "%d-%d", min, max);
	else
		return sprintf(buffer, "%d+", min);
}

int _print_str(char *str, int width, bool right)
{
	char format[64];
	int printed = 0;

	if (right == true && width != 0)
		snprintf(format, 64, "%%%ds", width);
	else if (width != 0)
		snprintf(format, 64, "%%.%ds", width);
	else {
		format[0] = '%';
		format[1] = 's';
		format[2] = '\0';
	}
	if ((printed = printf(format, str)) < 0)
		return printed;

	while (printed++ < width)
		printf(" ");

	return printed;


}

int _print_int(int number, int width, bool right)
{
	char buf[32];

	snprintf(buf, 32, "%d", number);
	return _print_str(buf, width, right);
}

static void _print_date(void)
{
	time_t now;

	now = time(NULL);
	printf("%s", ctime(&now));
}


/* _node_name_string_from_list - analyzes a list of node_info_t* and 
 * 	fills in a buffer with the appropriate nodename in a
 *	prefix[001-100] type format.
 * IN nodes - list of node_info_t* to analyze
 * OUT buffer - a char buffer to store the string in
 * IN  buf_size - byte size of buffer
 * OUT node_count - count of nodes found
 * RET 0 on success, -1 on failure
 */
static int _node_name_string_from_list(List nodes, char *buffer,
				int buf_size, int *node_count)
{
	node_info_t *curr_node = NULL;
	ListIterator i = list_iterator_create(nodes);
	hostlist_t list = hostlist_create(NULL);
	int err;

	*node_count = 0;
	while ((curr_node = list_next(i)) != NULL) {
		hostlist_push(list, curr_node->name);
		(*node_count)++;
	}
	list_iterator_destroy(i);

	err = hostlist_ranged_string(list, buf_size, buffer);
	hostlist_destroy(list);
	if (err < 0)
		return -1;
	else
		return 0;
}

/* _node_cpus_string_from_list - analyzes a list of node_info_t* and 
 * 	fills in a buffer with CPU count, "123+" if a range.
 * IN nodes - list of node_info_t* to analyze
 * OUT buffer - a char buffer to store the string in.
 */
static void _node_cpus_string_from_list(List nodes, char *buffer)
{
	node_info_t *curr_node = NULL;
	int min_cpus=0, max_cpus=0;
	bool first = true;
	ListIterator i = list_iterator_create(nodes);

	while ((curr_node = list_next(i)) != NULL) {
		if (first) {
			first = false;
			min_cpus = max_cpus = curr_node->cpus;
		} 
		else if (min_cpus > curr_node->cpus)
			min_cpus = curr_node->cpus;
		else if (max_cpus < curr_node->cpus)
			max_cpus = curr_node->cpus;
	}
	list_iterator_destroy(i);
	if (first)
		strcpy(buffer, "");
	else
		_build_min_max_string(buffer, min_cpus, max_cpus, false);
}

/* _node_mem_string_from_list - analyzes a list of node_info_t* and 
 * 	fills in a buffer with real memory size, "123+" if a range.
 * IN nodes - list of node_info_t* to analyze
 * OUT buffer - a char buffer to store the string in.
 */
static void _node_mem_string_from_list(List nodes, char *buffer)
{
	node_info_t *curr_node = NULL;
	int min_mem=0, max_mem=0;
	bool first = true;
	ListIterator i = list_iterator_create(nodes);

	while ((curr_node = list_next(i)) != NULL) {
		if (first) {
			first = false;
			min_mem = max_mem = curr_node->real_memory;
		} 
		else if (min_mem > curr_node->real_memory)
			min_mem = curr_node->real_memory;
		else if (max_mem < curr_node->real_memory)
			max_mem = curr_node->real_memory;
	}
	list_iterator_destroy(i);
	if (first)
		strcpy(buffer, "");
	else
		_build_min_max_string(buffer, min_mem, max_mem, false);
}

/* _node_disk_string_from_list - analyzes a list of node_info_t* and 
 * 	fills in a buffer with temporary disk size, "123+" if a range.
 * IN nodes - list of node_info_t* to analyze
 * OUT buffer - a char buffer to store the string in.
 */
static void _node_disk_string_from_list(List nodes, char *buffer)
{
	node_info_t *curr_node = NULL;
	int min_disk=0, max_disk=0;
	bool first = true;
	ListIterator i = list_iterator_create(nodes);

	while ((curr_node = list_next(i)) != NULL) {
		if (first) {
			first = false;
			min_disk = max_disk = curr_node->tmp_disk;
		} 
		else if (min_disk > curr_node->tmp_disk)
			min_disk = curr_node->tmp_disk;
		else if (max_disk < curr_node->tmp_disk)
			max_disk = curr_node->tmp_disk;
	}
	list_iterator_destroy(i);
	if (first)
		strcpy(buffer, "");
	else
		_build_min_max_string(buffer, min_disk, max_disk, false);
}

/* _node_weight_string_from_list - analyzes a list of node_info_t* and 
 * 	fills in a buffer with their weights, "123+" if a range.
 * IN nodes - list of node_info_t* to analyze
 * OUT buffer - a char buffer to store the string in.
 */
static void _node_weight_string_from_list(List nodes, char *buffer)
{
	node_info_t *curr_node = NULL;
	int min_weight=0, max_weight=0;
	bool first = true;
	ListIterator i = list_iterator_create(nodes);

	while ((curr_node = list_next(i)) != NULL) {
		if (first) {
			first = false;
			min_weight = max_weight = curr_node->weight;
		} 
		else if (min_weight > curr_node->weight)
			min_weight = curr_node->weight;
		else if (min_weight < curr_node->weight)
			max_weight = curr_node->weight;
	}
	list_iterator_destroy(i);
	if (first)
		strcpy(buffer, "");
	else
		_build_min_max_string(buffer, min_weight, max_weight, false);
}

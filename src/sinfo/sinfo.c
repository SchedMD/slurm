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

#define NODE_SIZE_CPUS		4
#define NODE_SIZE_CPUS_LONG	4
#define NODE_SIZE_DISK		8
#define NODE_SIZE_DISK_LONG	8
#define NODE_SIZE_FEATURES	0
#define NODE_SIZE_MEM		6
#define NODE_SIZE_MEM_LONG	6
#define NODE_SIZE_NAME		15
#define NODE_SIZE_PART		10
#define NODE_SIZE_STATE		6
#define NODE_SIZE_STATE_LONG	11
#define NODE_SIZE_WEIGHT	6

#define PART_SIZE_CPUS		4
#define PART_SIZE_CPUS_LONG	6
#define PART_SIZE_DISK		8
#define PART_SIZE_DISK_LONG	15
#define PART_SIZE_MEM		6
#define PART_SIZE_MEM_LONG	11
#define PART_SIZE_NODES		0
#define PART_SIZE_NUM		5
#define PART_SIZE_PART		10
#define PART_SIZE_STATE		6
#define PART_SIZE_STATE_LONG	11

/********************
 * Global Variables *
 ********************/
static char *command_name;
struct sinfo_parameters params =
    {	partition_flag:false, partition:NULL, state_flag:false,
	node_flag:false, node:NULL, summarize:false, long_output:false,
	line_wrap:false, verbose:false, iterate:0, exact_match:false
};
static int node_sz_cpus, node_sz_name, node_sz_mem, node_sz_state;
static int node_sz_disk, node_sz_part, node_sz_weight, node_sz_features;
static int part_sz_num, part_sz_nodes, part_sz_part, part_sz_state;
static int part_sz_cpus, part_sz_disk, part_sz_mem;
static const char equal_string[] = 
    "================================================================================\n";
static const char dash_line[] =
    "--------------------------------------------------------------------------------\n";

/************
 * Funtions *
 ************/
static int _query_server(partition_info_msg_t ** part_pptr,
			 node_info_msg_t ** node_pptr);


/* Node Functions */
static void _display_all_nodes(node_info_msg_t * node_msg, int node_rec_cnt);
static void _display_node_info_header(void);
static void _display_nodes_list(List nodes);
static void _display_nodes_list_long(List nodes);
static void _filter_nodes(node_info_msg_t *node_msg, int *node_rec_cnt);
static List _group_node_list(node_info_msg_t * msg, int node_rec_cnt);
static char *_node_name_string_from_list(List nodes, char *buffer);
static void _swap_char(char *from, char *to);
static void _swap_node_rec(node_info_t *from_node, node_info_t *to_node);

/* Partition Functions */
static void _display_all_partition_info_long(List partitions);
static void _display_all_partition_summary(partition_info_msg_t * part_ptr,
					   node_info_msg_t * node_ptr, 
					   int node_rec_cnt);
static void _display_partition_info_long(struct partition_summary
					 *partition);
static void _display_partition_node_info(struct partition_summary
					 *partition, bool print_name);
static void _display_partition_summaries(List partitions);

/* Misc Display functions */
static int  _build_min_max_string(char *buffer, int max, int min);
static void _print_date(void);
static int  _print_int(int number, int width, bool right);
static int  _print_str(char *number, int width, bool right);
static void _set_node_field_sizes(void);
static void _set_part_field_sizes(void);

/* Display partition functions */
static struct partition_summary *_find_partition_summary(List l, char *name);
static struct node_state_summary *_find_node_state_summary(
		List l, node_info_t *ninfo);
static List _setup_partition_summary(partition_info_msg_t * part_ptr, 
				     node_info_msg_t * node_ptr, 
				     int node_rec_cnt);
static void _print_partition_header(bool no_name);


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

		if (params.node_flag)
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

	if ((params.node	== NULL) && 
	    (params.partition 	== NULL) && 
	    (!params.state_flag)) {
		/* Nothing to filter out */
		*node_rec_cnt = node_msg->record_count;
		return;
	}

	if (params.node)
		hosts = hostlist_create(params.node);

	for (i = 0; i < node_msg->record_count; i++) {
		if (params.node && hostlist_find
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

static void _swap_char(char *from, char *to) 
{
	char *tmp;
	tmp  = to;
	to   = from;
 	from = tmp;
}

/* Swap *char values, just overwrite the numbers in moving node info */
static void _swap_node_rec(node_info_t *from_node, node_info_t *to_node)
{
	_swap_char(from_node->name,      to_node->name);
	_swap_char(from_node->features,  to_node->features);
	_swap_char(from_node->partition, to_node->partition);
	to_node->node_state	= from_node->node_state;
	to_node->cpus		= from_node->cpus;
	to_node->real_memory	= from_node->real_memory;
	to_node->tmp_disk	= from_node->tmp_disk;
	to_node->weight	= from_node->weight;
}


/*****************************************************************************
 *                        DISPLAY NODE INFO FUNCTIONS
 *****************************************************************************/

static void _display_all_nodes(node_info_msg_t * node_msg, int node_rec_cnt)
{
	_set_node_field_sizes();
	_display_node_info_header();

	if (params.long_output == true) {
		List nodes = list_create(NULL);
		int i;

		for (i = 0; i < node_rec_cnt; i++)
			list_append(nodes, &node_msg->node_array[i]);

		_display_nodes_list_long(nodes);
		list_destroy(nodes);
	} else {
		List nodes = _group_node_list(node_msg, node_rec_cnt);
		ListIterator i = list_iterator_create(nodes);
		List current;

		while ((current = list_next(i)) != NULL) {
			_display_nodes_list(current);
			list_destroy(current);
		}
		list_iterator_destroy(i);
		list_destroy(nodes);
	}
}

static void _set_node_field_sizes(void)
{
	node_sz_features  = NODE_SIZE_FEATURES;
	node_sz_name      = NODE_SIZE_NAME;
	node_sz_part      = NODE_SIZE_PART;
	node_sz_weight    = NODE_SIZE_WEIGHT;
	if (params.long_output) {
		node_sz_cpus	= NODE_SIZE_CPUS_LONG;
		node_sz_disk	= NODE_SIZE_DISK_LONG;
		node_sz_mem	= NODE_SIZE_MEM_LONG;
		node_sz_state	= NODE_SIZE_STATE_LONG;
	} else {
		node_sz_cpus	= NODE_SIZE_CPUS;
		node_sz_disk	= NODE_SIZE_DISK;
		node_sz_mem	= NODE_SIZE_MEM;
		node_sz_state	= NODE_SIZE_STATE;
	}
}

static void _display_node_info_header()
{
	_print_str("NODES", node_sz_name, false);
	printf(" ");
	_print_str("STATE", node_sz_state, false);
	printf(" ");
	_print_str("CPUS", node_sz_cpus, true);
	printf(" ");
	_print_str("MEMORY", node_sz_mem, true);
	printf(" ");
	_print_str("TMP_DISK", node_sz_disk, true);
	printf(" ");
	_print_str("WEIGHT", node_sz_weight, true);
	printf(" ");
	_print_str("PARTITION", node_sz_part, false);
	printf(" ");
	_print_str("FEATURES", node_sz_features, false);
	printf("\n");
	printf(dash_line);
}

static void _display_node_info(node_info_t * node, char *name)
{
	_print_str(name, node_sz_name, false);
	printf(" ");
	if (params.long_output)
		_print_str(node_state_string(node->node_state), 
			   node_sz_state, false);
	else
		_print_str(node_state_string_compact(node->node_state), 
			   node_sz_state, false);
	printf(" ");
	_print_int(node->cpus, node_sz_cpus, true);
	printf(" ");
	_print_int(node->real_memory, node_sz_mem, true);
	printf(" ");
	_print_int(node->tmp_disk, node_sz_disk, true);
	printf(" ");
	_print_int(node->weight, node_sz_weight, true);
	printf(" ");
	_print_str(node->partition, node_sz_part, false);
	printf(" ");
	_print_str(node->features, node_sz_features, false);
	printf("\n");
}

static void _display_nodes_list(List nodes)
{
	/*int console_width = atoi( getenv( "COLUMNS" ) ); */
	char node_names[32];
	node_info_t *curr = list_peek(nodes);
	_node_name_string_from_list(nodes, node_names);

	_display_node_info(curr, node_names);
}

static void _display_nodes_list_long(List nodes)
{
	/*int console_width = atoi( getenv( "COLUMNS" ) ); */
	int count = 0;
	ListIterator i = list_iterator_create(nodes);
	node_info_t *curr = NULL;

	while ((curr = list_next(i)) != NULL) {
		if (params.partition != NULL
		    && strcmp(params.partition, curr->partition))
			continue;
		_display_node_info(curr, curr->name);
		count++;
	}
	printf("-- %8d NODES LISTED --\n\n", count);

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
			bool feature_test;
			bool part_test;

			if (curr->features != NULL
			    && nodes[i].features != NULL)
				feature_test =
				    strcmp(nodes[i].features,
					   curr->features) ? false : true;
			else if (curr->features == nodes[i].features)
				feature_test = true;
			else
				feature_test = false;
			if (curr->partition != NULL
			    && nodes[i].partition != NULL)
				part_test =
				    strcmp(nodes[i].partition,
					   curr->partition) ? false : true;
			else if (curr->partition == nodes[i].partition)
				part_test = true;
			else
				part_test = false;

			if (feature_test && part_test &&
			    nodes[i].node_state  == curr->node_state &&
			    nodes[i].cpus        == curr->cpus &&
			    nodes[i].real_memory == curr->real_memory &&
			    nodes[i].tmp_disk    == curr->tmp_disk) {
				list_append(curr_list, &(nodes[i]));
				break;
			}
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

static struct node_state_summary *
_find_node_state_summary(List l, node_info_t *ninfo)
{
	ListIterator i = list_iterator_create(l);
	struct node_state_summary *current;

	while ((current = list_next(i)) != NULL) {
		if (ninfo->node_state != current->state)
			continue;
		if (params.exact_match == 0)
			break;
		if ((ninfo->cpus        == current->cpu_min) &&
		    (ninfo->real_memory == current->ram_min) &&
		    (ninfo->tmp_disk    == current->disk_min))
			break;
	}

	list_iterator_destroy(i);
	return current;
}

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
				printf("Node %s is not in any partition\n\n", 
					ninfo->name);
			continue;
		}
		part_sum = _find_partition_summary(partitions, 
						   ninfo->partition);
		if (part_sum == NULL) {
			/* This should never happen */
			printf
			    ("Couldn't find partition %s, notify system administators\n",
			     ninfo->partition);
			continue;
		}

		if ((node_sum = 
		     _find_node_state_summary(part_sum->states, ninfo))) {
			node_sum->state =
			    (enum node_states) ninfo->node_state;
			node_sum->cpu_max =
			    MAX(ninfo->cpus, node_sum->cpu_max);
			node_sum->cpu_min =
			    MIN(ninfo->cpus, node_sum->cpu_min);
			node_sum->ram_max =
			    MAX(ninfo->real_memory, node_sum->ram_max);
			node_sum->ram_min =
			    MIN(ninfo->real_memory, node_sum->ram_min);
			node_sum->disk_max =
			    MAX(ninfo->tmp_disk, node_sum->disk_max);
			node_sum->disk_min =
			    MIN(ninfo->tmp_disk, node_sum->disk_min);
			hostlist_push(node_sum->nodes, ninfo->name);
			node_sum->node_count++;
		} else {
			node_sum = (struct node_state_summary *)
			    malloc(sizeof(struct node_state_summary));
			node_sum->state =
			    (enum node_states) ninfo->node_state;
			node_sum->cpu_max = ninfo->cpus;
			node_sum->cpu_min = ninfo->cpus;
			node_sum->ram_max = ninfo->real_memory;
			node_sum->ram_min = ninfo->real_memory;
			node_sum->disk_max = ninfo->tmp_disk;
			node_sum->disk_min = ninfo->tmp_disk;
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
	if (params.long_output)
		_display_all_partition_info_long(partitions);
	else
		_display_partition_summaries(partitions);
	list_destroy(partitions);
}

static void _set_part_field_sizes(void)
{
	part_sz_part  = PART_SIZE_PART;
	part_sz_num   = PART_SIZE_NUM;
	part_sz_nodes = PART_SIZE_NODES;
	if (params.long_output) {
		part_sz_cpus	= PART_SIZE_CPUS_LONG;
		part_sz_disk	= PART_SIZE_DISK_LONG;
		part_sz_mem	= PART_SIZE_MEM_LONG;
		part_sz_state	= PART_SIZE_STATE_LONG;
	} else {
		part_sz_cpus	= PART_SIZE_CPUS;
		part_sz_disk	= PART_SIZE_DISK;
		part_sz_mem	= PART_SIZE_MEM;
		part_sz_state	= PART_SIZE_STATE;
	}
}

/* Formating for partiton display headers... */
static void _print_partition_header(bool no_name)
{
	if (no_name == true)
		printf("\t");
	else {
		_print_str("PARTITION", part_sz_part, false);
		printf(" ");
	}
	_print_str("NODES", part_sz_num, true);
	printf(" ");
	_print_str("STATE", part_sz_state, false);
	printf(" ");
	_print_str("CPUS", part_sz_cpus, true);
	printf(" ");
	_print_str("MEMORY", part_sz_mem, true);
	printf(" ");
	_print_str("TMP_DISK", part_sz_disk, true);
	printf(" ");
	_print_str("NODES", part_sz_nodes, false);
	printf("\n");
	if (no_name == true)
		printf("\t%s", dash_line);
	else
		printf("%s", dash_line);
}

static void _display_partition_summaries(List partitions)
{
	struct partition_summary *partition;
	ListIterator part_i = list_iterator_create(partitions);

	_print_partition_header(false);
	while ((partition = list_next(part_i)) != NULL) {
		if (params.partition == NULL
		    || strcmp(partition->info->name,
			      params.partition) == 0)
			_display_partition_node_info(partition, true);
	}
	list_iterator_destroy(part_i);
}


static void
_display_partition_node_info(struct partition_summary *partition,
			     bool print_name)
{
	char *no_name = "";
	char cpu_buf[64];
	char ram_buf[64];
	char disk_buf[64];
	char name_buf[64];


	ListIterator node_i = list_iterator_create(partition->states);
	struct node_state_summary *state_sum = NULL;
	char *part_name = partition->info->name;

	while ((state_sum = list_next(node_i)) != NULL) {
		_build_min_max_string(ram_buf, state_sum->ram_min,
				      state_sum->ram_max);
		_build_min_max_string(disk_buf, state_sum->disk_min,
				      state_sum->disk_max);
		_build_min_max_string(cpu_buf, state_sum->cpu_min,
				      state_sum->cpu_max);
		hostlist_ranged_string(state_sum->nodes, 64, name_buf);

		if (print_name == true) {
			_print_str(part_name, part_sz_part, false);
			printf(" ");
		} else
			printf("\t");

		_print_int(state_sum->node_count, part_sz_num, true);
		printf(" ");
		if (params.long_output)
			_print_str(node_state_string(state_sum->state),
				   part_sz_state, false);
		else
			_print_str(node_state_string_compact(state_sum->state),
				   part_sz_state, false);
		printf(" ");
		_print_str(cpu_buf, part_sz_cpus, true);
		printf(" ");
		_print_str(ram_buf, part_sz_mem, true);
		printf(" ");
		_print_str(disk_buf, part_sz_disk, true);
		printf(" ");
		_print_str(name_buf, part_sz_nodes, false);
		printf("\n");
		part_name = no_name;
	}
	list_iterator_destroy(node_i);
}

static void _display_all_partition_info_long(List partitions)
{
	struct partition_summary *partition;
	ListIterator part_i = list_iterator_create(partitions);

	printf("PARTITION INFORMATION\n");
	while ((partition = list_next(part_i)) != NULL) {
		if (params.partition == NULL
		    || strcmp(partition->info->name,
			      params.partition) == 0)
			_display_partition_info_long(partition);
		printf("\n");
	}
	printf("%s", equal_string);
	list_iterator_destroy(part_i);
}

void _display_partition_info_long(struct partition_summary *partition)
{
	partition_info_t *part = partition->info;

	printf("%s", equal_string);
	printf("%s\n", part->name);
	printf("\tcurrent state     = %s\n",
	       part->state_up ? "UP" : "DOWN");
	printf("\tdefault partition = %s\n",
	       part->default_part ? "YES" : "NO");
	printf("\ttotal nodes       = %d\n", part->total_nodes);
	printf("\ttotal cpus        = %d\n", part->total_cpus);
	if (part->max_time == -1)
		printf("\tmax jobtime       = NONE\n");
	else
		printf("\tmax jobtime       = %d\n", part->max_time);
	printf("\tmax nodes/job     = %d\n", part->max_nodes);
	printf("\troot only         = %s\n",
	       part->root_only ? "YES" : "NO");
	printf("\tshare nodes       = %s\n",
	       part->shared == 2 ? "ALWAYS" : part->shared ? "YES" : "NO");

	printf("\n");
	_print_partition_header(true);
	_display_partition_node_info(partition, false);

}

static int _build_min_max_string(char *buffer, int min, int max)
{
	if (max == min)
		return sprintf(buffer, "%d", max);
	else if (params.long_output)
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
 * OUT buffer - a char buffer to store the string in.
 * RET on success return buffer, NULL on failure
 */

static char *_node_name_string_from_list(List nodes, char *buffer)
{
	node_info_t *curr_node = NULL;
	ListIterator i = list_iterator_create(nodes);
	hostlist_t list = hostlist_create(NULL);

	while ((curr_node = list_next(i)) != NULL)
		hostlist_push(list, curr_node->name);
	list_iterator_destroy(i);

	hostlist_ranged_string(list, 32, buffer);
	hostlist_destroy(list);
	return buffer;
}

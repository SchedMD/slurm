/*****************************************************************************\
 *  read_config.c - read the overall slurm configuration file
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by moe jette <jette1@llnl.gov>.
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

#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <src/common/hostlist.h>
#include <src/common/list.h>
#include <src/common/macros.h>
#include <src/common/parse_spec.h>
#include <src/slurmctld/locks.h>
#include <src/slurmctld/slurmctld.h>

#define BUF_SIZE 1024

int	init_slurm_conf ();
int	parse_config_spec (char *in_line);
int	parse_node_spec (char *in_line);
int	parse_part_spec (char *in_line);

static char highest_node_name[MAX_NAME_LEN] = "";
int node_record_count = 0;

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
slurm_ctl_conf_t slurmctld_conf;
#include <sys/resource.h>
int 
main (int argc, char *argv[]) {
	int error_code;
	char bitmap[128];
	char *partitions[] = { "login", "debug", "batch", "class", "end" };
	struct part_record *part_ptr;
	int cycles, i, found;
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	struct rusage begin_usage, end_usage;

	log_init(argv[0], opts, SYSLOG_FACILITY_DAEMON, NULL);
	if (argc > 3) {
		printf ("usage: %s <in_file> <cnt>\n", argv[0]);
		exit (1);
	}			

	error_code = read_slurm_conf (0);
	if (error_code) {
		printf ("ERROR %d from read_slurm_conf\n", error_code);
		exit (error_code);
	}			

	printf ("ControlMachine=%s\n", slurmctld_conf.control_machine);
	printf ("BackupController=%s\n", bslurmctld_conf.backup_controller);
	printf ("\n");

	for (i = 0; i < node_record_count; i++) {
		if (strlen (node_record_table_ptr[i].name) == 0)
			continue;
		printf ("NodeName=%s ", node_record_table_ptr[i].name);
		printf ("NodeState=%s ",
			node_state_string[node_record_table_ptr[i].
					  node_state]);
		printf ("LastResponse=%ld ",
			(long) node_record_table_ptr[i].last_response);

		printf ("CPUs=%d ", node_record_table_ptr[i].cpus);
		printf ("RealMemory=%d ",
			node_record_table_ptr[i].real_memory);
		printf ("TmpDisk=%d ", node_record_table_ptr[i].tmp_disk);
		printf ("Weight=%d ",
			node_record_table_ptr[i].config_ptr->weight);
		printf ("Feature=%s\n",
			node_record_table_ptr[i].config_ptr->feature);
	}			
	(void) bit_fmt (bitmap, sizeof(bitmap), up_node_bitmap);
	printf ("\nup_node_bitmap  = %s\n", bitmap);
	(void) bit_fmt (bitmap, sizeof(bitmap), idle_node_bitmap);
	printf ("idle_node_bitmap= %s\n\n", bitmap);

	printf ("default_part_name=%s\n", default_part_name);
	found = 0;
	for (i = 0;; i++) {
		if (strcmp (partitions[i], "end") == 0) {
			if (found)
				break;
			part_ptr = default_part_loc;
		}
		else {
			part_ptr =
				list_find_first (part_list, &list_find_part,
						 partitions[i]);
			if (part_ptr == default_part_loc)
				found = 1;
		}		
		if (part_ptr == NULL)
			continue;
		printf ("PartitionName=%s ", part_ptr->name);
		printf ("MaxTime=%d ", part_ptr->max_time);
		printf ("MaxNodes=%d ", part_ptr->max_nodes);
		printf ("Key=%d ", part_ptr->key);
		printf ("State=%d ", part_ptr->state_up);
		printf ("Shared=%d ", part_ptr->shared);
		printf ("Nodes=%s ", part_ptr->nodes);
		printf ("AllowGroups=%s  ", part_ptr->allow_groups);
		printf ("total_nodes=%d ", part_ptr->total_nodes);
		printf ("total_cpus=%d ", part_ptr->total_cpus);
		(void)  bit_fmt (bitmap, sizeof(bitmap), part_ptr->node_bitmap);
		printf ("node_bitmap=%s\n", bitmap);
	}			
	if (argc < 3)
		exit (0);

	cycles = atoi (argv[2]);
	printf ("let's reinitialize the database %d times.\n", cycles);
	if (getrusage (RUSAGE_CHILDREN, &begin_usage))
		printf ("ERROR %d from getrusage\n", errno);
	else if (begin_usage.ru_maxrss == 0) {
		printf ("WARNING: getrusage inopeative, run /bin/ps and check for memory leak\n");
		sleep (5);
	}
	for (i = 0; i < cycles; i++) {
		error_code = read_slurm_conf (0);
		if (error_code) {
			printf ("ERROR %d from read_slurm_conf\n",
				error_code);
			exit (error_code);
		}		
	}			
	if (getrusage (RUSAGE_CHILDREN, &end_usage))
		printf ("error %d from getrusage\n", errno);
	i = (int) (end_usage.ru_maxrss - begin_usage.ru_maxrss);
	if (i > 0) {
		printf ("ERROR: Change in maximum RSS is %d.\n", i);
		error_code = ENOMEM;
	}
	else if (end_usage.ru_maxrss == 0) {
		printf ("WARNING: getrusage inopeative, run /bin/ps and check for memory leak\n");
		sleep (10);
	}

	exit (error_code);
}
#endif


/* 
 * report_leftover - report any un-parsed (non-whitespace) characters on the
 * configuration input line (we over-write parsed characters with whitespace).
 * input: in_line - what is left of the configuration input line.
 *        line_num - line number of the configuration file.
 */
static void
report_leftover (char *in_line, int line_num)
{
	int bad_index, i;

	bad_index = -1;
	for (i = 0; i < strlen (in_line); i++) {
		if (isspace ((int) in_line[i]) || (in_line[i] == '\n'))
			continue;
		bad_index = i;
		break;
	}

	if (bad_index == -1)
		return;
	error ("report_leftover: ignored input on line %d of configuration: %s",
			line_num, &in_line[bad_index]);
	return;
}


/*
 * build_bitmaps - build node bitmaps to define which nodes are in which 
 *    1) partition  2) configuration record  3) up state  4) idle state
 *    also sets values of total_nodes and total_cpus for every partition.
 * output: return - 0 if no error, errno otherwise
 * global: idle_node_bitmap - bitmap record of idle nodes
 *	up_node_bitmap - bitmap records of up nodes
 *	node_record_count - number of nodes in the system
 *	node_record_table_ptr - pointer to global node table
 *	part_list - pointer to global partition list
 */
int 
build_bitmaps () {
	int i, j, error_code;
	char  *this_node_name;
	ListIterator config_record_iterator;	/* for iterating through config_record */
	ListIterator part_record_iterator;	/* for iterating through part_record_list */
	struct config_record *config_record_point;	/* pointer to config_record */
	struct part_record *part_record_point;	/* pointer to part_record */
	struct node_record *node_record_point;	/* pointer to node_record */
	bitstr_t *all_part_node_bitmap;
	hostlist_t host_list;

	error_code = 0;
	last_node_update = time (NULL);
	last_part_update = time (NULL);

	/* initialize the idle and up bitmaps */
	if (idle_node_bitmap)
		bit_free (idle_node_bitmap);
	if (up_node_bitmap)
		bit_free (up_node_bitmap);
	idle_node_bitmap = (bitstr_t *) bit_alloc (node_record_count);
	up_node_bitmap   = (bitstr_t *) bit_alloc (node_record_count);
	if ((idle_node_bitmap == NULL) || (up_node_bitmap == NULL))
		fatal ("bit_alloc memory allocation failure");

	/* initialize the configuration bitmaps */
	config_record_iterator = list_iterator_create (config_list);
	if (config_record_iterator == NULL)
		fatal ("build_bitmaps: list_iterator_create unable to allocate memory");
	
	while ((config_record_point =
	       (struct config_record *) list_next (config_record_iterator))) {
		if (config_record_point->node_bitmap)
			bit_free (config_record_point->node_bitmap);

		config_record_point->node_bitmap = (bitstr_t *) bit_alloc (node_record_count);	
		if (config_record_point->node_bitmap == NULL)
			fatal ("bit_alloc memory allocation failure");
	}			
	list_iterator_destroy (config_record_iterator);

	/* scan all nodes and identify which are up and idle and their configuration */
	for (i = 0; i < node_record_count; i++) {
		if (node_record_table_ptr[i].name[0] == '\0')
			continue;	/* defunct */
		if (node_record_table_ptr[i].node_state == NODE_STATE_IDLE)
			bit_set (idle_node_bitmap, i);
		if ((node_record_table_ptr[i].node_state != NODE_STATE_DOWN) &&
		    ((node_record_table_ptr[i].node_state & NODE_STATE_NO_RESPOND) == 0))
			bit_set (up_node_bitmap, i);
		if (node_record_table_ptr[i].config_ptr)
			bit_set (node_record_table_ptr[i].config_ptr->node_bitmap, i);
	}			

	/* scan partition table and identify nodes in each */
	all_part_node_bitmap = (bitstr_t *) bit_alloc (node_record_count);	
	if (all_part_node_bitmap == NULL)
		fatal ("bit_alloc memory allocation failure");
	part_record_iterator = list_iterator_create (part_list);
	if (part_record_iterator == NULL)
		fatal ("build_bitmaps: list_iterator_create unable to allocate memory");

	while ((part_record_point = 
		(struct part_record *) list_next (part_record_iterator))) {
		if (part_record_point->node_bitmap)
			bit_free (part_record_point->node_bitmap);
		part_record_point->node_bitmap = (bitstr_t *) bit_alloc (node_record_count);	
		if (part_record_point->node_bitmap == NULL)
			fatal ("bit_alloc memory allocation failure");

		/* check for each node in the partition */
		if ((part_record_point->nodes == NULL) ||
		    (part_record_point->nodes[0] == '\0'))
			continue;

		if ( (host_list = hostlist_create (part_record_point->nodes)) == NULL) {
			error ("hostlist_create errno %d on %s", errno, part_record_point->nodes);
			continue;
		}

		while ( (this_node_name = hostlist_shift (host_list)) ) {
			node_record_point = find_node_record (this_node_name);
			if (node_record_point == NULL) {
				error ("build_bitmaps: invalid node name specified %s",
					this_node_name);
				free (this_node_name);
				continue;
			}	
			j = node_record_point - node_record_table_ptr;
			if (bit_test (all_part_node_bitmap, j) == 1) {
				error ("build_bitmaps: node %s defined in more than one partition",
					 this_node_name);
				error ("build_bitmaps: only the first specification is honored");
			}
			else {
				bit_set (part_record_point->node_bitmap, j);
				bit_set (all_part_node_bitmap, j);
				part_record_point->total_nodes++;
				part_record_point->total_cpus += node_record_point->cpus;
				node_record_point->partition_ptr = part_record_point;
			}
			free (this_node_name);
		}	
		hostlist_destroy (host_list);
	}
	list_iterator_destroy (part_record_iterator);
	bit_free (all_part_node_bitmap);
	return error_code;
}


/* 
 * init_slurm_conf - initialize or re-initialize the slurm configuration values.   
 * output: return value - 0 if no error, otherwise an error code
 */
int
init_slurm_conf () {
	int error_code;

	if ((error_code = init_node_conf ()))
		return error_code;

	if ((error_code = init_part_conf ()))
		return error_code;

	if ((error_code = init_job_conf ()))
		return error_code;

	strcpy(highest_node_name, "");
	return 0;
}


/*
 * parse_config_spec - parse the overall configuration specifications, build table and set values
 * output: 0 if no error, error code otherwise
 */
int 
parse_config_spec (char *in_line) 
{
	int error_code;
	int fast_schedule = 0, hash_base = 0, heartbeat_interval = 0, kill_wait = 0;
	int slurmctld_timeout = 0, slurmd_timeout = 0;
	char *backup_controller = NULL, *control_machine = NULL, *epilog = NULL;
	char *prioritize = NULL, *prolog = NULL, *state_save_location = NULL, *tmp_fs = NULL;
	char *slurmctld_port = NULL, *slurmd_port = NULL;
	char *job_credential_private_key = NULL , *job_credential_public_certificate = NULL;
	long first_job_id = 0;
	struct servent *servent;
	struct stat sbuf;

	error_code = slurm_parser(in_line,
		"BackupController=", 's', &backup_controller, 
		"ControlMachine=", 's', &control_machine, 
		"Epilog=", 's', &epilog, 
		"FastSchedule=", 'd', &fast_schedule,
		"FirstJobId=", 'l', &first_job_id,
		"HashBase=", 'd', &hash_base,
		"HeartbeatInterval=", 'd', &heartbeat_interval,
		"KillWait=", 'd', &kill_wait,
		"Prioritize=", 's', &prioritize,
		"Prolog=", 's', &prolog,
		"SlurmctldPort=", 's', &slurmctld_port,
		"SlurmctldTimeout=", 'd', &slurmctld_timeout,
		"SlurmdPort=", 's', &slurmd_port,
		"SlurmdTimeout=", 'd', &slurmd_timeout,
		"StateSaveLocation=", 's', &state_save_location, 
		"TmpFS=", 's', &tmp_fs,
		"JobCredentialPrivateKey=", 's', &job_credential_private_key,
		"JobCredentialPublicCertificate=", 's', &job_credential_public_certificate,
		"END");
	if (error_code)
		return error_code;

	if ( backup_controller ) {
		if ( slurmctld_conf.backup_controller )
			xfree (slurmctld_conf.backup_controller);
		slurmctld_conf.backup_controller = backup_controller;
	}

	if ( control_machine ) {
		if ( slurmctld_conf.control_machine )
			xfree (slurmctld_conf.control_machine);
		slurmctld_conf.control_machine = control_machine;
	}

	if ( epilog ) {
		if ( slurmctld_conf.epilog )
			xfree (slurmctld_conf.epilog);
		slurmctld_conf.epilog = epilog;
	}

	if ( fast_schedule ) 
		slurmctld_conf.fast_schedule = fast_schedule;

	if ( first_job_id ) 
		slurmctld_conf.first_job_id = first_job_id;

	if ( hash_base ) 
		slurmctld_conf.hash_base = hash_base;

	if ( kill_wait ) 
		slurmctld_conf.kill_wait = kill_wait;

	if ( prioritize ) {
		if ( slurmctld_conf.prioritize )
			xfree (slurmctld_conf.prioritize);
		slurmctld_conf.prioritize = prioritize;
	}

	if ( prolog ) {
		if ( slurmctld_conf.prolog )
			xfree (slurmctld_conf.prolog);
		slurmctld_conf.prolog = prolog;
	}

	if ( slurmctld_port ) {
		servent = getservbyname (slurmctld_port, NULL);
		if (servent)
			slurmctld_conf.slurmctld_port   = servent -> s_port;
		else
			slurmctld_conf.slurmctld_port   = strtol (slurmctld_port, (char **) NULL, 10);
		endservent ();
	}

	if ( slurmctld_timeout ) 
		slurmctld_conf.slurmctld_timeout = slurmctld_timeout;

	if ( slurmd_port ) {
		servent = getservbyname (slurmd_port, NULL);
		if (servent)
			slurmctld_conf.slurmd_port   = servent -> s_port;
		else
			slurmctld_conf.slurmd_port   = strtol (slurmd_port, (char **) NULL, 10);
		endservent ();
	}

	if ( slurmd_timeout ) 
		slurmctld_conf.slurmd_timeout = slurmd_timeout;

	if ( state_save_location ) {
		if ( slurmctld_conf.state_save_location )
			xfree (slurmctld_conf.state_save_location);
		slurmctld_conf.state_save_location = state_save_location;
		if (stat (state_save_location, &sbuf) == -1)	/* create directory as needed */
			(void) mkdir2 (state_save_location, 0744);
	}

	if ( tmp_fs ) {
		if ( slurmctld_conf.tmp_fs )
			xfree (slurmctld_conf.tmp_fs);
		slurmctld_conf.tmp_fs = tmp_fs;
	}
	
	if ( job_credential_public_certificate ) {
		if (slurmctld_conf.job_credential_public_certificate)
			xfree (slurmctld_conf.job_credential_public_certificate);
		slurmctld_conf.job_credential_public_certificate = job_credential_public_certificate;
	}

	if ( job_credential_private_key ) {
		if ( slurmctld_conf.job_credential_private_key )
			xfree ( job_credential_private_key ) ;
		slurmctld_conf.job_credential_private_key = job_credential_private_key ;
	}

	return 0;
}


/* 
 * parse_node_spec - parse the node specification (per the configuration file format), 
 *	build table and set values
 * input:  in_line line from the configuration file
 * output: in_line parsed keywords and values replaced by blanks
 *	return - 0 if no error, error code otherwise
 * global: default_config_record - default configuration values for group of nodes
 *	default_node_record - default node configuration values
 */
int
parse_node_spec (char *in_line) {
	char *node_name, *state, *feature, *this_node_name;
	int error_code, first, i;
	int state_val, cpus_val, real_memory_val, tmp_disk_val, weight_val;
	struct node_record *node_record_point;
	struct config_record *config_point = NULL;
	hostlist_t host_list = NULL;

	node_name = state = feature = (char *) NULL;
	cpus_val = real_memory_val = state_val = NO_VAL;
	tmp_disk_val = weight_val = NO_VAL;
	if ((error_code = load_string (&node_name, "NodeName=", in_line)))
		return error_code;
	if (node_name == NULL)
		return 0;	/* no node info */

	error_code = slurm_parser(in_line,
		"Procs=", 'd', &cpus_val, 
		"Feature=", 's', &feature, 
		"RealMemory=", 'd', &real_memory_val, 
		"State=", 's', &state, 
		"TmpDisk=", 'd', &tmp_disk_val, 
		"Weight=", 'd', &weight_val, 
		"END");

	if (error_code)
		goto cleanup;

	if (state != NULL) {
		state_val = NO_VAL;
		for (i = 0; i <= NODE_STATE_END; i++) {
			if (strcmp (node_state_string(i), "END") == 0)
				break;
			if (strcmp (node_state_string(i), state) == 0) {
				state_val = i;
				break;
			}	
		}		
		if (state_val == NO_VAL) {
			error ("parse_node_spec: invalid state %s for node_name %s",
				 state, node_name);
			error_code = EINVAL;
			goto cleanup;
		}	
	}			

	if ( (host_list = hostlist_create (node_name)) == NULL) {
		error ("hostlist_create errno %d on %s", errno, node_name);
		error_code = errno;
		goto cleanup;
	}

	first = 1;
	while ( (this_node_name = hostlist_shift (host_list)) ) {
		if (strcmp (this_node_name, "localhost") == 0) {
			free (this_node_name);
			this_node_name = malloc (128);
			if (this_node_name == NULL)
				fatal ("memory allocation failure");
			getnodename (this_node_name, 128);
		}
		if (strcmp (this_node_name, "DEFAULT") == 0) {
			xfree(node_name);
			node_name = NULL;
			if (cpus_val != NO_VAL)
				default_config_record.cpus = cpus_val;
			if (real_memory_val != NO_VAL)
				default_config_record.real_memory = real_memory_val;
			if (tmp_disk_val != NO_VAL)
				default_config_record.tmp_disk = tmp_disk_val;
			if (weight_val != NO_VAL)
				default_config_record.weight = weight_val;
			if (state_val != NO_VAL)
				default_node_record.node_state = state_val;
			if (feature) {
				if (default_config_record.feature)
					xfree (default_config_record.feature);
				default_config_record.feature = feature;
			}
			free (this_node_name);
			break;
		}

		if (first == 1) {
			first = 0;
			config_point = create_config_record ();
			if (config_point->nodes)
				free(config_point->nodes);	
			config_point->nodes = node_name;
			if (cpus_val != NO_VAL)
				config_point->cpus = cpus_val;
			if (real_memory_val != NO_VAL)
				config_point->real_memory = real_memory_val;
			if (tmp_disk_val != NO_VAL)
				config_point->tmp_disk = tmp_disk_val;
			if (weight_val != NO_VAL)
				config_point->weight = weight_val;
			if (feature) {
				if (config_point->feature)
					xfree (config_point->feature);
				config_point->feature = feature;
			}	
		}	

		if (strcmp (this_node_name, highest_node_name) <= 0)
			node_record_point = find_node_record (this_node_name);
		else {
			strncpy (highest_node_name, this_node_name, MAX_NAME_LEN);
			node_record_point = NULL;
		}

		if (node_record_point == NULL) {
			node_record_point =
				create_node_record (config_point, this_node_name);
			if (state_val != NO_VAL)
				node_record_point->node_state = state_val;
		}
		else {
			error ("parse_node_spec: reconfiguration for node %s ignored.",
				this_node_name);
		}		
		free (this_node_name);
	}

	/* xfree allocated storage */
	if (state)
		xfree(state);
	hostlist_destroy (host_list);
	return error_code;

      cleanup:
	if (node_name)
		xfree(node_name);
	if (feature)
		xfree(feature);
	if (state)
		xfree(state);
	return error_code;
}


/*
 * parse_part_spec - parse the partition specification, build table and set values
 * output: 0 if no error, error code otherwise
 * global: part_list - global partition list pointer
 *	default_part - default parameters for a partition
 */
int 
parse_part_spec (char *in_line) {
	char *allow_groups, *nodes, *partition_name;
	char *default_str, *root_str, *shared_str, *state_str;
	int max_time_val, max_nodes_val, root_val, default_val;
	int state_val, shared_val;
	int error_code;
	struct part_record *part_record_point;

	partition_name = (char *) NULL;
	default_str = shared_str = state_str = (char *) NULL;
	max_time_val = max_nodes_val = root_val = default_val = state_val = shared_val = NO_VAL;

	if ((error_code = load_string (&partition_name, "PartitionName=", in_line)))
		return error_code;
	if (partition_name == NULL)
		return 0;	/* no partition info */

	if (strlen (partition_name) >= MAX_NAME_LEN) {
		error ("parse_part_spec: partition name %s too long\n", partition_name);
		xfree (partition_name);
		return EINVAL;
	}			

	allow_groups = default_str = root_str = nodes = NULL;
	shared_str = state_str = NULL;
	error_code = slurm_parser(in_line,
		"AllowGroups=", 's', &allow_groups, 
		"Default=", 's', &default_str, 
		"RootOnly=", 's', &root_str, 
		"MaxTime=", 'd', &max_time_val, 
		"MaxNodes=", 'd', &max_nodes_val, 
		"Nodes=", 's', &nodes, 
		"Shared=", 's', &shared_str, 
		"State=", 's', &state_str, 
		"END");

	if (error_code) 
		goto cleanup;

	if (default_str) {
		if (strcmp(default_str, "YES") == 0)
			default_val = 1;
		else if (strcmp(default_str, "NO") == 0)
			default_val = 0;
		else {
			error ("update_part: ignored partition %s update, bad state %s",
			    partition_name, default_str);
			error_code = EINVAL;
			goto cleanup;
		}
		xfree (default_str);
		default_str = NULL;
	}

	if (root_str) {
		if (strcmp(root_str, "YES") == 0)
			root_val = 1;
		else if (strcmp(root_str, "NO") == 0)
			root_val = 0;
		else {
			error ("update_part: ignored partition %s update, bad key %s",
			    partition_name, root_str);
			error_code = EINVAL;
			goto cleanup;
		}
		xfree (root_str);
		root_str = NULL;
	}

	if (shared_str) {
		if (strcmp(shared_str, "YES") == 0)
			shared_val = SHARED_YES;
		else if (strcmp(shared_str, "NO") == 0)
			shared_val = SHARED_NO;
		else if (strcmp(shared_str, "FORCE") == 0)
			shared_val = SHARED_FORCE;
		else {
			error ("update_part: ignored partition %s update, bad shared %s",
			    partition_name, shared_str);
			error_code = EINVAL;
			goto cleanup;
		}
		xfree (shared_str);
		shared_str = NULL;
	}

	if (state_str) {
		if (strcmp(state_str, "UP") == 0)
			state_val = 1;
		else if (strcmp(state_str, "DOWN") == 0)
			state_val = 0;
		else {
			error ("update_part: ignored partition %s update, bad state %s",
			    partition_name, state_str);
			error_code = EINVAL;
			goto cleanup;
		}
		xfree (state_str);
		state_str = NULL;
	}

	if (strcmp (partition_name, "DEFAULT") == 0) {
		xfree (partition_name);
		if (max_time_val != NO_VAL)
			default_part.max_time = max_time_val;
		if (max_nodes_val != NO_VAL)
			default_part.max_nodes = max_nodes_val;
		if (root_val != NO_VAL)
			default_part.root_only = root_val;
		if (state_val != NO_VAL)
			default_part.state_up = state_val;
		if (shared_val != NO_VAL)
			default_part.shared = shared_val;
		if (allow_groups) {
			if (default_part.allow_groups)
				xfree (default_part.allow_groups);
			default_part.allow_groups = allow_groups;
		}		
		if (nodes) {
			if (default_part.nodes)
				xfree (default_part.nodes);
			default_part.nodes = nodes;
		}		
		return 0;
	}			

	part_record_point = list_find_first (part_list, &list_find_part, partition_name);
	if (part_record_point == NULL) {
		part_record_point = create_part_record ();
		strcpy (part_record_point->name, partition_name);
	}
	else {
		info ("parse_node_spec: duplicate entry for partition %s", partition_name);
	}			
	if (default_val == 1) {
		if (strlen (default_part_name) > 0)
			info ("parse_part_spec: changing default partition from %s to %s",
				 default_part_name, partition_name);
		strcpy (default_part_name, partition_name);
		default_part_loc = part_record_point;
	}			
	if (max_time_val != NO_VAL)
		part_record_point->max_time = max_time_val;
	if (max_nodes_val != NO_VAL)
		part_record_point->max_nodes = max_nodes_val;
	if (root_val != NO_VAL)
		part_record_point->root_only = root_val;
	if (state_val != NO_VAL)
		part_record_point->state_up = state_val;
	if (shared_val != NO_VAL)
		part_record_point->shared = shared_val;
	if (allow_groups) {
		if (part_record_point->allow_groups)
			xfree (part_record_point->allow_groups);
		part_record_point->allow_groups = allow_groups;
	}			
	if (nodes) {
		if (part_record_point->nodes)
			xfree (part_record_point->nodes);
		if (strcmp (nodes, "localhost") == 0) {
			xfree (nodes);
			nodes = xmalloc (128);
			if (nodes == NULL)
				fatal ("memory allocation failure");
			getnodename (nodes, 128);
		}
		part_record_point->nodes = nodes;
	}			
	xfree (partition_name);
	return 0;

      cleanup:
	if (allow_groups)
		xfree(allow_groups);
	if (default_str)
		xfree(default_str);
	if (root_str)
		xfree(root_str);
	if (nodes)
		xfree(nodes);
	if (partition_name)
		xfree(partition_name);
	if (shared_str)
		xfree(shared_str);
	if (state_str)
		xfree(state_str);
	return error_code;
}


/*
 * read_slurm_conf - load the slurm configuration from the configured file. 
 * read_slurm_conf can be called more than once if so desired.
 * input: recover - set to use state saved from last slurmctld shutdown
 * output: return - 0 if no error, otherwise an error code
 */
int 
read_slurm_conf (int recover) {
	clock_t start_time;
	FILE *slurm_spec_file;	/* pointer to input data file */
	int line_num;		/* line number in input file */
	char in_line[BUF_SIZE];	/* input line */
	int i, j, error_code;
	/* Locks: Write configuration, write job, write node, write partition */
	slurmctld_lock_t config_write_lock = { WRITE_LOCK, WRITE_LOCK, WRITE_LOCK, WRITE_LOCK };

	/* initialization */
	lock_slurmctld (config_write_lock);
	start_time = clock ();
	if ( (error_code = init_slurm_conf ()) ) {
		unlock_slurmctld (config_write_lock);
		return error_code;
	}

	slurm_spec_file = fopen (slurmctld_conf.slurm_conf, "r");
	if (slurm_spec_file == NULL)
		fatal ("read_slurm_conf error %d opening file %s", 
			errno, slurmctld_conf.slurm_conf);

	info ("read_slurm_conf: loading configuration from %s", slurmctld_conf.slurm_conf);

	/* process the data file */
	line_num = 0;
	while (fgets (in_line, BUF_SIZE, slurm_spec_file) != NULL) {
		line_num++;
		if (strlen (in_line) >= (BUF_SIZE - 1)) {
			error ("read_slurm_conf line %d, of input file %s too long\n",
				 line_num, slurmctld_conf.slurm_conf);
			fclose (slurm_spec_file);
			unlock_slurmctld (config_write_lock);
			return E2BIG;
			break;
		}		

		/* everything after a non-escaped "#" is a comment */
		/* replace comment flag "#" with an end of string (NULL) */
		for (i = 0; i < BUF_SIZE; i++) {
			if (in_line[i] == (char) NULL)
				break;
			if (in_line[i] != '#')
				continue;
			if ((i > 0) && (in_line[i - 1] == '\\')) {	/* escaped "#" */
				for (j = i; j < BUF_SIZE; j++) {
					in_line[j - 1] = in_line[j];
				}	
				continue;
			}	
			in_line[i] = (char) NULL;
			break;
		}		

		/* parse what is left */
		
		/* overall configuration parameters */
		if ((error_code = parse_config_spec (in_line))) {
			fclose (slurm_spec_file);
			unlock_slurmctld (config_write_lock);
			return error_code;
		}

		/* node configuration parameters */
		if ((error_code = parse_node_spec (in_line))) {
			fclose (slurm_spec_file);
			unlock_slurmctld (config_write_lock);
			return error_code;
		}		

		/* partition configuration parameters */
		if ((error_code = parse_part_spec (in_line))) {
			fclose (slurm_spec_file);
			unlock_slurmctld (config_write_lock);
			return error_code;
		}		

		/* report any leftover strings on input line */
		report_leftover (in_line, line_num);
	}			
	fclose (slurm_spec_file);

	/* if values not set in configuration file, set defaults */
	if (slurmctld_conf.backup_controller == NULL)
		info ("read_slurm_conf: backup_controller value not specified.");		

	if (slurmctld_conf.control_machine == NULL) {
		fatal ("read_slurm_conf: control_machine value not specified.");
		unlock_slurmctld (config_write_lock);
		return EINVAL;
	}			

	if (default_part_loc == NULL) {
		error ("read_slurm_conf: default partition not set.");
		unlock_slurmctld (config_write_lock);
		return EINVAL;
	}	

	if (node_record_count < 1) {
		error ("read_slurm_conf: no nodes configured.");
		unlock_slurmctld (config_write_lock);
		return EINVAL;
	}	
		
	rehash ();
	set_slurmd_addr ();

	if (recover) {
		(void) load_node_state ();
		(void) load_part_state ();
		(void) load_job_state ();
	}

	if ((error_code = build_bitmaps ())) {
		unlock_slurmctld (config_write_lock);
		return error_code;
	}

	/* sort config_list by weight for scheduling */
	list_sort (config_list, &list_compare_config);

	slurmctld_conf.last_update = time (NULL) ;
	info ("read_slurm_conf: finished loading configuration, time=%ld",
		(long) (clock () - start_time));

	unlock_slurmctld (config_write_lock);
	return SLURM_SUCCESS;
}

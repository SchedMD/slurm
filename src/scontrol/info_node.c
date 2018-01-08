/*****************************************************************************\
 *  info_node.c - node information functions for scontrol.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#include "scontrol.h"

/* Load current node table information into *node_buffer_pptr */
extern int
scontrol_load_nodes (node_info_msg_t ** node_buffer_pptr, uint16_t show_flags)
{
	int error_code;
	static int last_show_flags = 0xffff;
	node_info_msg_t *node_info_ptr = NULL;

	if (old_node_info_ptr) {
		if (last_show_flags != show_flags)
			old_node_info_ptr->last_update = (time_t) 0;
		error_code = slurm_load_node (old_node_info_ptr->last_update,
					      &node_info_ptr, show_flags);
		if (error_code == SLURM_SUCCESS)
			slurm_free_node_info_msg (old_node_info_ptr);
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			node_info_ptr = old_node_info_ptr;
			error_code = SLURM_SUCCESS;
			if (quiet_flag == -1)
				printf ("slurm_load_node no change in data\n");
		}
	}
	else
		error_code = slurm_load_node ((time_t) NULL, &node_info_ptr,
					      show_flags);

	if (error_code == SLURM_SUCCESS) {
		old_node_info_ptr = node_info_ptr;
		last_show_flags = show_flags;
		*node_buffer_pptr = node_info_ptr;
	}

	return error_code;
}

/*
 * scontrol_print_node - print the specified node's information
 * IN node_name - NULL to print all node information
 * IN node_ptr - pointer to node table of information
 * NOTE: call this only after executing load_node, called from
 *	scontrol_print_node_list
 * NOTE: To avoid linear searches, we remember the location of the
 *	last name match
 */
extern void
scontrol_print_node(char *node_name, node_info_msg_t *node_buffer_ptr)
{
	int error_code, i, j, print_cnt = 0;
	static int last_inx = 0;
	partition_info_msg_t *part_info_ptr = NULL;

	error_code = scontrol_load_partitions(&part_info_ptr);
	if (error_code) {
		part_info_ptr = NULL;
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror("slurm_load_partitions error");
	}
	slurm_populate_node_partitions(node_buffer_ptr, part_info_ptr);

	for (j = 0; j < node_buffer_ptr->record_count; j++) {
		if (node_name) {
			i = (j + last_inx) % node_buffer_ptr->record_count;
			if ((node_buffer_ptr->node_array[i].name == NULL) ||
			    xstrcmp (node_name,
				    node_buffer_ptr->node_array[i].name))
				continue;
		} else if (node_buffer_ptr->node_array[j].name == NULL)
			continue;
		else
			i = j;
		print_cnt++;
		slurm_print_node_table (stdout,
					& node_buffer_ptr->node_array[i],
					node_buffer_ptr->node_scaling,
					one_liner);

		if (node_name) {
			last_inx = i;
			break;
		}
	}

	if (print_cnt == 0) {
		if (node_name) {
			exit_code = 1;
			if (quiet_flag != 1)
				printf ("Node %s not found\n", node_name);
		} else if (quiet_flag != 1)
				printf ("No nodes in the system\n");
	}
}


/*
 * scontrol_print_node_list - print information about the supplied node list
 *	(or regular expression)
 * IN node_list - print information about the supplied node list
 *	(or regular expression)
 */
extern void
scontrol_print_node_list (char *node_list)
{
	node_info_msg_t *node_info_ptr = NULL;
	hostlist_t host_list;
	int error_code;
	uint16_t show_flags = 0;
	char *this_node_name;

	if (all_flag)
		show_flags |= SHOW_ALL;
	if (detail_flag)
		show_flags |= SHOW_DETAIL;
	if (future_flag)
		show_flags |= SHOW_FUTURE;

	error_code = scontrol_load_nodes(&node_info_ptr, show_flags);
	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("slurm_load_node error");
		return;
	}

	if (quiet_flag == -1) {
		char time_str[32];
		slurm_make_time_str ((time_t *)&node_info_ptr->last_update,
			             time_str, sizeof(time_str));
		printf ("last_update_time=%s, records=%d\n",
			time_str, node_info_ptr->record_count);
	}

	if (node_list == NULL) {
		scontrol_print_node (NULL, node_info_ptr);
	} else {
		if ((host_list = hostlist_create (node_list))) {
			while ((this_node_name = hostlist_shift (host_list))) {
				scontrol_print_node(this_node_name,
						    node_info_ptr);
				free(this_node_name);
			}

			hostlist_destroy(host_list);
		} else {
			exit_code = 1;
			if (quiet_flag != 1) {
				if (errno == EINVAL) {
					fprintf(stderr,
					        "unable to parse node list %s\n",
					        node_list);
				 } else if (errno == ERANGE) {
					fprintf(stderr,
					        "too many nodes in supplied range %s\n",
					        node_list);
				} else
					perror("error parsing node list");
			}
		}
	}
	return;
}

/*
 * scontrol_print_topo - print the switch topology above the specified node
 * IN node_name - NULL to print all topology information
 */
extern void	scontrol_print_topo (char *node_list)
{
	static topo_info_response_msg_t *topo_info_msg = NULL;
	int i, match, match_cnt = 0;
	hostset_t hs;

	if ((topo_info_msg == NULL) &&
	    slurm_load_topo(&topo_info_msg)) {
		slurm_perror ("slurm_load_topo error");
		return;
	}

	if ((node_list == NULL) || (node_list[0] == '\0')) {
		slurm_print_topo_info_msg(stdout, topo_info_msg, one_liner);
		return;
	}

	/* Search for matching switch name */
	for (i=0; i<topo_info_msg->record_count; i++) {
		if (xstrcmp(topo_info_msg->topo_array[i].name, node_list))
			continue;
		slurm_print_topo_record(stdout, &topo_info_msg->topo_array[i],
					one_liner);
		return;
	}

	/* Search for matching node name */
	for (i=0; i<topo_info_msg->record_count; i++) {
		if ((topo_info_msg->topo_array[i].nodes == NULL) ||
		    (topo_info_msg->topo_array[i].nodes[0] == '\0'))
			continue;
		hs = hostset_create(topo_info_msg->topo_array[i].nodes);
		if (hs == NULL)
			fatal("hostset_create: memory allocation failure");
		match = hostset_within(hs, node_list);
		hostset_destroy(hs);
		if (!match)
			continue;
		match_cnt++;
		slurm_print_topo_record(stdout, &topo_info_msg->topo_array[i],
					one_liner);
	}

	if (match_cnt == 0) {
		error("Topology information contains no switch or "
		      "node named %s", node_list);
	}
}

/*
 * scontrol_print_powercap - print the powercapping related information
 * above the specified node(s)
 * IN node_list - NULL to print the overall powercapping details
 */
extern void	scontrol_print_powercap (char *node_list)
{
	static powercap_info_msg_t *powercap_info_msg = NULL;

	if ((powercap_info_msg == NULL) &&
	    slurm_load_powercap(&powercap_info_msg)) {
		slurm_perror ("slurm_load_powercap error");
		return;
	}

	/* TODO: the case of a particular node list is not yet treated here */
	if ((node_list == NULL) || (node_list[0] == '\0')) {
		slurm_print_powercap_info_msg(stdout, powercap_info_msg,
					      one_liner);
		return;
	}
}

/*
 * Load current front_end table information into *node_buffer_pptr
 */
extern int
scontrol_load_front_end(front_end_info_msg_t ** front_end_buffer_pptr)
{
	int error_code;
	front_end_info_msg_t *front_end_info_ptr = NULL;

	if (old_front_end_info_ptr) {
		error_code = slurm_load_front_end (
				old_front_end_info_ptr->last_update,
				&front_end_info_ptr);
		if (error_code == SLURM_SUCCESS)
			slurm_free_front_end_info_msg (old_front_end_info_ptr);
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			front_end_info_ptr = old_front_end_info_ptr;
			error_code = SLURM_SUCCESS;
			if (quiet_flag == -1) {
				printf("slurm_load_front_end no change in "
				       "data\n");
			}
		}
	}
	else
		error_code = slurm_load_front_end((time_t) NULL,
						  &front_end_info_ptr);

	if (error_code == SLURM_SUCCESS) {
		old_front_end_info_ptr = front_end_info_ptr;
		*front_end_buffer_pptr = front_end_info_ptr;
	}

	return error_code;
}

/*
 * scontrol_print_front_end - print the specified front_end node's information
 * IN node_name - NULL to print all front_end node information
 * IN node_ptr - pointer to front_end node table of information
 * NOTE: call this only after executing load_front_end, called from
 *	scontrol_print_front_end_list
 * NOTE: To avoid linear searches, we remember the location of the
 *	last name match
 */
extern void
scontrol_print_front_end(char *node_name,
			 front_end_info_msg_t  *front_end_buffer_ptr)
{
	int i, j, print_cnt = 0;
	static int last_inx = 0;

	for (j = 0; j < front_end_buffer_ptr->record_count; j++) {
		if (node_name) {
			i = (j + last_inx) % front_end_buffer_ptr->record_count;
			if (!front_end_buffer_ptr->front_end_array[i].name ||
			    xstrcmp(node_name, front_end_buffer_ptr->
				    front_end_array[i].name))
				continue;
		} else if (front_end_buffer_ptr->front_end_array[j].name == NULL)
			continue;
		else
			i = j;
		print_cnt++;
		slurm_print_front_end_table(stdout,
					    &front_end_buffer_ptr->
					    front_end_array[i],
					    one_liner);

		if (node_name) {
			last_inx = i;
			break;
		}
	}

	if (print_cnt == 0) {
		if (node_name) {
			exit_code = 1;
			if (quiet_flag != 1)
				printf ("Node %s not found\n", node_name);
		} else if (quiet_flag != 1)
				printf ("No nodes in the system\n");
	}
}

/*
 * scontrol_print_front_end_list - print information about all front_end nodes
 */
extern void
scontrol_print_front_end_list(char *node_list)
{
	front_end_info_msg_t *front_end_info_ptr = NULL;
	int error_code;
	hostlist_t host_list;
	char *this_node_name;

	error_code = scontrol_load_front_end(&front_end_info_ptr);
	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("slurm_load_front_end error");
		return;
	}

	if (quiet_flag == -1) {
		char time_str[32];
		slurm_make_time_str((time_t *)&front_end_info_ptr->last_update,
			            time_str, sizeof(time_str));
		printf ("last_update_time=%s, records=%d\n",
			time_str, front_end_info_ptr->record_count);
	}

	if (node_list == NULL) {
		scontrol_print_front_end(NULL, front_end_info_ptr);
	} else {
		if ((host_list = hostlist_create (node_list))) {
			while ((this_node_name = hostlist_shift (host_list))) {
				scontrol_print_front_end(this_node_name,
							 front_end_info_ptr);
				free(this_node_name);
			}

			hostlist_destroy(host_list);
		} else {
			exit_code = 1;
			if (quiet_flag != 1) {
				if (errno == EINVAL) {
					fprintf(stderr,
					        "unable to parse node list %s\n",
					        node_list);
				 } else if (errno == ERANGE) {
					fprintf(stderr,
					        "too many nodes in supplied range %s\n",
					        node_list);
				} else
					perror("error parsing node list");
			}
		}
	}

	return;
}

/*****************************************************************************\
 *  info_node.c - node information functions for scontrol.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "scontrol.h"
#include "src/interfaces/data_parser.h"
#include <arpa/inet.h>

extern void scontrol_getaddrs(char *node_list)
{
	slurm_node_alias_addrs_t *alias_addrs = NULL;
	if (!slurm_get_node_alias_addrs(node_list, &alias_addrs)) {
		char *hostname = NULL;
		hostlist_t *host_list = NULL;
		int i = 0;

		if (!(host_list = hostlist_create(alias_addrs->node_list))) {
			error("hostlist_create error for %s: %m",
			      node_list);
			return;
		}

		while ((hostname = hostlist_shift(host_list))) {
			char addrbuf[INET6_ADDRSTRLEN] = {0};
			uint16_t port = 0;
			slurm_addr_t *addr = &alias_addrs->node_addrs[i++];

			slurm_get_ip_str(addr, (char *)addrbuf,
					 INET6_ADDRSTRLEN);
			port = slurm_get_port(addr);

			if (addr->ss_family == AF_INET6)
				printf("%s: [%s]:%d\n",
				       hostname, addrbuf, port);
			else
				printf("%s: %s:%d\n", hostname, addrbuf, port);
			free(hostname);
		}

		hostlist_destroy(host_list);
	}

	slurm_free_node_alias_addrs(alias_addrs);
}

/* Load current node table information into *node_buffer_pptr */
extern int
scontrol_load_nodes (node_info_msg_t ** node_buffer_pptr, uint16_t show_flags)
{
	int error_code;
	static int last_show_flags = 0xffff;
	node_info_msg_t *node_info_ptr = NULL;

	show_flags |= SHOW_MIXED;

	if (old_node_info_ptr) {
		if (last_show_flags != show_flags)
			old_node_info_ptr->last_update = (time_t) 0;
		error_code = slurm_load_node (old_node_info_ptr->last_update,
					      &node_info_ptr, show_flags);
		if (error_code == SLURM_SUCCESS)
			slurm_free_node_info_msg (old_node_info_ptr);
		else if (errno == SLURM_NO_CHANGE_IN_DATA) {
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
	int i, j, print_cnt = 0;
	static int last_inx = 0;

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
extern void scontrol_print_node_list(char *node_list, int argc, char **argv)
{
	node_info_msg_t *node_info_ptr = NULL;
	partition_info_msg_t *part_info_ptr = NULL;
	hostlist_t *host_list = NULL;
	int error_code;
	uint16_t show_flags = 0;

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
		char time_str[256];
		slurm_make_time_str ((time_t *)&node_info_ptr->last_update,
			             time_str, sizeof(time_str));
		printf ("last_update_time=%s, records=%d\n",
			time_str, node_info_ptr->record_count);
	}

	error_code = scontrol_load_partitions(&part_info_ptr);
	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror("slurm_load_partitions error");
		return;
	}
	slurm_populate_node_partitions(node_info_ptr, part_info_ptr);

	if (node_list == NULL) {
		if (mime_type) {
			int rc;
			openapi_resp_node_info_msg_t resp = {
				.nodes = node_info_ptr,
				.last_update = node_info_ptr->last_update,
			};

			DATA_DUMP_CLI(OPENAPI_NODES_RESP, resp, argc, argv,
				      NULL, mime_type, data_parser, rc);

			if (rc)
				exit_code = 1;
		} else {
			scontrol_print_node(NULL, node_info_ptr);
		}
	} else {
		if (!(host_list = hostlist_create(node_list))) {
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

		if (mime_type) {
			int rc, count = 0;
			char *node_name;
			node_info_msg_t msg = {
				.last_update = node_info_ptr->last_update,
			};
			openapi_resp_node_info_msg_t resp = {
				.nodes = &msg,
				.last_update = node_info_ptr->last_update,
			};

			msg.node_array = xcalloc(node_info_ptr->record_count,
						 sizeof(*msg.node_array));

			while ((node_name = hostlist_shift(host_list))) {
				for (int i = 0; i < node_info_ptr->record_count;
				     i++) {
					node_info_t *n =
						&node_info_ptr->node_array[i];

					if (xstrcmp(node_name, n->name))
						continue;

					msg.node_array[count] = *n;
					count++;
					break;
				}

				free(node_name);

				if (count >= node_info_ptr->record_count)
					break;
			}

			msg.record_count = count;

			DATA_DUMP_CLI(OPENAPI_NODES_RESP, resp, argc, argv,
				      NULL, mime_type, data_parser, rc);

			if (rc)
				exit_code = 1;

			xfree(msg.node_array);
		} else {
			char *node_name;

			while ((node_name = hostlist_shift(host_list))) {
				scontrol_print_node(node_name, node_info_ptr);
				free(node_name);
			}
		}

		hostlist_destroy(host_list);
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

	if ((topo_info_msg == NULL) &&
	    slurm_load_topo(&topo_info_msg)) {
		slurm_perror ("slurm_load_topo error");
		return;
	}
	slurm_print_topo_info_msg(stdout, topo_info_msg, node_list, one_liner);
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
		else if (errno == SLURM_NO_CHANGE_IN_DATA) {
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
	hostlist_t *host_list;
	char *this_node_name;

	error_code = scontrol_load_front_end(&front_end_info_ptr);
	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("slurm_load_front_end error");
		return;
	}

	if (quiet_flag == -1) {
		char time_str[256];
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

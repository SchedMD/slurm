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

#include <arpa/inet.h>

#include "src/common/drained_tres.h"
#include "src/common/node_conf.h"
#include "src/common/sercli.h"

#include "src/interfaces/data_parser.h"

#include "scontrol.h"

/*
 * One line of the listing. Indexed TRES that share a name and a drain reason
 * are reported together, however far apart their indexes are, because
 * "scontrol update" acts on all of them with one Index= range list. A count
 * names no index to merge and so always stands alone. An absent device
 * keeps its index and is merged with the other indexed TRES.
 */
typedef struct {
	uint64_t count;
	bitstr_t *index; /* merged indexes, NULL when reported by count */
	bool no_uuid; /* a TRES of this line reported no UUID */
	char *reason; /* borrowed from the record */
	char *tres_name; /* borrowed from the record */
	char *uuids; /* comma separated UUID of every TRES */
} drained_tres_line_t;

typedef struct {
	char *filter;
	list_t *recs; /* borrowed drained_tres_t */
} foreach_drained_copy_t;

typedef struct {
	int found;
	list_t *lines; /* drained_tres_line_t, NULL in long form */
	char *node_name;
	char *tres_filter;
} drained_tres_print_t;

extern void scontrol_getaddrs(char *node_list)
{
	slurm_node_alias_addrs_t *alias_addrs = NULL;
	if (!slurm_get_node_alias_addrs(node_list, &alias_addrs)) {
		char *hostname = NULL;
		hostlist_t *host_list = NULL;
		int i = 0;

		if (!(host_list =
			      hostlist_create_client(alias_addrs->node_list))) {
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
			xfree(hostname);
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
	int error_code = SLURM_SUCCESS;
	uint16_t show_flags = 0;
	data_parser_t *parser = NULL;

	if (mime_type) {
		error_code =
			data_parser_cli_load(&parser, NULL, orig_argc,
					     orig_argv, mime_type, data_parser);
		if (error_code || !parser)
			goto cleanup;
	}

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
		goto cleanup;
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
		goto cleanup;
	}
	slurm_populate_node_partitions(node_info_ptr, part_info_ptr);

	if (node_list == NULL) {
		if (mime_type) {
			openapi_resp_node_info_msg_t resp = {
				.nodes = node_info_ptr,
				.last_update = node_info_ptr->last_update,
			};

			error_code = data_parser_dump_cli_resp(
				DATA_PARSER_OPENAPI_NODES_RESP, &resp,
				sizeof(resp), parser);
		} else {
			scontrol_print_node(NULL, node_info_ptr);
		}
	} else {
		if (!(host_list = hostlist_create_client(node_list))) {
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
			int count = 0;
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

				xfree(node_name);

				if (count >= node_info_ptr->record_count)
					break;
			}

			msg.record_count = count;

			error_code = data_parser_dump_cli_resp(
				DATA_PARSER_OPENAPI_NODES_RESP, &resp,
				sizeof(resp), parser);

			xfree(msg.node_array);
		} else {
			char *node_name;

			while ((node_name = hostlist_shift(host_list))) {
				scontrol_print_node(node_name, node_info_ptr);
				xfree(node_name);
			}
		}

		hostlist_destroy(host_list);
	}

cleanup:
	if (error_code)
		exit_code = 1;
	if (mime_type)
		data_parser_cli_free_ctxt(&parser);
}

/*
 * scontrol_print_topo - print the switch topology above the specified node
 */
extern void scontrol_print_topo(int argc, char **argv)
{
	topo_info_response_msg_t *topo_info_msg = NULL;
	char *name = NULL, *unit = NULL, *node_list = NULL;
	data_parser_t *parser = NULL;
	int rc = SLURM_SUCCESS;

	for (int i = 0; i < argc; ++i) {
		char *tag = argv[i];
		int tag_len = strlen(tag);
		char *val = strchr(argv[i], '=');

		if (val) {
			tag_len = val - argv[i];
			val++;
		} else if (i == 0) {
			name = tag;
			continue;
		}

		if (!val) {
			if (name) {
				fprintf(stderr,
					"Too many arguments (%s), the topology name (%s) is already defined\n",
					tag, name);
				return;
			}
			name = tag;
		} else if (!xstrncasecmp(tag, "node", MAX(tag_len, 1)) ||
			   !xstrncasecmp(tag, "nodes", MAX(tag_len, 1))) {
			node_list = val;
		} else if (!xstrncasecmp(tag, "switch", MAX(tag_len, 1)) ||
			   !xstrncasecmp(tag, "block", MAX(tag_len, 1)) ||
			   !xstrncasecmp(tag, "ring", MAX(tag_len, 1)) ||
			   !xstrncasecmp(tag, "unit", MAX(tag_len, 1))) {
			unit = val;
		} else {
			fprintf(stderr, "Unknown argument %s\n", tag);
			return;
		}
	}

	if (mime_type) {
		rc = data_parser_cli_load(&parser, NULL, orig_argc, orig_argv,
					  mime_type, data_parser);
		if (rc || !parser)
			goto cleanup;
	}

	if (slurm_load_topo(&topo_info_msg, name)) {
		rc = errno ? errno : SLURM_ERROR;
		if (!mime_type) {
			slurm_perror("slurm_load_topo error");
			goto cleanup;
		}

		data_parser_cli_on_error(parser, rc,
					 XSTRINGIFY(slurm_load_topo),
					 "%s() failed to load topology",
					 XSTRINGIFY(slurm_load_topo));
	}

	if (mime_type) {
		int dump_rc = data_parser_dump_cli_single(
			DATA_PARSER_OPENAPI_TOPO_INFO_RESP,
			(topo_info_msg ? topo_info_msg->topo_info : NULL),
			parser);
		if (!rc)
			rc = dump_rc;
	} else {
		slurm_print_topo_info_msg(stdout, topo_info_msg, node_list,
					  unit, one_liner);
	}

cleanup:
	if (rc)
		exit_code = 1;

	slurm_free_topo_info_msg(topo_info_msg);

	if (mime_type)
		data_parser_cli_free_ctxt(&parser);
}

extern void scontrol_print_topo_conf(void)
{
	topo_config_response_msg_t *topo_config_msg = NULL;

	slurm_load_topo_config(&topo_config_msg);
	if (topo_config_msg && topo_config_msg->config) {
		printf("%s", topo_config_msg->config);
	}
	slurm_free_topo_config_msg(topo_config_msg);
}

/*
 * Print one drained TRES, one per line. Uses the same field names as
 * "scontrol update TresName=...", so a line can be pasted back into an update
 * (swapping Reason for Action=Resume) without translating anything. A device
 * that has both an index and a UUID reports both.
 *
 * Both forms of the listing print through here so that the pasteable format is
 * defined in exactly one place.
 */
static void _print_drained_fields(const char *node_name, const char *tres_name,
				  const char *index, const char *uuid,
				  uint64_t count, const char *reason)
{
	printf("NodeName=%s TresName=%s", node_name, tres_name);
	if (index)
		printf(" Index=%s", index);
	if (uuid)
		printf(" Uuid=%s", uuid);
	if (count != NO_VAL64)
		printf(" Count=%" PRIu64, count);
	/* Quoted when needed, so a multi-word reason pastes back intact */
	if (reason && reason[0] && strpbrk(reason, " \t"))
		printf(" Reason=\"%s\"", reason);
	else if (reason && reason[0])
		printf(" Reason=%s", reason);
	printf("\n");
}

/* Print one drained-TRES record, for the long form. */
static void _print_drained_rec(const char *node_name, drained_tres_t *rec)
{
	_print_drained_fields(node_name, rec->tres_name, rec->index, rec->uuid,
			      rec->count, rec->reason);
}

/*
 * A drained record carries the fully typed TRES name, e.g. "gres/gpu:a100",
 * but "scontrol update" accepts the untyped "gres/gpu" for the same devices.
 * Accept either spelling here, so TresName= selects for the listing exactly
 * what it would have acted on for an update.
 */
static bool _tres_filter_matches(char *filter, char *tres_name)
{
	int len;

	if (!filter)
		return true;
	if (!tres_name)
		return false;
	/* Records say "mem"; update also accepts "memory" for it. */
	if (!xstrcasecmp(filter, "memory"))
		filter = "mem";
	if (!xstrcasecmp(filter, tres_name))
		return true;

	/* An untyped filter matches every type of that TRES. */
	len = strlen(filter);
	return (!xstrncasecmp(tres_name, filter, len) &&
		(tres_name[len] == ':'));
}

static void _drained_tres_line_delete(void *x)
{
	drained_tres_line_t *line = x;

	if (!line)
		return;

	/* tres_name and reason are borrowed from the records. */
	xfree(line->uuids);
	FREE_NULL_BITMAP(line->index);
	xfree(line);
}

/*
 * Uuid= takes a list, but the controller checks a paired Index= against every
 * device the UUIDs resolve to. A line whose TRES do not all report a UUID
 * therefore cannot name them all that way, and reporting only some would build
 * a line that is refused when pasted back.
 */
static void _drained_tres_line_add_uuid(drained_tres_line_t *line, char *uuid)
{
	if (line->no_uuid)
		return;

	if (!uuid) {
		line->no_uuid = true;
		xfree(line->uuids);
		return;
	}

	xstrfmtcat(line->uuids, "%s%s", line->uuids ? "," : "", uuid);
}

static int _find_drained_tres_line(void *x, void *arg)
{
	drained_tres_line_t *line = x;
	drained_tres_t *rec = arg;

	return (line->index && !xstrcmp(line->tres_name, rec->tres_name) &&
		!xstrcmp(line->reason, rec->reason));
}

/* Merge rec into the line it belongs to, or start a line for it. */
static void _add_drained_tres_rec(list_t *lines, drained_tres_t *rec)
{
	drained_tres_line_t *line;
	bool indexed = rec->index && rec->index[0];

	if (indexed &&
	    (line = list_find_first(lines, _find_drained_tres_line, rec))) {
		drained_tres_index_add(&line->index, rec->index);
		_drained_tres_line_add_uuid(line, rec->uuid);
		return;
	}

	line = xmalloc(sizeof(*line));
	line->tres_name = rec->tres_name;
	line->reason = rec->reason;
	line->count = rec->count;
	if (indexed) {
		drained_tres_index_add(&line->index, rec->index);
		_drained_tres_line_add_uuid(line, rec->uuid);
	} else if (rec->count == NO_VAL64) {
		/* No index and no count: not a drained line. */
		xfree(line);
		return;
	}

	list_append(lines, line);
}

static int _foreach_print_drained_line(void *x, void *arg)
{
	drained_tres_line_t *line = x;
	char *node_name = arg;
	char *index = line->index ? bit_fmt_full(line->index) : NULL;

	_print_drained_fields(node_name, line->tres_name, index, line->uuids,
			      line->count, line->reason);
	xfree(index);

	return 0;
}

static int _foreach_drained_rec(void *x, void *arg)
{
	drained_tres_t *rec = x;
	drained_tres_print_t *print = arg;

	if (!_tres_filter_matches(print->tres_filter, rec->tres_name))
		return 0;

	print->found++;
	if (print->lines)
		_add_drained_tres_rec(print->lines, rec);
	else
		_print_drained_rec(print->node_name, rec);

	return 0;
}

static int _copy_matching_rec(void *x, void *arg)
{
	drained_tres_t *rec = x;
	foreach_drained_copy_t *copy = arg;

	if (_tres_filter_matches(copy->filter, rec->tres_name))
		list_append(copy->recs, rec);

	return 0;
}

/*
 * Dump the nodes that have a drained TRES through the data_parser, as
 * "scontrol show node --json" does. Each node reports only the drained TRES
 * that TresName= selects, and nodes with none are left out, so the result
 * holds the records the text listing would have printed, but ungrouped, so
 * --exact has no effect. The node records are shallow copies, so only the
 * filtered lists are freed here.
 */
static void _dump_drained_tres(node_info_msg_t *node_info_ptr,
			       hostlist_t *host_list, char *tres_filter,
			       data_parser_t *parser)
{
	node_info_msg_t msg = {
		.last_update = node_info_ptr->last_update,
	};
	openapi_resp_node_info_msg_t resp = {
		.nodes = &msg,
		.last_update = node_info_ptr->last_update,
	};

	msg.node_array = xcalloc(node_info_ptr->record_count + 1,
				 sizeof(*msg.node_array));

	for (int i = 0; i < node_info_ptr->record_count; i++) {
		node_info_t *node_ptr = &node_info_ptr->node_array[i];
		foreach_drained_copy_t copy = {
			.filter = tres_filter,
		};
		list_t *recs;

		if (!node_ptr->name || !node_ptr->drained_tres)
			continue;
		if (host_list &&
		    (hostlist_find(host_list, node_ptr->name) == -1))
			continue;

		/* The records belong to node_ptr; the list only borrows them. */
		recs = copy.recs = list_create(NULL);
		list_for_each(node_ptr->drained_tres, _copy_matching_rec,
			      &copy);
		if (!list_count(recs)) {
			FREE_NULL_LIST(recs);
			continue;
		}

		msg.node_array[msg.record_count] = *node_ptr;
		msg.node_array[msg.record_count].drained_tres = recs;
		msg.record_count++;
	}

	if (data_parser_dump_cli_resp(DATA_PARSER_OPENAPI_NODES_RESP, &resp,
				      sizeof(resp), parser))
		exit_code = 1;

	for (int i = 0; i < msg.record_count; i++)
		FREE_NULL_LIST(msg.node_array[i].drained_tres);
	xfree(msg.node_array);
}

extern void scontrol_print_drained_tres(int argc, char **argv)
{
	drained_tres_print_t print = { 0 };
	node_info_msg_t *node_info_ptr = NULL;
	hostlist_t *host_list = NULL;
	char *node_filter = NULL;
	data_parser_t *parser = NULL;
	uint16_t show_flags = SHOW_DETAIL;

	for (int i = 0; i < argc; i++) {
		char *val = xstrchr(argv[i], '=');
		int tag_len;

		if (!val) {
			exit_code = 1;
			error("Invalid input: %s", argv[i]);
			return;
		}
		tag_len = val - argv[i];
		val++;

		if (!xstrncasecmp(argv[i], "TresName", MAX(tag_len, 4))) {
			print.tres_filter = val;
		} else if (!xstrncasecmp(argv[i], "NodeName",
					 MAX(tag_len, 1))) {
			node_filter = val;
		} else {
			exit_code = 1;
			error("Invalid option: %s", argv[i]);
			return;
		}
	}

	if (all_flag)
		show_flags |= SHOW_ALL;
	if (future_flag)
		show_flags |= SHOW_FUTURE;

	if (mime_type &&
	    (data_parser_cli_load(&parser, NULL, orig_argc, orig_argv,
				  mime_type, data_parser) ||
	     !parser)) {
		exit_code = 1;
		goto cleanup;
	}

	if (scontrol_load_nodes(&node_info_ptr, show_flags)) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror("slurm_load_node error");
		goto cleanup;
	}

	if (node_filter && !(host_list = hostlist_create_client(node_filter))) {
		exit_code = 1;
		error("hostlist_create error for %s: %m", node_filter);
		goto cleanup;
	}

	if (mime_type) {
		_dump_drained_tres(node_info_ptr, host_list, print.tres_filter,
				   parser);
		goto cleanup;
	}

	for (int i = 0; i < node_info_ptr->record_count; i++) {
		node_info_t *node_ptr = &node_info_ptr->node_array[i];

		if (!node_ptr->name || !node_ptr->drained_tres)
			continue;
		if (host_list &&
		    (hostlist_find(host_list, node_ptr->name) == -1))
			continue;

		print.node_name = node_ptr->name;

		/*
		 * TRES sharing a name and a reason are reported as one line,
		 * matching the DrainedTRES field of "scontrol show node".
		 * --exact reports every TRES index on its own line instead.
		 */
		if (!exact_flag)
			print.lines = list_create(_drained_tres_line_delete);

		(void) list_for_each(node_ptr->drained_tres,
				     _foreach_drained_rec, &print);

		if (print.lines) {
			(void) list_for_each(print.lines,
					     _foreach_print_drained_line,
					     node_ptr->name);
			FREE_NULL_LIST(print.lines);
		}
	}

	if (!print.found && (quiet_flag != 1))
		printf("No drained TRES in the system\n");

cleanup:
	FREE_NULL_HOSTLIST(host_list);
	if (mime_type)
		data_parser_cli_free_ctxt(&parser);
}

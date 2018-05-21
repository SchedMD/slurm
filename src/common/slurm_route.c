/*****************************************************************************\
 *  slurm_route.c - route plugin functions.
 *****************************************************************************
 *  Copyright (C) 2014 Bull S. A. S.
 *		Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois.
 *
 *  Written by Rod Schultz <rod.schultz@bull.com>
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

#include "config.h"

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/param.h>		/* MAXPATHLEN */

#include "slurm/slurm.h"

#include "src/common/log.h"
#include "src/common/forward.h"
#include "src/common/node_conf.h"
#include "src/common/plugrack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_route.h"
#include "src/common/timers.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

strong_alias(route_split_hostlist_treewidth,
	     slurm_route_split_hostlist_treewidth);

typedef struct slurm_route_ops {
	int  (*split_hostlist)    (hostlist_t hl,
				   hostlist_t** sp_hl,
				   int* count, uint16_t tree_width);
	int  (*reconfigure)       (void);
	slurm_addr_t* (*next_collector) (bool* is_collector);
	slurm_addr_t* (*next_collector_backup) (void);
} slurm_route_ops_t;

/*
 * Must be synchronized with slurm_route_ops_t above.
 */
static const char *syms[] = {
	"route_p_split_hostlist",
	"route_p_reconfigure",
	"route_p_next_collector",
	"route_p_next_collector_backup"
};

static slurm_route_ops_t ops;
static plugin_context_t	*g_context = NULL;
static pthread_mutex_t g_context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;
static uint32_t debug_flags = 0;
static uint16_t g_tree_width;
static bool this_is_collector = false; /* this node is a collector node */
static slurm_addr_t *msg_collect_node = NULL; /* address of node to aggregate
						 messages from this node */
/* addresses of backup nodes to aggregate messages from this node */
static uint32_t msg_backup_cnt = 0;
static slurm_addr_t **msg_collect_backup  = NULL;

/* _get_all_nodes creates a hostlist containing all the nodes in the
 * node_record_table.
 *
 * Caller must destroy the list.
 */
static hostlist_t _get_all_nodes( void )
{
	int i;
	hostlist_t nodes = hostlist_create(NULL);
	for (i = 0; i < node_record_count; i++) {
		hostlist_push_host(nodes, node_record_table_ptr[i].name);
	}
	return nodes;
}

/*
 * _set_collectors call the split_hostlist API on the all nodes hostlist
 * to set the node to be used as a collector for unsolicited node aggregation.
 *
 * If this node is a forwarding node (first node in any hostlist),
 * then its collector and backup are the ControlMachine and it's backup.
 *
 * Otherwise, we find the hostlist containing this node.
 * The forwarding node in that hostlist becomes a collector, the next node
 * which is not this node becomes the backup.
 * That list is split, we iterate through it and searching for a list in
 * which this node is a forwarding node. If found, we set the collector and
 * backup, else this process is repeated.
 */
static void _set_collectors(char *this_node_name)
{
	slurm_ctl_conf_t *conf;
	hostlist_t  nodes;
	hostlist_t *hll = NULL;
	uint32_t backup_cnt;
	char *parent = NULL, **backup;
	char addrbuf[32];
	int i, j, f = -1;
	int hl_count = 0;
	uint16_t parent_port;
	uint16_t backup_port;
	bool ctldparent = true;
	char *tmp = NULL;

#ifdef HAVE_FRONT_END
	return; /* on a FrontEnd system this would never be useful. */
#endif

	if (!run_in_daemon("slurmd"))
		return; /* Only compute nodes have collectors */

	/*
	 * Set the initial iteration, collector is controller,
	 * full list is split
	 */
	xassert(this_node_name);

	conf = slurm_conf_lock();
	nodes = _get_all_nodes();
	backup_cnt = conf->control_cnt;
	backup = xmalloc(sizeof(char *) * backup_cnt);
	if (conf->slurmctld_addr) {
		parent = strdup(conf->slurmctld_addr);
		backup_cnt = 1;
	} else
		parent = strdup(conf->control_addr[0]);
	for (i = 0; i < backup_cnt; i++) {
		if (conf->control_addr[i])
			backup[i] = xstrdup(conf->control_addr[i]);
		else
			backup[i] = NULL;
	}
	msg_backup_cnt = backup_cnt + 2;
	msg_collect_backup = xmalloc(sizeof(slurm_addr_t *) * msg_backup_cnt);
	parent_port = conf->slurmctld_port;
	backup_port = parent_port;
	slurm_conf_unlock();
	while (1) {
		if (route_g_split_hostlist(nodes, &hll, &hl_count, 0)) {
			error("unable to split forward hostlist");
			goto clean; /* collector addrs remains null */
		}
		/* Find which hostlist contains this node */
		for (i = 0; i < hl_count; i++) {
			f = hostlist_find(hll[i], this_node_name);
			if (f != -1)
				break;
		}
		if (i == hl_count) {
			fatal("ROUTE -- %s not found in node_record_table",
			      this_node_name);
		}
		if (f == 0) {
			/*
			 * we are a forwarded to node,
			 * so our parent is "parent"
			 */
			if (hostlist_count(hll[i]) > 1)
				this_is_collector = true;
			xfree(msg_collect_node);
			msg_collect_node = xmalloc(sizeof(slurm_addr_t));
			if (ctldparent) {
				slurm_set_addr(msg_collect_node, parent_port,
					       parent);
			} else {
				slurm_conf_get_addr(parent, msg_collect_node);
				msg_collect_node->sin_port = htons(parent_port);
			}
			if (debug_flags & DEBUG_FLAG_ROUTE) {
				slurm_print_slurm_addr(msg_collect_node,
						       addrbuf, 32);
				info("ROUTE -- message collector (%s) address is %s",
				     parent, addrbuf);
			}
			msg_backup_cnt = 0;
			xfree(msg_collect_backup[0]);
			for (i = 1; (i < backup_cnt) && backup[i]; i++) {
				msg_backup_cnt = i;
				msg_collect_backup[i-1] =
					xmalloc(sizeof(slurm_addr_t));
				if (ctldparent) {
					slurm_set_addr(msg_collect_backup[i-1],
						       backup_port, backup[i]);
				} else {
					slurm_conf_get_addr(backup[i],
						msg_collect_backup[i-1]);
					msg_collect_backup[i-1]->sin_port =
						htons(backup_port);
				}
				if (debug_flags & DEBUG_FLAG_ROUTE) {
					slurm_print_slurm_addr(
						msg_collect_backup[i-1],
						addrbuf, 32);
					info("ROUTE -- message collector backup[%d] (%s) "
					     "address is %s",
					     i, backup[i], addrbuf);
				}
			}
			if ((i == 1) && (debug_flags & DEBUG_FLAG_ROUTE))
				info("ROUTE -- no message collector backup");
			goto clean;
		}

		/*
		 * We are not a forwarding node, the first node in this list
		 * will split the forward_list.
		 * We also know that the forwarding node is not a controller.
		 *
		 * clean up parent context
		 */
		ctldparent = false;
		hostlist_destroy(nodes);
		nodes = hostlist_copy(hll[i]);
		for (j = 0; j < hl_count; j++) {
			hostlist_destroy(hll[j]);
		}
		xfree(hll);

		/* set our parent, backup, and continue search */
		for (i = 0; i < backup_cnt; i++)
			xfree(backup[i]);
		if (parent)
			free(parent);
		parent = hostlist_shift(nodes);
		tmp = hostlist_nth(nodes, 0);
		backup[0] = xstrdup(tmp);
		free(tmp);
		tmp = NULL;
		if (xstrcmp(backup[0], this_node_name) == 0) {
			xfree(backup[0]);
			if (hostlist_count(nodes) > 1) {
				tmp = hostlist_nth(nodes, 1);
				backup[0] = xstrdup(tmp);
				free(tmp);
				tmp = NULL;
			}
		}
		parent_port =  slurm_conf_get_port(parent);
		if (backup[0])
			backup_port = slurm_conf_get_port(backup[0]);
		else
			backup_port = 0;
	}
clean:
	if (debug_flags & DEBUG_FLAG_ROUTE) {
		slurm_print_slurm_addr(msg_collect_node, addrbuf, 32);
		xstrfmtcat(tmp, "ROUTE -- %s is a %s node (parent:%s",
			   this_node_name,
			   this_is_collector ? "collector" : "leaf", addrbuf);
		for (i = 0; (i < backup_cnt) && msg_collect_backup[i]; 
		     i++) {
			slurm_print_slurm_addr(msg_collect_backup[i],
					       addrbuf, 32);
			xstrfmtcat(tmp, " backup[%d]:%s", i, addrbuf);
		}
		info("%s)", tmp);
		xfree(tmp);
	}

	hostlist_destroy(nodes);
	if (parent)
		free(parent);
	for (i = 0; i < backup_cnt; i++)
		xfree(backup[i]);
	xfree(backup);
	for (i = 0; i < hl_count; i++) {
		hostlist_destroy(hll[i]);
	}
	xfree(hll);
}

extern int route_init(char *node_name)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "route";
	char *type = NULL;

	if (init_run && g_context)
		return retval;

	slurm_mutex_lock(&g_context_lock);

	if (g_context)
		goto done;

	type = slurm_get_route_plugin();

	g_context = plugin_context_create(
		plugin_type, type, (void **)&ops, syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s", plugin_type, type);
		retval = SLURM_ERROR;
		goto done;
	}

	g_tree_width = slurm_get_tree_width();
	debug_flags = slurm_get_debug_flags();

	init_run = true;
	_set_collectors(node_name);

done:
	slurm_mutex_unlock(&g_context_lock);
	xfree(type);
	return retval;
}

extern int route_fini(void)
{
	int i, rc;

	if (!g_context)
		return SLURM_SUCCESS;

	init_run = false;
	rc = plugin_context_destroy(g_context);
	g_context = NULL;

	xfree(msg_collect_node);
	for (i = 0; i < msg_backup_cnt; i++)
		xfree(msg_collect_backup[i]);
	xfree(msg_collect_backup);
	msg_backup_cnt = 0;

	return rc;
}


/*
 * route_g_split_hostlist - logic to split an input hostlist into
 *                          a set of hostlists to forward to.
 *
 * IN: hl        - hostlist_t   - list of every node to send message to
 *                                will be empty on return which is same behavior
 *                                as similar code replaced in forward.c
 * OUT: sp_hl    - hostlist_t** - the array of hostlists that will be malloced
 * OUT: count    - int*         - the count of created hostlists
 * RET: SLURM_SUCCESS - int
 *
 * Note: created hostlist will have to be freed independently using
 *       hostlist_destroy by the caller.
 * Note: the hostlist_t array will have to be xfree.
 */
extern int route_g_split_hostlist(hostlist_t hl,
				  hostlist_t** sp_hl,
				  int* count, uint16_t tree_width)
{
	int rc;
	int j, nnodes, nnodex;
	char *buf;

	nnodes = nnodex = 0;
	if (route_init(NULL) != SLURM_SUCCESS)
		return SLURM_ERROR;

	if (debug_flags & DEBUG_FLAG_ROUTE) {
		/* nnodes has to be set here as the hl is empty after the
		 * split_hostlise call.  */
		nnodes = hostlist_count(hl);
		buf = hostlist_ranged_string_xmalloc(hl);
		info("ROUTE: split_hostlist: hl=%s tree_width %u",
		     buf, tree_width);
		xfree(buf);
	}

	rc = (*(ops.split_hostlist))(hl, sp_hl, count,
				     tree_width ? tree_width : g_tree_width);
	if (debug_flags & DEBUG_FLAG_ROUTE) {
		/* Sanity check to make sure all nodes in msg list are in
		 * a child list */
		nnodex = 0;
		for (j = 0; j < *count; j++) {
			nnodex += hostlist_count((*sp_hl)[j]);
		}
		if (nnodex != nnodes) {	/* CLANG false positive */
			info("ROUTE: number of nodes in split lists (%d)"
			     " is not equal to number in input list (%d)",
			     nnodex, nnodes);
		}
	}
	return rc;
}

/*
 * route_g_reconfigure - reset during reconfigure
 *
 * RET: SLURM_SUCCESS - int
 */
extern int route_g_reconfigure(void)
{
	if (route_init(NULL) != SLURM_SUCCESS)
		return SLURM_ERROR;
	debug_flags = slurm_get_debug_flags();
	g_tree_width = slurm_get_tree_width();

	return (*(ops.reconfigure))();
}

/*
 * route_g_next_collector - return address of next collector
 *
 * IN: is_collector - bool* - flag indication if this node is a collector
 *
 * RET: slurm_addr_t* - address of node to send messages to be aggregated.
 */
extern slurm_addr_t* route_g_next_collector(bool *is_collector)
{
	if (route_init(NULL) != SLURM_SUCCESS)
		return NULL;

	return (*(ops.next_collector))(is_collector);
}

/*
 * route_g_next_collector_backup
 *
 * RET: slurm_addr_t* - address of backup node to send messages to be aggregated.
 */
extern slurm_addr_t* route_g_next_collector_backup(void)
{
	if (route_init(NULL) != SLURM_SUCCESS)
		return NULL;

	return (*(ops.next_collector_backup))();
}


/*
 * route_split_hostlist_treewidth - logic to split an input hostlist into
 *                                  a set of hostlists to forward to.
 *
 * This is the default behavior. It is implemented here as there are cases
 * where the topology version also needs to split the message list based
 * on TreeWidth.
 *
 * IN: hl        - hostlist_t   - list of every node to send message to
 *                                will be empty on return which is same behavior
 *                                as similar code replaced in forward.c
 * OUT: sp_hl    - hostlist_t** - the array of hostlists that will be malloced
 * OUT: count    - int*         - the count of created hostlists
 * RET: SLURM_SUCCESS - int
 *
 * Note: created hostlist will have to be freed independently using
 *       hostlist_destroy by the caller.
 * Note: the hostlist_t array will have to be xfree.
 */
extern int route_split_hostlist_treewidth(hostlist_t hl,
					  hostlist_t** sp_hl,
					  int* count, uint16_t tree_width)
{
	int host_count;
	int *span = NULL;
	char *name = NULL;
	char *buf;
	int nhl = 0;
	int j;

	if (!tree_width)
		tree_width = g_tree_width;

	host_count = hostlist_count(hl);
	span = set_span(host_count, tree_width);
	*sp_hl = (hostlist_t*) xmalloc(tree_width * sizeof(hostlist_t));

	while ((name = hostlist_shift(hl))) {
		(*sp_hl)[nhl] = hostlist_create(name);
		free(name);
		for (j = 0; j < span[nhl]; j++) {
			name = hostlist_shift(hl);
			if (!name) {
				break;
			}
			hostlist_push_host((*sp_hl)[nhl], name);
			free(name);
		}
		if (debug_flags & DEBUG_FLAG_ROUTE) {
			buf = hostlist_ranged_string_xmalloc((*sp_hl)[nhl]);
			debug("ROUTE: ... sublist[%d] %s", nhl, buf);
			xfree(buf);
		}
		nhl++;
	}
	xfree(span);
	*count = nhl;

	return SLURM_SUCCESS;
}

/*
 * route_next_collector - get collector node address based
 *
 * IN: is_collector - bool* - flag indication if this node is a collector
 *
 * RET: slurm_addr_t* - address of node to send messages to be aggregated.
 */
extern slurm_addr_t* route_next_collector(bool *is_collector)
{
	*is_collector = this_is_collector;
	return msg_collect_node;
}

/*
 * route_next_collector_backup - get collector backup address based on offset
 *
 * backup_inx IN - Backup server index (between 1 and msg_backup_cnt-1)
 * RET: slurm_addr_t* - address of backup node to send messages to be aggregated
 */
extern slurm_addr_t* route_next_collector_backup(int backup_inx)
{
	if ((backup_inx <= 0) || (backup_inx >= msg_backup_cnt))
		return NULL;
	return msg_collect_backup[backup_inx];
}

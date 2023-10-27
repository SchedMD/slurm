/*****************************************************************************\
 *  sackd_mgr.c - sackd (login) node manager
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
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

#include "src/common/fetch_config.h"
#include "src/common/macros.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmctld/agent.h"
#include "src/slurmctld/slurmctld.h"

typedef struct {
	char *hostname;
	char *nodeaddr;
	time_t last_update;
	uint16_t protocol_version;
} sackd_node_t;

static list_t *sackd_nodes = NULL;
static pthread_mutex_t sackd_lock = PTHREAD_MUTEX_INITIALIZER;

static void _destroy_sackd_node(void *x)
{
	sackd_node_t *node = x;

	if (!node)
		return;

	xfree(node->hostname);
	xfree(node->nodeaddr);
	xfree(node);
}

static void _pack_node(void *x, uint16_t protocol_version, buf_t *buffer)
{
	sackd_node_t *node = x;

	pack16(node->protocol_version, buffer);
	pack64(node->last_update, buffer);
	packstr(node->hostname, buffer);
	packstr(node->nodeaddr, buffer);
}

static int _unpack_node(void **x, uint16_t protocol_version, buf_t *buffer)
{
	uint64_t time_tmp;
	sackd_node_t *node = xmalloc(sizeof(*node));

	safe_unpack16(&node->protocol_version, buffer);
	safe_unpack64(&time_tmp, buffer);
	node->last_update = time_tmp;
	safe_unpackstr(&node->hostname, buffer);
	safe_unpackstr(&node->nodeaddr, buffer);

	*x = node;
	return SLURM_SUCCESS;

unpack_error:
	_destroy_sackd_node(node);
	return SLURM_ERROR;
}

static int _find_sackd_node(void *x, void *arg)
{
	sackd_node_t *node = x;
	char *hostname = arg;

	return !xstrcmp(node->hostname, hostname);
}

static void _update_sackd_node(sackd_node_t *node, slurm_msg_t *msg)
{
	slurm_addr_t addr;

	node->last_update = time(NULL);
	node->protocol_version = msg->protocol_version;

        /* Get IP of slurmd */
	xfree(node->nodeaddr);
	if (msg->conn_fd >= 0 && !slurm_get_peer_addr(msg->conn_fd, &addr)) {
		node->nodeaddr = xmalloc(INET6_ADDRSTRLEN);
		slurm_get_ip_str(&addr, node->nodeaddr, INET6_ADDRSTRLEN);
        } else {
		node->nodeaddr = xstrdup(node->hostname);
	}
}

extern void sackd_mgr_dump_state(buf_t *buffer, uint16_t protocol_version)
{
	slurm_mutex_lock(&sackd_lock);
	slurm_pack_list(sackd_nodes, _pack_node, buffer,
			SLURM_PROTOCOL_VERSION);
	debug("%s: saved state of %d nodes",
	      __func__, list_count(sackd_nodes));
	slurm_mutex_unlock(&sackd_lock);
}

extern int sackd_mgr_load_state(buf_t *buffer, uint16_t protocol_version)
{
	int rc;

	slurm_mutex_lock(&sackd_lock);
	FREE_NULL_LIST(sackd_nodes);
	rc = slurm_unpack_list(&sackd_nodes, _unpack_node, _destroy_sackd_node,
			       buffer, protocol_version);
	debug("%s: restored state of %d nodes",
	      __func__, list_count(sackd_nodes));
	slurm_mutex_unlock(&sackd_lock);

	return rc;
}

extern void sackd_mgr_fini(void)
{
	debug("%s", __func__);
	slurm_mutex_lock(&sackd_lock);
	slurm_mutex_unlock(&sackd_lock);
}

extern void sackd_mgr_add_node(slurm_msg_t *msg)
{
	sackd_node_t *node = NULL;
	char *auth_host = auth_g_get_host(msg);

	slurm_mutex_lock(&sackd_lock);
	if (!sackd_nodes)
		sackd_nodes = list_create(_destroy_sackd_node);

	if ((node = list_find_first(sackd_nodes, _find_sackd_node,
				    auth_host))) {
		debug("%s: updating existing record for %s",
		      __func__, auth_host);
		_update_sackd_node(node, msg);
		xfree(auth_host);
	} else {
		debug("%s: adding record for %s",
		      __func__, auth_host);
		node = xmalloc(sizeof(*node));
		node->hostname = auth_host;
		_update_sackd_node(node, msg);
		list_append(sackd_nodes, node);
	}
	slurm_mutex_unlock(&sackd_lock);
}

static int _each_sackd_node(void *x, void *arg)
{
	sackd_node_t *node = x;
	agent_arg_t *args = xmalloc(sizeof(*args));

	args->addr = xmalloc(sizeof(slurm_addr_t));
	slurm_set_addr(args->addr, slurm_conf.slurmd_port, node->nodeaddr);
	args->msg_args = new_config_response(false);
	args->msg_type = REQUEST_RECONFIGURE_SACKD;
	args->hostlist = hostlist_create(node->hostname);
	args->node_count = 1;
	args->protocol_version = node->protocol_version;
	args->retry = 0;
	set_agent_arg_r_uid(args, slurm_conf.slurm_user_id);

	agent_queue_request(args);

	return 0;
}

extern void sackd_mgr_push_reconfig(void)
{
	int count = 0;

	slurm_mutex_lock(&sackd_lock);

	if (!sackd_nodes) {
		slurm_mutex_unlock(&sackd_lock);
		return;
	}

	count = list_for_each(sackd_nodes, _each_sackd_node, NULL);
	debug("%s: triggered reconfig for %d nodes", __func__, count);
	slurm_mutex_unlock(&sackd_lock);
}

extern void sackd_mgr_remove_node(char *node)
{
	debug("%s: removing %s", __func__, node);

	slurm_mutex_lock(&sackd_lock);
	if (sackd_nodes)
		list_delete_first(sackd_nodes, _find_sackd_node, node);
	slurm_mutex_unlock(&sackd_lock);
}

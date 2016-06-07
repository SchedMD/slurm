/*****************************************************************************\
 *  fed_mgr.c - functions for federations
 *****************************************************************************
 *  Copyright (C) 2016 SchedMD LLC.
 *  Written by Brian Christiansen <brian@schedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#include <pthread.h>

#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/fed_mgr.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmdbd/read_config.h"

static pthread_mutex_t fed_mutex = PTHREAD_MUTEX_INITIALIZER;
char *fed_mgr_cluster_name = NULL;
char *fed_mgr_fed_name = NULL;
List fed_mgr_clusters = NULL;

static pthread_t ping_thread = 0;
static bool fed_shutdown = false;

static int _close_controller_conn(slurmdb_cluster_rec_t *conn)
{
	int rc = SLURM_SUCCESS;
	xassert(conn);
	if (conn->sockfd >= 0)
		rc = slurm_close_persist_controller_conn(conn->sockfd);
	conn->sockfd = -1;
	return rc;
}

static int _open_controller_conn(slurmdb_cluster_rec_t *conn)
{
	if (conn->control_host && conn->control_host[0] == '\0')
		conn->sockfd = -1;
	else
		conn->sockfd = slurm_open_persist_controller_conn(conn->control_host,
							  conn->control_port);
	return conn->sockfd;
}

static int _ping_controller(slurmdb_cluster_rec_t *conn)
{
	int rc = SLURM_SUCCESS;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);
	req_msg.msg_type = REQUEST_PING;

	info("pinging %s(%s:%d)", conn->name, conn->control_host, conn->control_port);

	if ((rc = slurm_send_recv_msg(conn->sockfd, &req_msg, &resp_msg, 0))) {
		error("failed to ping %s(%s:%d)",
		      conn->name, conn->control_host, conn->control_port);
		conn->sockfd = -1;
	} else if ((rc = slurm_get_return_code(resp_msg.msg_type, resp_msg.data)))
		error("ping returned error from %s(%s:%d)",
		      conn->name, conn->control_host, conn->control_port);

	return rc;
}

static int _close_fed_conns()
{
	ListIterator itr;
	slurmdb_cluster_rec_t *conn;
	slurm_mutex_lock(&fed_mutex);
	if (!fed_mgr_clusters)
		goto fini;

	itr = list_iterator_create(fed_mgr_clusters);
	while ((conn = list_next(itr))) {
		_close_controller_conn(conn);
	}
	list_iterator_destroy(itr);

fini:
	slurm_mutex_unlock(&fed_mutex);

	return SLURM_SUCCESS;
}

static void *_ping_thread(void *arg)
{
	while(!fed_shutdown) {
		ListIterator itr;
		slurmdb_cluster_rec_t *conn;

		slurm_mutex_lock(&fed_mutex);
		if (!fed_mgr_clusters)
			goto next;

		itr = list_iterator_create(fed_mgr_clusters);

		while ((conn = list_next(itr))) {
			if (conn->sockfd == -1)
				conn->sockfd = _open_controller_conn(conn);
			if (conn->sockfd == -1)
				continue;
			_ping_controller(conn);
		}
		list_iterator_destroy(itr);

next:
		slurm_mutex_unlock(&fed_mutex);

		sleep(5);
	}

	return NULL;
}

static void _create_ping_thread()
{
	static bool first = true;
	pthread_t thread_id;

	if (!first)
		return;

	first = false;

	pthread_attr_t attr;

	slurm_attr_init(&attr);
	if (pthread_create(&thread_id, &attr, _ping_thread, NULL) != 0) {
		error("pthread_create of message thread: %m");
		slurm_attr_destroy(&attr);
		return;
	}
	slurm_attr_destroy(&attr);
}

extern int fed_mgr_init()
{
	static int inited = false;
	if (!inited) {
		_create_ping_thread();
		inited = true;
	}
	return SLURM_SUCCESS;
}

extern int fed_mgr_fini()
{
	fed_shutdown = true;
	_close_fed_conns();
	if (ping_thread)
		pthread_cancel(ping_thread);

	xfree(fed_mgr_cluster_name);
	xfree(fed_mgr_fed_name);
	FREE_NULL_LIST(fed_mgr_clusters);
	return SLURM_SUCCESS;
}

extern int fed_mgr_update_feds(slurmdb_update_object_t *update)
{
	List feds;
	ListIterator f_itr;
	slurmdb_federation_rec_t *fed = NULL;

	if (!update->objects)
		return SLURM_SUCCESS;

	info("Got FEDS");

	feds = update->objects;
	f_itr = list_iterator_create(feds);

	if ((!fed_mgr_cluster_name) && !slurmdbd_conf) {
		xfree(fed_mgr_cluster_name);
		fed_mgr_cluster_name = slurm_get_cluster_name();
	}

	fed_mgr_init();

	slurm_mutex_lock(&fed_mutex);

	/* find the federation that this cluster is in.
	 * if it's changed from last time then do something.
	 * grab other clusters in federation
	 * establish connections with each cluster in federation */

	/* what if a remote cluster is removed from federation.
	 * have to detect that and close the connection to the remote */
	while ((fed = list_next(f_itr))) {
		ListIterator c_itr = list_iterator_create(fed->cluster_list);
		slurmdb_cluster_rec_t *cluster = NULL;

		if (slurmctld_conf.debug_flags & DEBUG_FLAG_DB_FEDR)
			info("Fed:%s Clusters:%d", fed->name,
			     list_count(fed->cluster_list));
		while ((cluster = list_next(c_itr))) {
			if (slurmctld_conf.debug_flags & DEBUG_FLAG_DB_FEDR)
				info("\tCluster:%s", cluster->name);
			if (!xstrcasecmp(cluster->name, fed_mgr_cluster_name)) {
				if (slurmctld_conf.debug_flags &
				    DEBUG_FLAG_DB_FEDR)
					info("I'm part of the '%s' federation!",
					     fed->name);
				fed_mgr_fed_name = xstrdup(fed->name);
				break;
			}
		}

		if (!cluster)
			goto next_fed;

		list_iterator_reset(c_itr);
		/* add clusters from federation into local list */
		if (fed_mgr_clusters) {
			/* close connections to all other clusters */
			/* free cluster list as host and ports may have changed?
			 * */
			ListIterator fed_c_itr =
				list_iterator_create(fed_mgr_clusters);
			slurmdb_cluster_rec_t *conn ;
			while((conn = list_next(fed_c_itr))) {
				_close_controller_conn(conn);
			}
			list_iterator_destroy(fed_c_itr);
			FREE_NULL_LIST(fed_mgr_clusters);
		}

		fed_mgr_clusters = list_create(slurmdb_destroy_cluster_rec);
		while ((cluster = list_next(c_itr))) {
			slurmdb_cluster_rec_t *conn;
			if (!xstrcasecmp(cluster->name, fed_mgr_cluster_name))
				continue;

			conn = xmalloc(sizeof(slurmdb_cluster_rec_t));
			slurmdb_init_cluster_rec(conn, false);
			slurmdb_copy_cluster_rec(conn, cluster);

			conn->sockfd = _open_controller_conn(conn);
			list_append(fed_mgr_clusters, conn);
		}

next_fed:
		list_iterator_destroy(c_itr);
	}
	list_iterator_destroy(f_itr);

	slurm_mutex_unlock(&fed_mutex);

	return SLURM_SUCCESS;
}

extern int fed_mgr_get_fed_info(slurmdb_federation_rec_t **ret_fed)
{
	slurmdb_federation_rec_t tmp_fed;
	slurmdb_federation_rec_t *out_fed;

	xassert(ret_fed);

	out_fed = (slurmdb_federation_rec_t *)
		xmalloc(sizeof(slurmdb_federation_rec_t));
	slurmdb_init_federation_rec(&tmp_fed, false);
	slurmdb_init_federation_rec(out_fed, false);

	slurm_mutex_lock(&fed_mutex);
	tmp_fed.name         = xstrdup(fed_mgr_fed_name);
	tmp_fed.cluster_list = fed_mgr_clusters;

	slurmdb_copy_federation_rec(out_fed, &tmp_fed);
	slurm_mutex_unlock(&fed_mutex);

	xfree(tmp_fed.name);

	*ret_fed = out_fed;

	return SLURM_SUCCESS;
}


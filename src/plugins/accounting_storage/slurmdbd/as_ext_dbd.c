/*****************************************************************************\
 *  as_ext_dbd.c - External Database connections
 *****************************************************************************
 *  Copyright (C) 2011-2020 SchedMD LLC.
 *  Written by Brian Christiansen <brian@schedmd.com>
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

#include "src/common/slurm_accounting_storage.h"

#if HAVE_SYS_PRCTL_H
#  include <sys/prctl.h>
#endif

#include "as_ext_dbd.h"

static List ext_conns_list;
static pthread_t ext_thread_tid = 0;
static int ext_shutdown = 0;

static pthread_mutex_t ext_conns_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_cond_t  ext_thread_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t ext_thread_mutex = PTHREAD_MUTEX_INITIALIZER;

extern void _destroy_external_host_conns(void *object)
{
	slurm_persist_conn_t *conn = (slurm_persist_conn_t *)object;
	xfree(conn->shutdown);
	slurm_persist_conn_destroy(conn);
}

/* don't connect now as it will block the ctld */
extern slurm_persist_conn_t *_create_slurmdbd_conn(char *host, int port)
{
	static char *cluster_name = NULL;
	slurm_persist_conn_t *dbd_conn = NULL;

	if (!cluster_name)
		cluster_name = slurm_get_cluster_name();

	dbd_conn = xmalloc(sizeof(slurm_persist_conn_t));
	dbd_conn->cluster_name = xstrdup(cluster_name);
	dbd_conn->fd = -1;
	dbd_conn->flags = PERSIST_FLAG_DBD;
	dbd_conn->persist_type = PERSIST_TYPE_DBD;
	dbd_conn->rem_host = xstrdup(host);
	dbd_conn->rem_port = port;
	dbd_conn->timeout = -1;
	/* need to manually free as this isn't handled by the destroy function */
	dbd_conn->shutdown = xmalloc(sizeof(time_t));

	return dbd_conn;
}

extern int _find_ext_conn(void *x, void *key)
{
	slurm_persist_conn_t *selected_conn = (slurm_persist_conn_t *)x;
	slurm_persist_conn_t *query_conn = (slurm_persist_conn_t *)key;

	if (!xstrcmp(selected_conn->rem_host, query_conn->rem_host) &&
	    (selected_conn->rem_port == query_conn->rem_port))
		return 1;

	return 0;
}

static void _create_ext_conns(void)
{
	char *ext_hosts = slurm_get_accounting_storage_ext_host();
	char *tok = NULL, *save_ptr = NULL;
	List new_list = list_create(_destroy_external_host_conns);
	slurm_persist_conn_t *old_conn;

	if (ext_hosts)
		tok = strtok_r(ext_hosts, ",", &save_ptr);
	while (ext_hosts && tok) {
		slurm_persist_conn_t *dbd_conn;
		char *colon = xstrstr(tok, ":");
		int port = SLURMDBD_PORT;
		if (colon) {
			*(colon++) = '\0';
			port = strtol(colon, NULL, 10);
		}

		dbd_conn = _create_slurmdbd_conn(tok, port);
		if (dbd_conn)
			list_append(new_list, dbd_conn);

		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(ext_hosts);

	/*
	 * Transfer existing connections to new list so that existing
	 * connections are preserved and old can be removed.
	 */
	if (ext_conns_list) {
		while ((old_conn = list_pop(ext_conns_list))) {
			slurm_persist_conn_t *new_conn;
			if ((new_conn = list_remove_first(new_list,
							  _find_ext_conn,
							  old_conn))) {
				_destroy_external_host_conns(new_conn);
				list_append(new_list, old_conn);
			} else
				_destroy_external_host_conns(old_conn);
		}
	}

	FREE_NULL_LIST(ext_conns_list);
	if (list_count(new_list))
		ext_conns_list = new_list;
	else
		FREE_NULL_LIST(new_list);
}

static int _for_each_check_ext_conn(void *x, void *arg)
{
	bool delete = false;
	slurm_persist_conn_t *dbd_conn = (slurm_persist_conn_t *)x;

	if (slurm_persist_conn_writeable(dbd_conn) == -1) {
		int rc;
		slurm_persist_conn_reopen(dbd_conn, true);

		/* slurm_persist_send_msg will reconnect */
		rc = clusteracct_storage_g_register_ctld(
			dbd_conn, slurmctld_conf.slurmctld_port);
		if (rc == ESLURM_ACCESS_DENIED) {
			error("Not allowed to register to external cluster, not going to try again.");
			delete = true;
		}
	}

	return delete;
}

static void _check_ext_conns()
{
	slurm_mutex_lock(&ext_conns_mutex);
	if (!ext_conns_list) {
		slurm_mutex_unlock(&ext_conns_mutex);
		return;
	}

	/* Use list_delete_all() to be able to delete within the lock */
	list_delete_all(ext_conns_list, _for_each_check_ext_conn, NULL);
	slurm_mutex_unlock(&ext_conns_mutex);
}

static void *_ext_thread(void *x)
{
	struct timespec ts = {0, 0};

#if HAVE_SYS_PRCTL_H
	if (prctl(PR_SET_NAME, "ext_dbd", NULL, NULL, NULL) < 0) {
		error("%s: cannot set my name to %s %m", __func__, "ext_dbd");
	}
#endif

	while (!ext_shutdown) {
		_check_ext_conns();

		ts.tv_sec  = time(NULL) + 5;
		slurm_mutex_lock(&ext_thread_mutex);
		if (!ext_shutdown)
			slurm_cond_timedwait(&ext_thread_cond,
					     &ext_thread_mutex, &ts);
		slurm_mutex_unlock(&ext_thread_mutex);
	}

	return NULL;
}

static void _create_ext_thread(void)
{
	ext_shutdown = 0;

	slurm_mutex_lock(&ext_thread_mutex);
	slurm_thread_create(&ext_thread_tid, _ext_thread, NULL);
	slurm_mutex_unlock(&ext_thread_mutex);
}

static void _destroy_ext_thread(void)
{
	ext_shutdown = 1;

	slurm_mutex_lock(&ext_thread_mutex);
	slurm_cond_broadcast(&ext_thread_cond);
	slurm_mutex_unlock(&ext_thread_mutex);

	if (ext_thread_tid)
		pthread_join(ext_thread_tid,  NULL);
	ext_thread_tid = 0;
}

extern void ext_dbd_init(void)
{
	if (!running_in_slurmctld())
		return;

	slurm_mutex_lock(&ext_conns_mutex);
	_create_ext_conns();
	if (ext_conns_list)
		_create_ext_thread();
	slurm_mutex_unlock(&ext_conns_mutex);
}

extern void ext_dbd_fini(void)
{
	if (!running_in_slurmctld())
		return;

	_destroy_ext_thread();

	slurm_mutex_lock(&ext_conns_mutex);
	FREE_NULL_LIST(ext_conns_list);
	slurm_mutex_unlock(&ext_conns_mutex);
}

extern void ext_dbd_reconfig(void)
{
	bool create = false, destroy = false;

	if (!running_in_slurmctld())
		return;

	slurm_mutex_lock(&ext_conns_mutex);
	_create_ext_conns();
	if (ext_thread_tid && !ext_conns_list)
		destroy = true;
	else if (!ext_thread_tid && ext_conns_list)
		create = true;
	slurm_mutex_unlock(&ext_conns_mutex);

	if (destroy)
		_destroy_ext_thread();
	else if (create)
		_create_ext_thread();
}

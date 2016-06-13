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

#include "config.h"

#include <pthread.h>

#if HAVE_SYS_PRCTL_H
#  include <sys/prctl.h>
#endif

#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/fed_mgr.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmdbd/read_config.h"

#define FED_MGR_STATE_FILE "fed_mgr_state"

static pthread_mutex_t fed_mutex = PTHREAD_MUTEX_INITIALIZER;
char *fed_mgr_cluster_name = NULL;
fed_elem_t fed_mgr_fed_info;
List fed_mgr_siblings = NULL;

static pthread_t ping_thread = 0;
static bool fed_shutdown = false;

static int _close_controller_conn(slurmdb_cluster_rec_t *conn)
{
	int rc = SLURM_SUCCESS;
	xassert(conn);
	slurm_mutex_lock(&conn->lock);
	if (conn->sockfd >= 0)
		rc = slurm_close_persist_controller_conn(conn->sockfd);
	conn->sockfd = -1;
	slurm_mutex_unlock(&conn->lock);

	return rc;
}

static int _open_controller_conn(slurmdb_cluster_rec_t *conn)
{
	slurm_mutex_lock(&conn->lock);
	if (conn->control_host && conn->control_host[0] == '\0')
		conn->sockfd = -1;
	else
		conn->sockfd = slurm_open_persist_controller_conn(conn->control_host,
							  conn->control_port);
	slurm_mutex_unlock(&conn->lock);

	return conn->sockfd;
}

static int _send_recv_msg(slurmdb_cluster_rec_t *conn, slurm_msg_t *req,
			  slurm_msg_t *resp)
{
	int rc;
	slurm_mutex_lock(&conn->lock);
	rc = slurm_send_recv_msg(conn->sockfd, req, resp, 0);
	slurm_mutex_unlock(&conn->lock);

	return rc;
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

	if ((rc = _send_recv_msg(conn, &req_msg, &resp_msg))) {
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
	if (!fed_mgr_siblings)
		goto fini;

	itr = list_iterator_create(fed_mgr_siblings);
	while ((conn = list_next(itr))) {
		if (!xstrcasecmp(conn->name, fed_mgr_cluster_name))
			continue;
		_close_controller_conn(conn);
	}
	list_iterator_destroy(itr);

fini:
	slurm_mutex_unlock(&fed_mutex);

	return SLURM_SUCCESS;
}

static void *_ping_thread(void *arg)
{
#if HAVE_SYS_PRCTL_H
	if (prctl(PR_SET_NAME, "fed_ping", NULL, NULL, NULL) < 0) {
		error("%s: cannot set my name to %s %m", __func__, "fed_ping");
	}
#endif
	while (!slurmctld_config.shutdown_time) {
		ListIterator itr;
		slurmdb_cluster_rec_t *conn;

		slurm_mutex_lock(&fed_mutex);
		if (!fed_mgr_siblings)
			goto next;

		itr = list_iterator_create(fed_mgr_siblings);

		while ((conn = list_next(itr))) {
			if (!xstrcasecmp(conn->name, fed_mgr_cluster_name))
				continue;
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

	if (!first)
		return;

	first = false;

	pthread_attr_t attr;

	slurm_attr_init(&attr);
	if (pthread_create(&ping_thread, &attr, _ping_thread, NULL) != 0) {
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

		if (!fed_mgr_cluster_name)
			fed_mgr_cluster_name = slurm_get_cluster_name();
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
	xfree(fed_mgr_fed_info.name);
	FREE_NULL_LIST(fed_mgr_siblings);
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

	if (!fed_mgr_cluster_name)
		fed_mgr_cluster_name = slurm_get_cluster_name();

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
				memcpy(&fed_mgr_fed_info, &cluster->fed,
				       sizeof(fed_elem_t));
				fed_mgr_fed_info.name =
					xstrdup(cluster->fed.name);
				break;
			}
		}

		if (!cluster)
			goto next_fed;

		list_iterator_reset(c_itr);
		/* add clusters from federation into local list */
		if (fed_mgr_siblings) {
			/* close connections to all other clusters */
			/* free cluster list as host and ports may have changed?
			 * */
			ListIterator fed_c_itr =
				list_iterator_create(fed_mgr_siblings);
			slurmdb_cluster_rec_t *conn ;
			while((conn = list_next(fed_c_itr))) {
				if (!xstrcasecmp(conn->name,
						 fed_mgr_cluster_name))
					continue;
				_close_controller_conn(conn);
			}
			list_iterator_destroy(fed_c_itr);
			FREE_NULL_LIST(fed_mgr_siblings);
		}

		fed_mgr_siblings = list_create(slurmdb_destroy_cluster_rec);
		while ((cluster = list_next(c_itr))) {
			slurmdb_cluster_rec_t *conn;

			conn = xmalloc(sizeof(slurmdb_cluster_rec_t));
			slurmdb_init_cluster_rec(conn, false);
			slurmdb_copy_cluster_rec(conn, cluster);

			conn->sockfd = _open_controller_conn(conn);
			list_append(fed_mgr_siblings, conn);
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
	tmp_fed.name         = fed_mgr_fed_info.name;
	tmp_fed.cluster_list = fed_mgr_siblings;

	slurmdb_copy_federation_rec(out_fed, &tmp_fed);
	slurm_mutex_unlock(&fed_mutex);

	*ret_fed = out_fed;

	return SLURM_SUCCESS;
}


extern int fed_mgr_state_save(char *state_save_location)
{
	int error_code = 0, log_fd;
	char *old_file = NULL, *new_file = NULL, *reg_file = NULL;
	dbd_list_msg_t msg;
	Buf buffer = init_buf(0);

	DEF_TIMERS;

	START_TIMER;

	/* write header: version, time */
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);

	memset(&msg, 0, sizeof(dbd_list_msg_t));
	msg.my_list = fed_mgr_siblings;
	slurmdbd_pack_list_msg(&msg, SLURM_PROTOCOL_VERSION,
			       DBD_ADD_CLUSTERS, buffer);

	/* write the buffer to file */
	reg_file = xstrdup_printf("%s/%s", state_save_location,
				  FED_MGR_STATE_FILE);
	old_file = xstrdup_printf("%s.old", reg_file);
	new_file = xstrdup_printf("%s.new", reg_file);

	log_fd = creat(new_file, 0600);
	if (log_fd < 0) {
		error("Can't save state, create file %s error %m", new_file);
		error_code = errno;
	} else {
		int pos = 0, nwrite = get_buf_offset(buffer), amount;
		char *data = (char *)get_buf_data(buffer);
		while (nwrite > 0) {
			amount = write(log_fd, &data[pos], nwrite);
			if ((amount < 0) && (errno != EINTR)) {
				error("Error writing file %s, %m", new_file);
				error_code = errno;
				break;
			}
			nwrite -= amount;
			pos    += amount;
		}
		fsync(log_fd);
		close(log_fd);
	}
	if (error_code)
		(void) unlink(new_file);
	else {			/* file shuffle */
		(void) unlink(old_file);
		if (link(reg_file, old_file))
			debug4("unable to create link for %s -> %s: %m",
			       reg_file, old_file);
		(void) unlink(reg_file);
		if (link(new_file, reg_file))
			debug4("unable to create link for %s -> %s: %m",
			       new_file, reg_file);
		(void) unlink(new_file);
	}
	xfree(old_file);
	xfree(reg_file);
	xfree(new_file);

	free_buf(buffer);

	END_TIMER2("fed_mgr_state_save");

	return error_code;
}

extern int fed_mgr_state_load(char *state_save_location)
{
	Buf buffer = NULL;
	dbd_list_msg_t *msg = NULL;
	char *data = NULL, *state_file;
	time_t buf_time;
	uint16_t ver = 0;
	uint32_t data_size = 0;
	int state_fd;
	int data_allocated, data_read = 0, error_code = SLURM_SUCCESS;
	slurmdb_cluster_rec_t *cluster = NULL;

	state_file = xstrdup_printf("%s/%s", state_save_location,
				    FED_MGR_STATE_FILE);
	state_fd = open(state_file, O_RDONLY);
	if (state_fd < 0) {
		error("No fed_mgr state file (%s) to recover", state_file);
		xfree(state_file);
		return SLURM_SUCCESS;
	} else {
		data_allocated = BUF_SIZE;
		data = xmalloc(data_allocated);
		while (1) {
			data_read = read(state_fd, &data[data_size],
					 BUF_SIZE);
			if (data_read < 0) {
				if (errno == EINTR)
					continue;
				else {
					error("Read error on %s: %m",
					      state_file);
					break;
				}
			} else if (data_read == 0)	/* eof */
				break;
			data_size      += data_read;
			data_allocated += data_read;
			xrealloc(data, data_allocated);
		}
		close(state_fd);
	}
	xfree(state_file);

	buffer = create_buf(data, data_size);

	safe_unpack16(&ver, buffer);

	debug3("Version in fed_mgr_state header is %u", ver);
	if (ver > SLURM_PROTOCOL_VERSION || ver < SLURM_MIN_PROTOCOL_VERSION) {
		error("***********************************************");
		error("Can not recover fed_mgr state, incompatible version, "
		      "got %u need > %u <= %u", ver,
		      SLURM_MIN_PROTOCOL_VERSION, SLURM_PROTOCOL_VERSION);
		error("***********************************************");
		free_buf(buffer);
		slurm_mutex_unlock(&fed_mutex);
		return EFAULT;
	}

	slurm_mutex_lock(&fed_mutex);

	safe_unpack_time(&buf_time, buffer);

	error_code = slurmdbd_unpack_list_msg(&msg, ver, DBD_ADD_CLUSTERS,
					      buffer);
	if (error_code != SLURM_SUCCESS)
		goto unpack_error;
	else if (!msg->my_list) {
		error("No tres retrieved");
	}
	FREE_NULL_LIST(fed_mgr_siblings);
	fed_mgr_siblings = msg->my_list;
	msg->my_list = NULL;
	slurmdbd_free_list_msg(msg);

	/* Find current cluster and save off fed_elem for quick access. */
	if (!fed_mgr_cluster_name)
		fed_mgr_cluster_name = slurm_get_cluster_name();
	if (fed_mgr_siblings &&
	    !(cluster = list_find_first(fed_mgr_siblings,
					slurmdb_find_cluster_in_list,
					fed_mgr_cluster_name))) {
		error("This cluster doesn't exist in the fed siblings");
		goto unpack_error;
	} else if (cluster) {
		xfree(fed_mgr_fed_info.name);
		memcpy(&fed_mgr_fed_info, &cluster->fed, sizeof(fed_elem_t));
		fed_mgr_fed_info.name = xstrdup(cluster->fed.name);
	}

	free_buf(buffer);
	slurm_mutex_unlock(&fed_mutex);

	fed_mgr_init();

	return SLURM_SUCCESS;

unpack_error:
	if (buffer)
		free_buf(buffer);

	slurm_mutex_unlock(&fed_mutex);
	return SLURM_ERROR;
}

extern int _find_sibling_by_ip(void *x, void *key)
{
	slurmdb_cluster_rec_t *object = (slurmdb_cluster_rec_t *)x;
	char *ip = (char *)key;

	if (!xstrcmp(object->control_host, ip))
		return 1;

	return 0;
}

extern char *fed_mgr_find_sibling_name_by_ip(char *ip)
{
	char *name = NULL;
	slurmdb_cluster_rec_t *sibling = NULL;

	slurm_mutex_lock(&fed_mutex);
	if (fed_mgr_siblings &&
	    (sibling = list_find_first(fed_mgr_siblings, _find_sibling_by_ip,
				       ip)))
		name = xstrdup(sibling->name);
	slurm_mutex_unlock(&fed_mutex);

	return name;
}

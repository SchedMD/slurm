/*****************************************************************************\
 *  cred_context.c
 *****************************************************************************
 *  Copyright (C) 2023 SchedMD LLC.
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

#include "src/common/pack.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/cred.h"

#include "src/slurmd/slurmd/slurmd.h"

static int _drain_node(char *reason)
{
	slurm_msg_t req_msg;
	update_node_msg_t update_node_msg;

	slurm_init_update_node_msg(&update_node_msg);
	update_node_msg.node_names = conf->node_name;
	update_node_msg.node_state = NODE_STATE_DRAIN;
	update_node_msg.reason = reason;
	slurm_msg_t_init(&req_msg);
	req_msg.msg_type = REQUEST_UPDATE_NODE;
	req_msg.data = &update_node_msg;

	if (slurm_send_only_controller_msg(&req_msg, working_cluster_rec) < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

extern void save_cred_state(void)
{
	char *new_file, *reg_file;
	int cred_fd = -1, rc;
	buf_t *buffer = NULL;
	static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;

	reg_file = xstrdup(conf->spooldir);
	xstrcat(reg_file, "/cred_state");
	new_file = xstrdup(conf->spooldir);
	xstrcat(new_file, "/cred_state.new");

	slurm_mutex_lock(&state_mutex);
	if ((cred_fd = creat(new_file, 0600)) < 0) {
		error("creat(%s): %m", new_file);
		if (errno == ENOSPC)
			_drain_node("SlurmdSpoolDir is full");
		goto cleanup;
	}
	buffer = init_buf(1024);
	slurm_cred_ctx_pack(buffer);
	rc = write(cred_fd, get_buf_data(buffer), get_buf_offset(buffer));
	if (rc != get_buf_offset(buffer)) {
		error("write %s error %m", new_file);
		(void) unlink(new_file);
		if ((rc < 0) && (errno == ENOSPC))
			_drain_node("SlurmdSpoolDir is full");
		goto cleanup;
	}
	(void) unlink(reg_file);
	if (link(new_file, reg_file))
		debug4("unable to create link for %s -> %s: %m",
		       new_file, reg_file);
	(void) unlink(new_file);

cleanup:
	slurm_mutex_unlock(&state_mutex);
	xfree(reg_file);
	xfree(new_file);
	FREE_NULL_BUFFER(buffer);
	if (cred_fd >= 0)
		close(cred_fd);
}

extern void restore_cred_state(void)
{
	char *file_name = NULL;
	buf_t *buffer = NULL;

	file_name = xstrdup(conf->spooldir);
	xstrcat(file_name, "/cred_state");

	if (!(buffer = create_mmap_buf(file_name)))
		goto cleanup;

	slurm_cred_ctx_unpack(buffer);

cleanup:
	xfree(file_name);
	FREE_NULL_BUFFER(buffer);
}

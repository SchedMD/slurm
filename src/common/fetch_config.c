/*****************************************************************************\
 *  fetch_config.c - functions for "configless" slurm operation
 *****************************************************************************
 *  Copyright (C) 2020 SchedMD LLC.
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

#define _GNU_SOURCE

#include <inttypes.h>
#include <sys/mman.h>	/* memfd_create */
#include <sys/types.h>

#include "src/common/fetch_config.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_resolv.h"
#include "src/common/strlcpy.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"

static void _init_minimal_conf_server_config(List controllers);

static int to_parent[2] = {-1, -1};

static config_response_msg_t *_fetch_parent(pid_t pid)
{
	int len;
	buf_t *buffer;
	config_response_msg_t *config = NULL;
	int status;

	safe_read(to_parent[0], &len, sizeof(int));

	/*
	 * A zero across the pipe indicates the child failed to fetch the
	 * config file for some reason. The child will have already printed
	 * some error messages about this, so just return.
	 */
	if (len <= 0)
		return NULL;

	buffer = init_buf(len);
	safe_read(to_parent[0], buffer->head, len);

	if (unpack_config_response_msg(&config, buffer,
				       SLURM_PROTOCOL_VERSION)) {
		error("%s: unpack failed", __func__);
		return NULL;
	}

	waitpid(pid, &status, 0);
	debug2("%s: status from child %d", __func__, status);
	return config;

rwfail:
	error("%s: failed to read from child: %m", __func__);
	waitpid(pid, &status, 0);
	debug2("%s: status from child %d", __func__, status);

	return NULL;
}

static void _fetch_child(List controllers, uint32_t flags)
{
	config_response_msg_t *config;
	buf_t *buffer = init_buf(1024 * 1024);
	int len = 0;

	/*
	 * Parent process was holding this, but we need to drop it before
	 * issuing any RPC calls as the RPC stack will call into
	 * several slurm_conf_get_() functions.
	 *
	 * This is safe as we're single-threaded due to the fork().
	 */
	slurm_conf_unlock();

	_init_minimal_conf_server_config(controllers);
	config = fetch_config_from_controller(flags);

	if (!config) {
		error("%s: failed to fetch remote configs", __func__);
		safe_write(to_parent[1], &len, sizeof(int));
		_exit(1);
	}

	pack_config_response_msg(config, buffer, SLURM_PROTOCOL_VERSION);

	len = buffer->processed;
	safe_write(to_parent[1], &len, sizeof(int));
	safe_write(to_parent[1], buffer->head, len);

	_exit(0);

rwfail:
	error("%s: failed to write to parent: %m", __func__);
	_exit(1);
}

extern config_response_msg_t *fetch_config(char *conf_server, uint32_t flags)
{
	char *env_conf_server = getenv("SLURM_CONF_SERVER");
	List controllers = NULL;
	pid_t pid;

	/*
	 * Two main processing options here: we are either given an explicit
	 * server (with optional port number) via SLURM_CONF_SERVER or the
	 * conf_server argument, or we will need to make a blind DNS lookup.
	 *
	 * In either case, phase one here is to make a List with at least one
	 * slurmctld entry.
	 */
	if (env_conf_server || conf_server) {
		char *server, *port;
		ctl_entry_t *ctl = xmalloc(sizeof(*ctl));
		controllers = list_create(xfree_ptr);

		if (!(server = env_conf_server))
			server = conf_server;
		strlcpy(ctl->hostname, server, sizeof(ctl->hostname));

		if ((port = xstrchr(ctl->hostname, ':'))) {
			*port = '\0';
			port++;
			ctl->port = atoi(port);
		} else
			ctl->port = SLURMCTLD_PORT;

		list_push(controllers, ctl);
	} else {
                if (!(controllers = resolve_ctls_from_dns_srv())) {
                        error("%s: DNS SRV lookup failed", __func__);
			return NULL;
                }
	}

	/*
	 * At this point we have a List of controllers.
	 * Use that to build a memfd-backed minimal config file so we can
	 * communicate with slurmctld and get the real configs.
	 */
	if (pipe(to_parent) < 0) {
		error("%s: pipe failed: %m", __func__);
		return NULL;
	}

	if ((pid = fork()) < 0) {
		error("%s: fork: %m", __func__);
		close(to_parent[0]);
		close(to_parent[1]);
		return NULL;
	} else if (pid > 0) {
		list_destroy(controllers);
		return _fetch_parent(pid);
	}

	_fetch_child(controllers, flags);
	_exit(0);
}

extern config_response_msg_t *fetch_config_from_controller(uint32_t flags)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	config_request_msg_t req;
	config_response_msg_t *resp;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	memset(&req, 0, sizeof(req));
	req.flags = flags;
	req_msg.msg_type = REQUEST_CONFIG;
	req_msg.data = &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg,
					   working_cluster_rec) < 0)
		return NULL;

	switch (resp_msg.msg_type) {
	case RESPONSE_CONFIG:
		resp = (config_response_msg_t *) resp_msg.data;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		slurm_seterrno(rc);
		return NULL;
		break;
	default:
		slurm_seterrno(SLURM_UNEXPECTED_MSG_ERROR);
		return NULL;
		break;
	}

	return resp;
}

int dump_to_memfd(char *type, char *config, char **filename)
{
#ifdef HAVE_MEMFD_CREATE
	pid_t pid = getpid();

	int fd = memfd_create(type, MFD_CLOEXEC);
	if (fd < 0)
		fatal("%s: failed memfd_create: %m", __func__);

	xfree(*filename);
	xstrfmtcat(*filename, "/proc/%lu/fd/%d", (unsigned long) pid, fd);

	safe_write(fd, config, strlen(config));

	return fd;

rwfail:
	fatal("%s: could not write conf file, likely out of memory", __func__);
	return SLURM_ERROR;
#else
	pid_t pid = getpid();
	char template[] = "/tmp/fake-memfd-XXXXXX";
	int fd = mkstemp(template);

	if (fd < 0)
		fatal("%s: could not create temp file", __func__);
	/* immediately unlink the file so it doesn't get left around */
	(void) unlink(template);

	xfree(*filename);
	xstrfmtcat(*filename, "/proc/%lu/fd/%d", (unsigned long) pid, fd);

	safe_write(fd, config, strlen(config));

	return fd;

rwfail:
	fatal("%s: could not write conf file", __func__);
	return SLURM_ERROR;
#endif
}

static int _print_controllers(void *x, void *arg)
{
	ctl_entry_t *ctl = (ctl_entry_t *) x;
	char **conf = (char **) arg;

	/*
	 * First ctl entry's port number will be used. Slurm does not support
	 * the TCP port varying between slurmctlds.
	 */
	if (!*conf)
		xstrfmtcat(*conf, "SlurmctldPort=%u\n", ctl->port);
	xstrfmtcat(*conf, "SlurmctldHost=%s\n", ctl->hostname);

	return SLURM_SUCCESS;
}

static void _init_minimal_conf_server_config(List controllers)
{
	char *conf = NULL, *filename = NULL;
	int fd;

	list_for_each(controllers, _print_controllers, &conf);
	xstrfmtcat(conf, "ClusterName=CONFIGLESS\n");

	if ((fd = dump_to_memfd("slurm.conf", conf, &filename)) < 0)
		fatal("%s: could not write temporary config", __func__);
	xfree(conf);

	slurm_conf_init(filename);

	close(fd);
	xfree(filename);
}

static int _write_conf(const char *dir, const char *name, const char *content)
{
	char *file = NULL, *file_final = NULL;
	int fd = -1;

	xstrfmtcat(file, "%s/%s.new", dir, name);
	xstrfmtcat(file_final, "%s/%s", dir, name);

	if (!content) {
		(void) unlink(file_final);
		goto cleanup;
	}

	if ((fd = open(file, O_CREAT|O_WRONLY|O_TRUNC|O_CLOEXEC, 0644)) < 0) {
		error("%s: could not open config file `%s`", __func__, file);
		goto rwfail;
	}

	safe_write(fd, content, strlen(content));
	close(fd);
	fd = -1;

	if (rename(file, file_final))
		goto rwfail;

cleanup:
	xfree(file);
	xfree(file_final);
	return SLURM_SUCCESS;

rwfail:
	error("%s: error writing config to %s: %m", __func__, file);
	xfree(file);
	xfree(file_final);
	if (fd >= 0)
		close(fd);
	return SLURM_ERROR;
}

extern int write_configs_to_conf_cache(config_response_msg_t *msg,
				       const char *dir)
{
	if (_write_conf(dir, "slurm.conf", msg->config))
		return SLURM_ERROR;
	if (_write_conf(dir, "acct_gather.conf", msg->acct_gather_config))
		return SLURM_ERROR;
	if (_write_conf(dir, "cgroup.conf", msg->cgroup_config))
		return SLURM_ERROR;
	if (_write_conf(dir, "cgroup_allowed_devices_file.conf",
			msg->cgroup_allowed_devices_file_config))
		return SLURM_ERROR;
	if (_write_conf(dir, "ext_sensors.conf", msg->ext_sensors_config))
		return SLURM_ERROR;
	if (_write_conf(dir, "gres.conf", msg->gres_config))
		return SLURM_ERROR;
	if (_write_conf(dir, "job_container.conf", msg->xtra_config))
		return SLURM_ERROR;
	if (_write_conf(dir, "knl_cray.conf", msg->knl_cray_config))
		return SLURM_ERROR;
	if (_write_conf(dir, "knl_generic.conf", msg->knl_generic_config))
		return SLURM_ERROR;
	if (_write_conf(dir, "plugstack.conf", msg->plugstack_config))
		return SLURM_ERROR;
	if (_write_conf(dir, "topology.conf", msg->topology_config))
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

static void _load_conf(const char *dir, const char *name, char **target)
{
	char *file = NULL;
	buf_t *config;

	xstrfmtcat(file, "%s/%s", dir, name);
	config = create_mmap_buf(file);
	xfree(file);

	/*
	 * If we can't load a given config, then assume that one isn't required
	 * on this system.
	 */
	if (config)
		*target = xstrndup(config->head, config->size);

	free_buf(config);
}

extern void load_config_response_msg(config_response_msg_t *msg, int flags)
{
	xassert(msg);
	char *dir = get_extra_conf_path("");

	_load_conf(dir, "slurm.conf", &msg->config);

	if (!(flags & CONFIG_REQUEST_SLURMD)) {
		xfree(dir);
		return;
	}

	_load_conf(dir, "acct_gather.conf", &msg->acct_gather_config);
	_load_conf(dir, "cgroup.conf", &msg->cgroup_config);
	_load_conf(dir, "cgroup_allowed_devices_file.conf",
		   &msg->cgroup_allowed_devices_file_config);
	_load_conf(dir, "ext_sensors.conf", &msg->ext_sensors_config);
	_load_conf(dir, "gres.conf", &msg->gres_config);
	_load_conf(dir, "job_container.conf", &msg->xtra_config);
	_load_conf(dir, "knl_cray.conf", &msg->knl_cray_config);
	_load_conf(dir, "knl_generic.conf", &msg->knl_generic_config);
	_load_conf(dir, "plugstack.conf", &msg->plugstack_config);
	_load_conf(dir, "topology.conf", &msg->topology_config);

	msg->slurmd_spooldir = xstrdup(slurm_conf.slurmd_spooldir);

	xfree(dir);
}

/*****************************************************************************\
 *  fetch_config.c - functions for "configless" slurm operation
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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
#include <sys/stat.h>

#include "src/common/fetch_config.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/slurm_resolv.h"
#include "src/common/strlcpy.h"
#include "src/common/util-net.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"

#include "src/interfaces/conn.h"

/* Define slurm-specific aliases for use by plugins, see slurm_xlator.h. */
strong_alias(dump_to_memfd, slurm_dump_to_memfd);

static char *slurmd_config_files[] = {
	"slurm.conf", "acct_gather.conf", "cgroup.conf",
	"cli_filter.lua", "gres.conf", "helpers.conf",
	"job_container.conf", "mpi.conf", "oci.conf",
	"plugstack.conf", "scrun.lua", "topology.conf", "topology.yaml", NULL
};

static char *client_config_files[] = {
	"slurm.conf", "cli_filter.lua", "plugstack.conf", "topology.conf",
	"topology.yaml", "oci.conf", "scrun.lua", NULL
};


static void _init_minimal_conf_server_config(list_t *controllers, bool use_v6,
					     bool reinit);

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
	if (len <= 0) {
		waitpid(pid, &status, 0);
		debug2("%s: status from child %d", __func__, status);
		return NULL;
	}

	buffer = init_buf(len);
	safe_read(to_parent[0], buffer->head, len);

	waitpid(pid, &status, 0);
	debug2("%s: status from child %d", __func__, status);

	if (unpack_config_response_msg(&config, buffer,
				       SLURM_PROTOCOL_VERSION)) {
		FREE_NULL_BUFFER(buffer);
		error("%s: unpack failed", __func__);
		return NULL;
	}
	FREE_NULL_BUFFER(buffer);

	return config;

rwfail:
	error("%s: failed to read from child: %m", __func__);
	waitpid(pid, &status, 0);
	debug2("%s: status from child %d", __func__, status);

	return NULL;
}

static void _fetch_child(list_t *controllers, uint32_t flags, uint16_t port,
			 char *ca_cert_file)
{
	config_response_msg_t *config;
	ctl_entry_t *ctl = NULL;
	buf_t *buffer = init_buf(1024 * 1024);
	int len = 0;

	setenv("SLURM_CONFIG_FETCH", "1", 1);

	/*
	 * Parent process was holding this, but we need to drop it before
	 * issuing any RPC calls as the RPC stack will call into
	 * several slurm_conf_get_() functions.
	 *
	 * This is safe as we're single-threaded due to the fork().
	 */
	slurm_conf_unlock();

	if (ca_cert_file) {
		slurm_conf.plugindir = xstrdup(default_plugin_path);
		slurm_conf.tls_type = xstrdup("tls/s2n");

		/* certmgr plugin will be loaded after getting configuration */
		if (conn_g_init()) {
			error("--ca-cert-file was specified but TLS plugin failed to load");
			goto rwfail;
		}
		if (conn_g_load_ca_cert(ca_cert_file)) {
			error("Failed to load certificate file '%s'", ca_cert_file);
			goto rwfail;
		}
	}

	ctl = list_peek(controllers);

	if (ctl->has_ipv6 && !ctl->has_ipv4)
		_init_minimal_conf_server_config(controllers, true, false);
	else
		_init_minimal_conf_server_config(controllers, false, false);

	config = fetch_config_from_controller(flags, port);

	if (!config && ctl->has_ipv6 && ctl->has_ipv4) {
		warning("%s: failed to fetch remote configs via IPv4, retrying with IPv6: %m",
			__func__);
		_init_minimal_conf_server_config(controllers, true, true);
		config = fetch_config_from_controller(flags, port);
	}

	if (!config) {
		error("%s: failed to fetch remote configs: %m", __func__);
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

static int _get_controller_addr_type(void *x, void *arg)
{
	ctl_entry_t *ctl = (ctl_entry_t *) x;

	host_has_addr_family(ctl->hostname, NULL, &ctl->has_ipv4,
			     &ctl->has_ipv6);

	return SLURM_SUCCESS;
}

extern config_response_msg_t *fetch_config(char *conf_server, uint32_t flags,
					   uint16_t sackd_port,
					   char *ca_cert_file)
{
	char *env_conf_server = getenv("SLURM_CONF_SERVER");
	list_t *controllers = NULL;
	pid_t pid;
	char *sack_jwks = NULL, *sack_key = NULL;
	struct stat statbuf;

	/*
	 * Two main processing options here: we are either given an explicit
	 * server (with optional port number) via SLURM_CONF_SERVER or the
	 * conf_server argument, or we will need to make a blind DNS lookup.
	 *
	 * In either case, phase one here is to make a List with at least one
	 * slurmctld entry.
	 */
	if (env_conf_server || conf_server) {
		char *server, *tmp, *port, *save_ptr = NULL;
		controllers = list_create(xfree_ptr);

		if (env_conf_server)
			tmp = xstrdup(env_conf_server);
		else
			tmp = xstrdup(conf_server);

		server = strtok_r(tmp, ",", &save_ptr);
		while (server) {
			ctl_entry_t *ctl = xmalloc(sizeof(*ctl));
			char *tmp_ptr = NULL;

			if (server[0] == '[')
				server++;

			strlcpy(ctl->hostname, server, sizeof(ctl->hostname));

			if ((tmp_ptr = strchr(ctl->hostname, ']'))) {
				*tmp_ptr = '\0';
				tmp_ptr++;
			} else {
				tmp_ptr = ctl->hostname;
			}

			if ((port = xstrchr(tmp_ptr, ':'))) {
				*port = '\0';
				port++;
				ctl->port = atoi(port);
			} else
				ctl->port = SLURMCTLD_PORT;

			list_append(controllers, ctl);
			server = strtok_r(NULL, ",", &save_ptr);
		}
		xfree(tmp);
	} else {
                if (!(controllers = resolve_ctls_from_dns_srv())) {
                        error("%s: DNS SRV lookup failed", __func__);
			return NULL;
                }
	}

	list_for_each(controllers, _get_controller_addr_type, NULL);

	/* If the slurm.key file exists, assume we're using auth/slurm */
	sack_jwks = get_extra_conf_path("slurm.jwks");
	sack_key = get_extra_conf_path("slurm.key");
	if (!stat(sack_jwks, &statbuf))
		setenv("SLURM_SACK_JWKS", sack_jwks, 1);
	else if (!stat(sack_key, &statbuf))
		setenv("SLURM_SACK_KEY", sack_key, 1);
	xfree(sack_jwks);
	xfree(sack_key);

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
		FREE_NULL_LIST(controllers);
		return _fetch_parent(pid);
	}

	_fetch_child(controllers, flags, sackd_port, ca_cert_file);
	_exit(0);
}

extern config_response_msg_t *fetch_config_from_controller(uint32_t flags,
							   uint16_t port)
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
	req.port = port;
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
		errno = rc;
		return NULL;
		break;
	default:
		errno = SLURM_UNEXPECTED_MSG_ERROR;
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

	if (config)
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

	if (config)
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

static void _init_minimal_conf_server_config(list_t *controllers, bool use_v6,
					     bool reinit)
{
	char *conf = NULL, *filename = NULL;
	int fd;

	list_for_each(controllers, _print_controllers, &conf);
	xstrfmtcat(conf, "ClusterName=CONFIGLESS\n");

	/* Use for the --authinfo option in slurmd */
	if (slurm_conf.authinfo)
		xstrfmtcat(conf, "AuthInfo=%s\n", slurm_conf.authinfo);

	if (use_v6)
		xstrcat(conf, "CommunicationParameters=EnableIPv6");

	if ((fd = dump_to_memfd("slurm.conf", conf, &filename)) < 0)
		fatal("%s: could not write temporary config", __func__);
	xfree(conf);

	if (reinit)
		slurm_conf_reinit(filename);
	else
		slurm_init(filename);

	close(fd);
	xfree(filename);
}

static int _write_conf(const char *dir, const char *name, const char *content,
		      bool exists, bool execute)
{
	char *file = NULL, *file_final = NULL;
	int fd = -1;
	mode_t mode = execute ? 0755 : 0644;

	xstrfmtcat(file, "%s/%s.new", dir, name);
	xstrfmtcat(file_final, "%s/%s", dir, name);

	if (!exists) {
		(void) unlink(file_final);
		goto cleanup;
	}


	if ((fd = open(file, O_CREAT|O_WRONLY|O_TRUNC|O_CLOEXEC, mode)) < 0) {
		error("%s: could not open config file `%s`", __func__, file);
		goto rwfail;
	}

	if (content)
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

extern int find_conf_by_name(void *x, void *key)
{
	config_file_t *config = (config_file_t *)x;
	char *file_name_key = (char *)key;
	return !xstrcmp(config->file_name, file_name_key);
}

extern int write_one_config(void *x, void *arg)
{
	config_file_t *config = (config_file_t *) x;
	char *dir = (char *) arg;
	if (_write_conf(dir, config->file_name, config->file_content,
		        config->exists, config->execute))
		return SLURM_ERROR;
	return SLURM_SUCCESS;
}

extern int write_config_to_memfd(void *x, void *arg)
{
	config_file_t *config = x;

	if (config->exists)
		config->memfd_fd = dump_to_memfd(config->file_name,
						 config->file_content,
						 &config->memfd_path);

	return SLURM_SUCCESS;
}

extern int write_configs_to_conf_cache(config_response_msg_t *msg,
				       char *dir)
{
	if (list_for_each(msg->config_files, write_one_config, dir) < 0) {
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static void _load_conf2list(config_response_msg_t *msg, char *file_name,
			    bool is_script)
{
	config_file_t *conf_file = NULL;
	buf_t *config;
	char *file = get_extra_conf_path(file_name);
	bool config_exists = true;

	config = create_mmap_buf(file);
	xfree(file);

	/*
	 * If we failed to mmap the file, it likely doesn't exist.
	 * However, since Linux 2.6.16, EINVAL likely indicates an empty file.
	 * We do need to create that blank file, as certain plugins - cgroup
	 * especially - treat the absence of the file differently than an
	 * empty file.
	 */
	if (!config && errno != EINVAL)
		config_exists = false;

	conf_file = xmalloc(sizeof(*conf_file));
	conf_file->exists = config_exists;
	conf_file->execute = is_script;
	if (config)
		conf_file->file_content = xstrndup(config->head, config->size);
	conf_file->file_name = xstrdup(file_name);
	list_append(msg->config_files, conf_file);

	debug3("%s: config file %s %s",
	       __func__, file_name,
	       (config_exists ? "exists" : "does not exist"));

	FREE_NULL_BUFFER(config);
}

/*
 * ListForF to load the config from includes_list into the response msg.
 *
 * IN: x, list data (char pointer with include filename).
 * IN/OUT: key, config_response_msg_t to be updated.
 *
 * RET: SLURM_SUCCESS.
 */
static int _foreach_include_file(void *x, void *arg)
{
	char *file_name = x;
	config_response_msg_t *msg = arg;

	_load_conf2list(msg, file_name, false);

	return SLURM_SUCCESS;
}

/*
 * ListFindF for conf_file in conf_includes_list.
 *
 * IN: x, list data (conf_includes_map_t node).
 * IN: key, conf filename to be found.
 *
 * RET: 1 if found, 0 otherwise.
 */
extern int find_map_conf_file(void *x, void *key)
{
	conf_includes_map_t *map = x;
	char *conf_file = key;

	xassert(map);
	xassert(map->conf_file);
	xassert(conf_file);

	if (!xstrcmp(map->conf_file, conf_file))
		return 1;

	return 0;
}

extern config_response_msg_t *new_config_response(bool to_slurmd)
{
	config_response_msg_t *msg = xmalloc(sizeof(*msg));
	conf_includes_map_t *map = NULL;
	char **files = client_config_files;

	if (to_slurmd)
		files = slurmd_config_files;

	msg->config_files = list_create(destroy_config_file);

	for (int i = 0; files[i]; i++) {
		_load_conf2list(msg, files[i], false);

		if (conf_includes_list) {
			map = list_find_first_ro(conf_includes_list,
						 find_map_conf_file, files[i]);

			if (map && map->include_list)
				list_for_each_ro(map->include_list,
						 _foreach_include_file, msg);
		}
	}

	/*
	 * Load Prolog and Epilog scripts.
	 * Only load if a non-absolute path is provided, this is our
	 * indication that the file should be sent out, and matches
	 * configuration semantics for the Include lines.
	 */
	if (to_slurmd) {
		for (int i = 0; i < slurm_conf.prolog_cnt; i++) {
			if (slurm_conf.prolog[i][0] != '/')
				_load_conf2list(msg, slurm_conf.prolog[i],
						true);
		}
		for (int i = 0; i < slurm_conf.epilog_cnt; i++) {
			if (slurm_conf.epilog[i][0] != '/')
				_load_conf2list(msg, slurm_conf.epilog[i],
						true);
		}
	}

	return msg;
}

extern void destroy_config_file(void *object)
{
	config_file_t *conf_file = (config_file_t *)object;

	if (!conf_file)
		return;

	if (conf_file->memfd_path)
		close(conf_file->memfd_fd);
	xfree(conf_file->memfd_path);

	xfree(conf_file->file_name);
	xfree(conf_file->file_content);
	xfree(conf_file);
}

extern void grab_include_directives(void)
{
	char *conf_file = NULL;
	struct stat stat_buf;
	uint32_t parse_flags = 0;

	parse_flags |= PARSE_FLAGS_INCLUDE_ONLY;
	for (int i = 0; slurmd_config_files[i]; i++) {
		if ((!conf_includes_list) ||
		    (!list_find_first_ro(conf_includes_list,
					 find_map_conf_file,
					 slurmd_config_files[i]))) {
			conf_file = get_extra_conf_path(slurmd_config_files[i]);
			if (!stat(conf_file, &stat_buf))
				s_p_parse_file(NULL, NULL, conf_file,
					       parse_flags, NULL);
		}
		xfree(conf_file);
	}
}

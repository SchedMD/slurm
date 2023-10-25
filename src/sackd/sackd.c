/*****************************************************************************\
 *  sackd.c
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

#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/conmgr.h"
#include "src/common/fd.h"
#include "src/common/fetch_config.h"
#include "src/common/read_config.h"
#include "src/common/ref.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/auth.h"
#include "src/interfaces/hash.h"

decl_static_data(usage_txt);

static bool registered = false;
static char *conf_file = NULL;
static char *conf_server = NULL;
static char *dir = "/run/slurm/conf";

static void _usage(void)
{
	char *txt;
	static_ref_to_cstring(txt, usage_txt);
	fprintf(stderr, "%s", txt);
	xfree(txt);
}

static void _parse_args(int argc, char **argv)
{
	log_options_t logopt = LOG_OPTS_STDERR_ONLY;
	int c = 0, option_index = 0;

	enum {
		LONG_OPT_ENUM_START = 0x100,
		LONG_OPT_CONF_SERVER,
	};

	static struct option long_options[] = {
		{"conf-server", required_argument, 0, LONG_OPT_CONF_SERVER},
		{NULL, no_argument, 0, 'v'},
		{NULL, 0, 0, 0}
	};

	log_init(xbasename(argv[0]), logopt, 0, NULL);

	opterr = 0;
	while ((c = getopt_long(argc, argv, "f:hv",
				long_options, &option_index)) != -1) {
		switch (c) {
		case (int) 'f':
			xfree(conf_file);
			conf_file = xstrdup(optarg);
			break;
		case (int) 'h':
			_usage();
			exit(0);
			break;
		case (int) 'v':
			logopt.stderr_level++;
			log_alter(logopt, 0, NULL);
			break;
		case LONG_OPT_CONF_SERVER:
			xfree(conf_server);
			conf_server = xstrdup(optarg);
			break;
		default:
			_usage();
			exit(1);
			break;
		}
	}
}

/*
 * Returns true when a local config file is found.
 * Will ensure conf_file is set to avoid slurm_conf_init()
 * needing to make this same decision again later.
 */
static bool _slurm_conf_file_exists(void)
{
	struct stat stat_buf;

	if (conf_file)
		return true;
	if ((conf_file = xstrdup(getenv("SLURM_CONF"))))
		return true;
	if (!stat(default_slurm_config_file, &stat_buf)) {
		conf_file = xstrdup(default_slurm_config_file);
		return true;
	}

	return false;
}

static void _establish_config_source(void)
{
	config_response_msg_t *configs;

	if (!conf_server && _slurm_conf_file_exists()) {
		debug("%s: config will load from file", __func__);
		return;
	}

	/*
	 * Attempt to create cache dir.
	 * If that fails, attempt to destroy it, then make a new directory.
	 * If that fails again, we're out of luck.
	 */
	if (mkdir(dir, 0755) < 0) {
		(void) rmdir_recursive(dir, true);
		if (mkdir(dir, 0755) < 0)
			fatal("%s: failed to create a clean %s", __func__, dir);
	}

	if (!(configs = fetch_config(conf_server, CONFIG_REQUEST_SACKD)))
		fatal("%s: failed to load configs", __func__);

	registered = true;

	if (write_configs_to_conf_cache(configs, dir))
		fatal("%s: failed to write configs to cache", __func__);

	slurm_free_config_response_msg(configs);
	xstrfmtcat(conf_file, "%s/slurm.conf", dir);
}

static int _on_msg(con_mgr_fd_t *con, slurm_msg_t *msg, void *arg)
{
	if (!msg->auth_ids_set) {
		error("%s: [%s] rejecting %s RPC with missing user auth",
		      __func__, con_mgr_fd_get_name(con),
		      rpc_num2string(msg->msg_type));
		return SLURM_PROTOCOL_AUTHENTICATION_ERROR;
	} else if (msg->auth_uid != slurm_conf.slurm_user_id) {
		error("%s: [%s] rejecting %s RPC with user:%u != SlurmUser:%u",
		      __func__, con_mgr_fd_get_name(con),
		      rpc_num2string(msg->msg_type), msg->auth_uid,
		      slurm_conf.slurm_user_id);
		return SLURM_PROTOCOL_AUTHENTICATION_ERROR;
	}

	switch (msg->msg_type) {
	case REQUEST_RECONFIGURE_SACKD:
		info("reconfigure requested");
		if (write_configs_to_conf_cache(msg->data, dir))
			error("%s: failed to write configs to cache", __func__);
		/* no need to respond */
		break;
	default:
		error("%s: [%s] unexpected message %u",
		      __func__, con_mgr_fd_get_name(con), msg->msg_type);
	}

	slurm_free_msg(msg);
	con_mgr_queue_close_fd(con);
	return SLURM_SUCCESS;
}

static void _listen_for_reconf(void)
{
	int fd = -1;
	int rc = SLURM_SUCCESS;
	con_mgr_events_t events = { .on_msg = _on_msg };

	if ((fd = slurm_init_msg_engine_port(slurm_conf.slurmd_port)) < 0) {
		error("%s: failed to open port: %m", __func__);
		return;
	}

	if ((rc = con_mgr_process_fd_listen(fd, CON_TYPE_RPC, events, NULL, 0, NULL)))
		fatal("%s: conmgr refused fd=%d: %s",
		      __func__, fd, slurm_strerror(rc));
}

extern int main(int argc, char **argv)
{
	_parse_args(argc, argv);

	_establish_config_source();

	slurm_conf_init(conf_file);

	if (getuid() != slurm_conf.slurm_user_id) {
		char *user = uid_to_string(getuid());
		warning("sackd running as %s instead of SlurmUser(%s)",
			user, slurm_conf.slurm_user_name);
		xfree(user);
	}

	auth_g_init();
	hash_g_init();

	if (registered)
		_listen_for_reconf();

	info("running");
	con_mgr_run(true);

	xfree(conf_file);
	xfree(conf_server);
	return 0;
}

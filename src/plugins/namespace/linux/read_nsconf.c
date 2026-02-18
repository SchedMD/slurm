/*****************************************************************************\
 *  read_nsconf.c - parse namespace.yaml configuration file.
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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
#define _GNU_SOURCE
#include <sys/stat.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

#include "src/common/log.h"
#include "src/common/parse_config.h"
#include "src/common/read_config.h"
#include "src/common/sercli.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/data_parser.h"
#include "src/interfaces/serializer.h"

#include "read_nsconf.h"

#define SWAP_IF_SET(str_dst, str_src) \
	do { \
		if (str_src && str_src[0]) \
			SWAP(str_dst, str_src); \
	} while (0);

char *ns_conf_file = "namespace.yaml";

static ns_conf_t slurm_ns_conf;
static buf_t *slurm_ns_conf_buf = NULL;
static bool slurm_ns_conf_inited = false;

static void _dump_ns_conf(void)
{
	if (!(slurm_conf.debug_flags & DEBUG_FLAG_NAMESPACE))
		return;

	log_flag(NAMESPACE, "AutoBasePath=%d", slurm_ns_conf.auto_basepath);
	log_flag(NAMESPACE, "BasePath=%s", slurm_ns_conf.basepath);
	log_flag(NAMESPACE, "CloneNSEpilog=%s", slurm_ns_conf.clonensepilog);
	log_flag(NAMESPACE, "CloneNSFlags=%s", slurm_ns_conf.clonensflags_str);
	log_flag(NAMESPACE, "CloneNSScript=%s", slurm_ns_conf.clonensscript);
	log_flag(NAMESPACE, "CloneNSEpilog_Wait=%u",
		 slurm_ns_conf.clonensepilog_wait);
	log_flag(NAMESPACE, "CloneNSScript_Wait=%u",
		 slurm_ns_conf.clonensscript_wait);
	log_flag(NAMESPACE, "Dirs=%s", slurm_ns_conf.dirs);
	log_flag(NAMESPACE, "disable_bpf_token=%d",
		 slurm_ns_conf.disable_bpf_token);
	log_flag(NAMESPACE, "InitScript=%s", slurm_ns_conf.initscript);
	log_flag(NAMESPACE, "Shared=%d", slurm_ns_conf.shared);
	log_flag(NAMESPACE, "UserNSScript=%s", slurm_ns_conf.usernsscript);
}

static void _pack_slurm_ns_conf_buf(void)
{
	if (slurm_ns_conf_buf)
		FREE_NULL_BUFFER(slurm_ns_conf_buf);

	slurm_ns_conf_buf = init_buf(0);
	packbool(slurm_ns_conf.auto_basepath, slurm_ns_conf_buf);
	packstr(slurm_ns_conf.basepath, slurm_ns_conf_buf);
	packstr(slurm_ns_conf.clonensepilog, slurm_ns_conf_buf);
	pack32(slurm_ns_conf.clonensflags, slurm_ns_conf_buf);
	packstr(slurm_ns_conf.clonensscript, slurm_ns_conf_buf);
	pack32(slurm_ns_conf.clonensepilog_wait, slurm_ns_conf_buf);
	pack32(slurm_ns_conf.clonensscript_wait, slurm_ns_conf_buf);
	packstr(slurm_ns_conf.dirs, slurm_ns_conf_buf);
	packbool(slurm_ns_conf.disable_bpf_token, slurm_ns_conf_buf);
	packstr(slurm_ns_conf.initscript, slurm_ns_conf_buf);
	packbool(slurm_ns_conf.shared, slurm_ns_conf_buf);
	packstr(slurm_ns_conf.usernsscript, slurm_ns_conf_buf);
}

static void _swap_slurm_ns_conf(ns_node_conf_t *ns_node_conf)
{
	ns_conf_t *ns_conf = ns_node_conf->ns_conf;

	if (!ns_conf)
		return;

	if (ns_node_conf->set_auto_basepath)
		slurm_ns_conf.auto_basepath = ns_conf->auto_basepath;
	if (ns_node_conf->set_clonensepilog_wait)
		slurm_ns_conf.clonensepilog_wait = ns_conf->clonensepilog_wait;
	if (ns_node_conf->set_clonensscript_wait)
		slurm_ns_conf.clonensscript_wait = ns_conf->clonensscript_wait;
	if (ns_node_conf->set_disable_bpf_token)
		slurm_ns_conf.disable_bpf_token = ns_conf->disable_bpf_token;
	if (ns_node_conf->set_shared)
		slurm_ns_conf.shared = ns_conf->shared;

	SWAP_IF_SET(slurm_ns_conf.basepath, ns_conf->basepath);
	SWAP_IF_SET(slurm_ns_conf.clonensepilog, ns_conf->clonensepilog);
	SWAP_IF_SET(slurm_ns_conf.clonensflags_str, ns_conf->clonensflags_str);
	SWAP_IF_SET(slurm_ns_conf.clonensscript, ns_conf->clonensscript);
	SWAP_IF_SET(slurm_ns_conf.dirs, ns_conf->dirs);
	SWAP_IF_SET(slurm_ns_conf.initscript, ns_conf->initscript);
	SWAP_IF_SET(slurm_ns_conf.usernsscript, ns_conf->usernsscript);
}

static int _find_node_conf(void *x, void *key)
{
	ns_node_conf_t *ns_node_conf = x;
	char *node_name = key;

	xassert(ns_node_conf && ns_node_conf->nodes);

	if (hostlist_find(ns_node_conf->nodes, node_name) >= 0)
		return true;
	return false;
}

static void _set_clonensflags(void)
{
	/* Always set CLONE_NEWNS */
	slurm_ns_conf.clonensflags = CLONE_NEWNS;

	if (xstrcasestr(slurm_ns_conf.clonensflags_str, "CLONE_NEWNS"))
		slurm_ns_conf.clonensflags |= CLONE_NEWNS;
	if (xstrcasestr(slurm_ns_conf.clonensflags_str, "CLONE_NEWPID"))
		slurm_ns_conf.clonensflags |= CLONE_NEWPID;
	if (xstrcasestr(slurm_ns_conf.clonensflags_str, "CLONE_NEWUSER"))
		slurm_ns_conf.clonensflags |= CLONE_NEWUSER;
}

static void _set_slurm_ns_conf_defaults()
{
	if (!slurm_ns_conf.dirs)
		slurm_ns_conf.dirs = xstrdup(SLURM_NEWNS_DEF_DIRS);
	if (!slurm_ns_conf.clonensepilog_wait)
		slurm_ns_conf.clonensepilog_wait = SLURM_NS_WAIT_DEF;
	if (!slurm_ns_conf.clonensscript_wait)
		slurm_ns_conf.clonensscript_wait = SLURM_NS_WAIT_DEF;
}

static int _read_slurm_ns_conf(void)
{
	char *conf_path = NULL;
	struct stat stat_buf;
	buf_t *conf_buf = NULL;
	int rc = SLURM_SUCCESS;
	ns_full_conf_t *ns_full_conf = NULL;

	xassert(conf->node_name);

	conf_path = get_extra_conf_path(ns_conf_file);

	if (stat(conf_path, &stat_buf) == -1) {
		error("Could not find %s file", ns_conf_file);
		rc = ENOENT;
		goto end_it;
	}

	debug("Reading %s file %s", ns_conf_file, conf_path);

	serializer_required(MIME_TYPE_YAML);

	if (!(conf_buf = create_mmap_buf(conf_path))) {
		error("could not load %s, and thus cannot create namespace context",
		      conf_path);
		rc = SLURM_ERROR;
		goto end_it;
	}

	DATA_PARSE_FROM_STR(NAMESPACE_FULL_CONF_PTR, conf_buf->head,
			    conf_buf->size, ns_full_conf, NULL, MIME_TYPE_YAML,
			    rc);
	if (!ns_full_conf ||
	    (!ns_full_conf->defaults && !ns_full_conf->node_confs))
		goto end_it;
	if (ns_full_conf->defaults) {
		/* All set to true for _swap_slurm_ns_conf() to work */
		ns_node_conf_t tmp_ns_node_conf = {
			.ns_conf = ns_full_conf->defaults,
			.set_auto_basepath = true,
			.set_clonensepilog_wait = true,
			.set_clonensscript_wait = true,
			.set_disable_bpf_token = true,
			.set_shared = true,
		};

		_swap_slurm_ns_conf(&tmp_ns_node_conf);
	}
	if (ns_full_conf->node_confs) {
		ns_node_conf_t *ns_node_conf =
			list_find_first(ns_full_conf->node_confs,
					_find_node_conf, conf->node_name);
		if (ns_node_conf)
			_swap_slurm_ns_conf(ns_node_conf);
	}

	if (!slurm_ns_conf.dirs)
		debug3("empty Dirs detected");

	if (!slurm_ns_conf.disable_bpf_token)
		log_flag(NAMESPACE, "empty disable_bpf_token detected");

	if (!slurm_ns_conf.initscript)
		debug3("empty init script detected");

	if (!slurm_ns_conf.usernsscript)
		debug3("empty user ns script detected");

	if (!slurm_ns_conf.clonensscript)
		debug3("empty post clone ns script detected");

	if (!slurm_ns_conf.clonensepilog)
		debug3("empty post clone ns epilog script detected");

	if (!slurm_ns_conf.basepath) {
		debug("Config not found in %s. Disabling plugin on this node",
		      ns_conf_file);
	} else if (!xstrncasecmp(slurm_ns_conf.basepath, "none", 4)) {
		debug("Plugin is disabled on this node per %s.",
		      ns_conf_file);
	} else {
		char *tmp_basepath = slurm_ns_conf.basepath;
		slurm_ns_conf.basepath =
			slurm_conf_expand_slurmd_path(tmp_basepath,
						      conf->node_name, NULL);
		xfree(tmp_basepath);
#ifdef MULTIPLE_SLURMD
		xstrfmtcat(slurm_ns_conf.basepath, "/%s", conf->node_name);
#endif
	}

	_set_clonensflags();
	_set_slurm_ns_conf_defaults();

end_it:
	xfree(conf_path);
	FREE_NULL_BUFFER(conf_buf);
	slurm_free_ns_full_conf(ns_full_conf);
	return rc;
}

extern ns_conf_t *init_slurm_ns_conf(void)
{
	int rc;
	if (!slurm_ns_conf_inited) {
		char *save_ptr = NULL, *token, *buffer;
		memset(&slurm_ns_conf, 0, sizeof(slurm_ns_conf));
		rc = _read_slurm_ns_conf();
		if (rc != SLURM_SUCCESS)
			return NULL;

		xassert(slurm_ns_conf.dirs);

		/* BasePath cannot be in "Dirs" */
		buffer = xstrdup(slurm_ns_conf.dirs);
		token = strtok_r(buffer, ",", &save_ptr);
		while (token) {
			char *found = xstrstr(token, slurm_ns_conf.basepath);
			if (found == token)
				fatal("BasePath(%s) cannot also be in Dirs.",
				      slurm_ns_conf.basepath);
			token = strtok_r(NULL, ",", &save_ptr);
		}
		xfree(buffer);

		_pack_slurm_ns_conf_buf();

		slurm_ns_conf_inited = true;

		_dump_ns_conf();
	}

	return &slurm_ns_conf;
}

extern ns_conf_t *set_slurm_ns_conf(buf_t *buf)
{
	xassert(buf);

	safe_unpackbool(&slurm_ns_conf.auto_basepath, buf);
	safe_unpackstr(&slurm_ns_conf.basepath, buf);
	safe_unpackstr(&slurm_ns_conf.clonensepilog, buf);
	safe_unpack32(&slurm_ns_conf.clonensflags, buf);
	safe_unpackstr(&slurm_ns_conf.clonensscript, buf);
	safe_unpack32(&slurm_ns_conf.clonensepilog_wait, buf);
	safe_unpack32(&slurm_ns_conf.clonensscript_wait, buf);
	safe_unpackstr(&slurm_ns_conf.dirs, buf);
	safe_unpackbool(&slurm_ns_conf.disable_bpf_token, buf);
	safe_unpackstr(&slurm_ns_conf.initscript, buf);
	safe_unpackbool(&slurm_ns_conf.shared, buf);
	safe_unpackstr(&slurm_ns_conf.usernsscript, buf);
	slurm_ns_conf_inited = true;

	return &slurm_ns_conf;
unpack_error:
	return NULL;
}

extern ns_conf_t *get_slurm_ns_conf(void)
{
	if (!slurm_ns_conf_inited)
		return NULL;
	return &slurm_ns_conf;
}

extern buf_t *get_slurm_ns_conf_buf(void)
{
	return slurm_ns_conf_buf;
}

extern void free_ns_conf(void)
{
	if (slurm_ns_conf_inited) {
		slurm_free_ns_conf_members(&slurm_ns_conf);
		FREE_NULL_BUFFER(slurm_ns_conf_buf);
		slurm_ns_conf_inited = false;
	}
	return;
}

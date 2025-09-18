/*****************************************************************************\
 *  read_nsconf.c - parse namespace.conf configuration file.
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
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "read_nsconf.h"

char *ns_conf_file = NULL;
char *job_container_conf_file = "job_container.conf";
char *namespace_conf_file = "namespace.conf";

static slurm_ns_conf_t slurm_ns_conf;
static buf_t *slurm_ns_conf_buf = NULL;
static bool slurm_ns_conf_inited = false;
static bool auto_basepath_set = false;
static bool shared_set = false;
static bool clonensscript_wait_set = false;
static bool clonensepilog_wait_set = false;
static bool clonensflags_set = false;

static s_p_hashtbl_t *_create_ns_hashtbl(void)
{
	static s_p_options_t ns_options[] = {
		{ "AutoBasePath", S_P_BOOLEAN },
		{ "BasePath", S_P_STRING },
		{ "CloneNSEpilog", S_P_STRING },
		{ "CloneNSFlags", S_P_STRING },
		{ "CloneNSScript", S_P_STRING },
		{ "CloneNSEpilog_Wait", S_P_UINT32 },
		{ "CloneNSScript_Wait", S_P_UINT32 },
		{ "Dirs", S_P_STRING },
		{ "InitScript", S_P_STRING },
		{ "Shared", S_P_BOOLEAN },
		{ NULL }
	};

	return s_p_hashtbl_create(ns_options);
}

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
	log_flag(NAMESPACE, "Shared=%d", slurm_ns_conf.shared);
	log_flag(NAMESPACE, "InitScript=%s", slurm_ns_conf.initscript);
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

	clonensflags_set = true;
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
	packstr(slurm_ns_conf.initscript, slurm_ns_conf_buf);
	packbool(slurm_ns_conf.shared, slurm_ns_conf_buf);
}

static int _parse_ns_conf_internal(void **dest, slurm_parser_enum_t type,
				   const char *key, const char *value,
				   const char *line, char **leftover)
{
	char *basepath = NULL;
	int rc = 1;
	s_p_hashtbl_t *tbl = _create_ns_hashtbl();
	s_p_parse_line(tbl, *leftover, leftover);
	if (value) {
		basepath = xstrdup(value);
	} else if (!s_p_get_string(&basepath, "BasePath", tbl)) {
		fatal("empty basepath detected, please verify %s is correct",
		      ns_conf_file);
		rc = 0;
		goto end_it;
	}

	slurm_ns_conf.basepath =
		slurm_conf_expand_slurmd_path(basepath, conf->node_name, NULL);
	xfree(basepath);

#ifdef MULTIPLE_SLURMD
	xstrfmtcat(slurm_ns_conf.basepath, "/%s", conf->node_name);
#endif

	if (s_p_get_boolean(&slurm_ns_conf.auto_basepath, "AutoBasePath", tbl))
		auto_basepath_set = true;

	if (!s_p_get_string(&slurm_ns_conf.dirs, "Dirs", tbl))
		debug3("empty Dirs detected");

	if (!s_p_get_string(&slurm_ns_conf.initscript, "InitScript", tbl))
		debug3("empty init script detected");

	if (s_p_get_boolean(&slurm_ns_conf.shared, "Shared", tbl))
		shared_set = true;

	if (!s_p_get_string(&slurm_ns_conf.clonensscript, "CloneNSScript", tbl))
		debug3("empty post clone ns script detected");

	if (!s_p_get_string(&slurm_ns_conf.clonensepilog, "CloneNSEpilog", tbl))
		debug3("empty post clone ns epilog script detected");

	if (s_p_get_uint32(&slurm_ns_conf.clonensscript_wait,
			   "CloneNSScript_Wait", tbl))
		clonensscript_wait_set = true;

	if (s_p_get_uint32(&slurm_ns_conf.clonensepilog_wait,
			   "CloneNSEpilog_Wait", tbl))
		clonensepilog_wait_set = true;

	if (s_p_get_string(&slurm_ns_conf.clonensflags_str, "CloneNSFlags",
			   tbl))
		_set_clonensflags();

end_it:
	s_p_hashtbl_destroy(tbl);

	/* Nothing to free on this line in parse_config.c when freeing table. */
	*dest = NULL;
	return rc;
}

static int _parse_ns_conf(void **dest, slurm_parser_enum_t type,
			  const char *key, const char *value, const char *line,
			  char **leftover)
{
	if (value) {
		bool match = false;
		hostlist_t *hl = hostlist_create(value);
		if (hl) {
			match = (hostlist_find(hl, conf->node_name) >= 0);
			hostlist_destroy(hl);
		}
		if (!match) {
			s_p_hashtbl_t *tbl = _create_ns_hashtbl();
			s_p_parse_line(tbl, *leftover, leftover);
			s_p_hashtbl_destroy(tbl);
			debug("skipping NS for NodeName=%s %s", value, line);
			return 0;
		}
	}

	return _parse_ns_conf_internal(dest, type, key, NULL, line, leftover);
}

static int _read_slurm_ns_conf(void)
{
	char *conf_path = NULL;
	s_p_hashtbl_t *tbl = NULL;
	struct stat buf;
	int rc = SLURM_SUCCESS;

	static s_p_options_t options[] = {
		{ "AutoBasePath", S_P_BOOLEAN },
		{ "BasePath", S_P_ARRAY, _parse_ns_conf_internal, NULL },
		{ "CloneNSEpilog", S_P_STRING },
		{ "CloneNSFlags", S_P_STRING },
		{ "CloneNSScript", S_P_STRING },
		{ "CloneNSEpilog_Wait", S_P_UINT32 },
		{ "CloneNSScript_Wait", S_P_UINT32 },
		{ "Dirs", S_P_STRING },
		{ "NodeName", S_P_ARRAY, _parse_ns_conf, NULL },
		{ "Shared", S_P_BOOLEAN },
		{ NULL }
	};

	xassert(conf->node_name);

	conf_path = get_extra_conf_path(namespace_conf_file);

	if (stat(conf_path, &buf) == -1) {
		warning("Could not find %s file", namespace_conf_file);
		xfree(conf_path);
		conf_path = get_extra_conf_path(job_container_conf_file);
		if (stat(conf_path, &buf) == -1) {
			error("Could not find %s or %s file",
				namespace_conf_file, job_container_conf_file);
			rc = ENOENT;
			goto end_it;
		} else {
			ns_conf_file = job_container_conf_file;
			warning("Found %s file, please rename to %s.",
				job_container_conf_file, namespace_conf_file);
		}
	} else {
		ns_conf_file = namespace_conf_file;
	}

	debug("Reading %s file %s", ns_conf_file, conf_path);
	tbl = s_p_hashtbl_create(options);

	if (s_p_parse_file(tbl, NULL, conf_path, 0, NULL)) {
		fatal("Could not open/read/parse %s file %s",
		      ns_conf_file, conf_path);
	}

	/* If AutoBasePath wasn't set on the line see if it was on the global */
	if (!auto_basepath_set)
		s_p_get_boolean(&slurm_ns_conf.auto_basepath, "AutoBasePath",
				tbl);

	if (!slurm_ns_conf.dirs &&
	    !s_p_get_string(&slurm_ns_conf.dirs, "Dirs", tbl))
		slurm_ns_conf.dirs = xstrdup(SLURM_NEWNS_DEF_DIRS);

	if (!slurm_ns_conf.basepath) {
		debug("Config not found in %s. Disabling plugin on this node",
		      ns_conf_file);
	} else if (!xstrncasecmp(slurm_ns_conf.basepath, "none", 4)) {
		debug("Plugin is disabled on this node per %s.",
		      ns_conf_file);
	}

	if (!shared_set)
		s_p_get_boolean(&slurm_ns_conf.shared, "Shared", tbl);

	if (!clonensscript_wait_set) {
		if (!s_p_get_uint32(&slurm_ns_conf.clonensscript_wait,
				    "CloneNSScript_Wait", tbl))
			slurm_ns_conf.clonensscript_wait = 10;
	}

	if (!clonensepilog_wait_set) {
		if (!s_p_get_uint32(&slurm_ns_conf.clonensepilog_wait,
				    "CloneNSEpilog_Wait", tbl))
			slurm_ns_conf.clonensepilog_wait = 10;
	}

	if (!slurm_ns_conf.clonensflags_str)
		s_p_get_string(&slurm_ns_conf.clonensflags_str, "CloneNSFlags",
			       tbl);

	_set_clonensflags();

end_it:

	s_p_hashtbl_destroy(tbl);
	xfree(conf_path);

	return rc;
}

extern slurm_ns_conf_t *init_slurm_ns_conf(void)
{
	int rc;
	if (!slurm_ns_conf_inited) {
		char *save_ptr = NULL, *token, *buffer;
		memset(&slurm_ns_conf, 0, sizeof(slurm_ns_conf_t));
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

extern slurm_ns_conf_t *set_slurm_ns_conf(buf_t *buf)
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
	safe_unpackstr(&slurm_ns_conf.initscript, buf);
	safe_unpackbool(&slurm_ns_conf.shared, buf);
	slurm_ns_conf_inited = true;

	return &slurm_ns_conf;
unpack_error:
	return NULL;
}

extern slurm_ns_conf_t *get_slurm_ns_conf(void)
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
		xfree(slurm_ns_conf.basepath);
		xfree(slurm_ns_conf.clonensepilog);
		xfree(slurm_ns_conf.clonensflags_str);
		xfree(slurm_ns_conf.clonensscript);
		xfree(slurm_ns_conf.dirs);
		xfree(slurm_ns_conf.initscript);
		FREE_NULL_BUFFER(slurm_ns_conf_buf);
		slurm_ns_conf_inited = false;
	}
	return;
}

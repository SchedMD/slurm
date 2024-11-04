/*****************************************************************************\
 *  read_jcconf.c - parse job_container.conf configuration file.
 *****************************************************************************
 *  Copyright (C) 2019-2021 Regents of the University of California
 *  Produced at Lawrence Berkeley National Laboratory
 *  Written by Aditi Gaur <agaur@lbl.gov>
 *  All rights reserved.
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
#include <unistd.h>
#include <sys/stat.h>

#include "slurm/slurm_errno.h"

#include "src/common/xstring.h"
#include "src/common/log.h"
#include "src/common/parse_config.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"

#include "read_jcconf.h"

char *tmpfs_conf_file = "job_container.conf";

static slurm_jc_conf_t slurm_jc_conf;
static buf_t *slurm_jc_conf_buf = NULL;
static bool slurm_jc_conf_inited = false;
static bool auto_basepath_set = false;
static bool shared_set = false;
static bool entire_step_in_ns_set = false;
static bool clonensscript_wait_set = false;
static bool clonensepilog_wait_set = false;

static s_p_hashtbl_t *_create_ns_hashtbl(void)
{
	static s_p_options_t ns_options[] = {
		{"AutoBasePath", S_P_BOOLEAN},
		{"BasePath", S_P_STRING},
		{"Dirs", S_P_STRING},
		{"EntireStepInNS", S_P_BOOLEAN},
		{"InitScript", S_P_STRING},
		{"Shared", S_P_BOOLEAN},
		{"CloneNSScript", S_P_STRING},
		{"CloneNSEpilog", S_P_STRING},
		{"CloneNSScript_Wait", S_P_UINT32},
		{"CloneNSEpilog_Wait", S_P_UINT32},
		{NULL}
	};

	return s_p_hashtbl_create(ns_options);
}

static void _dump_jc_conf(void)
{
	if (!(slurm_conf.debug_flags & DEBUG_FLAG_JOB_CONT))
		return;

	log_flag(JOB_CONT, "AutoBasePath=%d", slurm_jc_conf.auto_basepath);
	log_flag(JOB_CONT, "BasePath=%s", slurm_jc_conf.basepath);
	log_flag(JOB_CONT, "Dirs=%s", slurm_jc_conf.dirs);
	log_flag(JOB_CONT, "EntireStepInNS=%d",
		 slurm_jc_conf.entire_step_in_ns);
	log_flag(JOB_CONT, "Shared=%d", slurm_jc_conf.shared);
	log_flag(JOB_CONT, "InitScript=%s", slurm_jc_conf.initscript);
	log_flag(JOB_CONT, "CloneNSScript=%s", slurm_jc_conf.clonensscript);
	log_flag(JOB_CONT, "CloneNSEpilog=%s", slurm_jc_conf.clonensepilog);
	log_flag(JOB_CONT, "CloneNSScript_Wait=%u",
		 slurm_jc_conf.clonensscript_wait);
	log_flag(JOB_CONT, "CloneNSEpilog_Wait=%u",
		 slurm_jc_conf.clonensepilog_wait);
}

static void _pack_slurm_jc_conf_buf(void)
{
	if (slurm_jc_conf_buf)
		FREE_NULL_BUFFER(slurm_jc_conf_buf);

	slurm_jc_conf_buf = init_buf(0);
	packbool(slurm_jc_conf.auto_basepath, slurm_jc_conf_buf);
	packstr(slurm_jc_conf.basepath, slurm_jc_conf_buf);
	packstr(slurm_jc_conf.dirs, slurm_jc_conf_buf);
	packbool(slurm_jc_conf.entire_step_in_ns, slurm_jc_conf_buf);
	packstr(slurm_jc_conf.initscript, slurm_jc_conf_buf);
	packbool(slurm_jc_conf.shared, slurm_jc_conf_buf);
	packstr(slurm_jc_conf.clonensscript, slurm_jc_conf_buf);
	packstr(slurm_jc_conf.clonensepilog, slurm_jc_conf_buf);
	pack32(slurm_jc_conf.clonensscript_wait, slurm_jc_conf_buf);
	pack32(slurm_jc_conf.clonensepilog_wait, slurm_jc_conf_buf);

}

static int _parse_jc_conf_internal(void **dest, slurm_parser_enum_t type,
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
		      tmpfs_conf_file);
		rc = 0;
		goto end_it;
	}

	slurm_jc_conf.basepath = slurm_conf_expand_slurmd_path(basepath,
							       conf->node_name,
							       NULL);
	xfree(basepath);

#ifdef MULTIPLE_SLURMD
	xstrfmtcat(slurm_jc_conf.basepath, "/%s", conf->node_name);
#endif

	if (s_p_get_boolean(&slurm_jc_conf.auto_basepath, "AutoBasePath", tbl))
		auto_basepath_set = true;

	if (!s_p_get_string(&slurm_jc_conf.dirs, "Dirs", tbl))
		debug3("empty Dirs detected");

	if (s_p_get_boolean(&slurm_jc_conf.entire_step_in_ns, "EntireStepInNS",
			    tbl))
		entire_step_in_ns_set = true;

	if (!s_p_get_string(&slurm_jc_conf.initscript, "InitScript", tbl))
		debug3("empty init script detected");

	if (s_p_get_boolean(&slurm_jc_conf.shared, "Shared", tbl))
		shared_set = true;

	if (!s_p_get_string(&slurm_jc_conf.clonensscript, "CloneNSScript", tbl))
		debug3("empty post clone ns script detected");

	if (!s_p_get_string(&slurm_jc_conf.clonensepilog, "CloneNSEpilog", tbl))
		debug3("empty post clone ns epilog script detected");

	if (s_p_get_uint32(&slurm_jc_conf.clonensscript_wait,
			   "CloneNSScript_Wait", tbl))
		clonensscript_wait_set = true;

	if (s_p_get_uint32(&slurm_jc_conf.clonensepilog_wait,
			   "CloneNSEpilog_Wait", tbl))
		clonensepilog_wait_set = true;

end_it:
	s_p_hashtbl_destroy(tbl);

	/* Nothing to free on this line in parse_config.c when freeing table. */
	*dest = NULL;
	return rc;
}

static int _parse_jc_conf(void **dest, slurm_parser_enum_t type,
			  const char *key, const char *value,
			  const char *line, char **leftover)
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

	return _parse_jc_conf_internal(dest, type, key, NULL, line, leftover);
}

static int _read_slurm_jc_conf(void)
{
	char *conf_path = NULL;
	s_p_hashtbl_t *tbl = NULL;
	struct stat buf;
	int rc = SLURM_SUCCESS;

	static s_p_options_t options[] = {
		{"AutoBasePath", S_P_BOOLEAN},
		{"BasePath", S_P_ARRAY, _parse_jc_conf_internal, NULL},
		{"Dirs", S_P_STRING},
		{"EntireStepInNS", S_P_BOOLEAN},
		{"NodeName", S_P_ARRAY, _parse_jc_conf, NULL},
		{"Shared", S_P_BOOLEAN},
		{"CloneNSScript", S_P_STRING},
		{"CloneNSEpilog", S_P_STRING},
		{"CloneNSScript_Wait", S_P_UINT32},
		{"CloneNSEpilog_Wait", S_P_UINT32},
		{NULL}
	};

	xassert(conf->node_name);

	conf_path = get_extra_conf_path(tmpfs_conf_file);

	if ((!conf_path) || (stat(conf_path, &buf) == -1)) {
		error("No %s file", tmpfs_conf_file);
		rc = ENOENT;
		goto end_it;
	}

	debug("Reading %s file %s", tmpfs_conf_file, conf_path);
	tbl = s_p_hashtbl_create(options);
	if (s_p_parse_file(tbl, NULL, conf_path, 0, NULL) == SLURM_ERROR) {
		fatal("Could not open/read/parse %s file %s",
		      tmpfs_conf_file, conf_path);
		goto end_it;
	}

	/* If AutoBasePath wasn't set on the line see if it was on the global */
	if (!auto_basepath_set)
		s_p_get_boolean(&slurm_jc_conf.auto_basepath,
				"AutoBasePath", tbl);

	if (!slurm_jc_conf.dirs &&
	    !s_p_get_string(&slurm_jc_conf.dirs, "Dirs", tbl))
		slurm_jc_conf.dirs = xstrdup(SLURM_TMPFS_DEF_DIRS);

	if (!slurm_jc_conf.basepath) {
		debug("Config not found in %s. Disabling plugin on this node",
		      tmpfs_conf_file);
	} else if (!xstrncasecmp(slurm_jc_conf.basepath, "none", 4)) {
		debug("Plugin is disabled on this node per %s.",
		      tmpfs_conf_file);
	}

	if (!entire_step_in_ns_set)
		s_p_get_boolean(&slurm_jc_conf.entire_step_in_ns,
				"EntireStepInNS", tbl);

	if (!shared_set)
		s_p_get_boolean(&slurm_jc_conf.shared, "Shared", tbl);

	if (!clonensscript_wait_set) {
		if (!s_p_get_uint32(&slurm_jc_conf.clonensscript_wait,
				    "CloneNSScript_Wait", tbl))
			slurm_jc_conf.clonensscript_wait = 10;
	}

	if (!clonensepilog_wait_set) {
		if (!s_p_get_uint32(&slurm_jc_conf.clonensepilog_wait,
				    "CloneNSEpilog_Wait", tbl))
			slurm_jc_conf.clonensepilog_wait = 10;
	}

end_it:

	s_p_hashtbl_destroy(tbl);
	xfree(conf_path);

	return rc;
}

extern slurm_jc_conf_t *init_slurm_jc_conf(void)
{
	int rc;
	if (!slurm_jc_conf_inited) {
		char *save_ptr = NULL, *token, *buffer;
		memset(&slurm_jc_conf, 0, sizeof(slurm_jc_conf_t));
		rc = _read_slurm_jc_conf();
		if (rc == SLURM_ERROR)
			return NULL;

		xassert(slurm_jc_conf.dirs);

		/* BasePath cannot be in "Dirs" */
		buffer = xstrdup(slurm_jc_conf.dirs);
		token = strtok_r(buffer, ",", &save_ptr);
		while (token) {
			char *found = xstrstr(token, slurm_jc_conf.basepath);
			if (found == token)
				fatal("BasePath(%s) cannot also be in Dirs.",
				      slurm_jc_conf.basepath);
			token = strtok_r(NULL, ",", &save_ptr);
		}
		xfree(buffer);

		_pack_slurm_jc_conf_buf();

		slurm_jc_conf_inited = true;

		_dump_jc_conf();
	}

	return &slurm_jc_conf;
}

extern slurm_jc_conf_t *set_slurm_jc_conf(buf_t *buf)
{
	xassert(buf);

	safe_unpackbool(&slurm_jc_conf.auto_basepath, buf);
	safe_unpackstr(&slurm_jc_conf.basepath, buf);
	safe_unpackstr(&slurm_jc_conf.dirs, buf);
	safe_unpackbool(&slurm_jc_conf.entire_step_in_ns, buf);
	safe_unpackstr(&slurm_jc_conf.initscript, buf);
	safe_unpackbool(&slurm_jc_conf.shared, buf);
	safe_unpackstr(&slurm_jc_conf.clonensscript, buf);
	safe_unpackstr(&slurm_jc_conf.clonensepilog, buf);
	safe_unpack32(&slurm_jc_conf.clonensscript_wait, buf);
	safe_unpack32(&slurm_jc_conf.clonensepilog_wait, buf);
	slurm_jc_conf_inited = true;

	return &slurm_jc_conf;
unpack_error:
	return NULL;
}

extern slurm_jc_conf_t *get_slurm_jc_conf(void)
{
	if (!slurm_jc_conf_inited)
		return NULL;
	return &slurm_jc_conf;
}

extern buf_t *get_slurm_jc_conf_buf(void)
{
	return slurm_jc_conf_buf;
}

extern void free_jc_conf(void)
{
	if (slurm_jc_conf_inited) {
		xfree(slurm_jc_conf.basepath);
		xfree(slurm_jc_conf.initscript);
		xfree(slurm_jc_conf.dirs);
		xfree(slurm_jc_conf.clonensscript);
		xfree(slurm_jc_conf.clonensepilog);
		FREE_NULL_BUFFER(slurm_jc_conf_buf);
		slurm_jc_conf_inited = false;
	}
	return;
}

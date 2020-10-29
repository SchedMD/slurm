/*****************************************************************************\
 *  read_nsconf.c - parse namespace.conf configuration file.
 *****************************************************************************
 *  Copyright (C) 2019-2021 Regents of the University of California
 *  Produced at Lawrence Berkeley National Laboratory
 *  Written by Aditi Gaur <agaur@lbl.gov>
 *  All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
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

#include "read_nsconf.h"

static slurm_ns_conf_t slurm_ns_conf;
static bool slurm_ns_conf_inited = false;

static int _read_slurm_ns_conf(void)
{
	char *conf_path = NULL;
	s_p_hashtbl_t **node_tbl = NULL;
	s_p_hashtbl_t *node = NULL;
	s_p_hashtbl_t *tbl = NULL;
	struct stat buf;
	int node_count;
	char *nodename = NULL;
	char *basepath = NULL;
	char *initscript = NULL;
	int rc = SLURM_SUCCESS;
	int i;
	s_p_options_t node_options[] = {
		{"NodeName", S_P_STRING},
		{"BasePath", S_P_PLAIN_STRING},
		{"InitScript", S_P_PLAIN_STRING},
		{NULL}
	};

	s_p_options_t options[] = {
		{"NodeName", S_P_EXPLINE, NULL, NULL, node_options},
		{NULL}
	};

	xassert(conf->node_name);

	conf_path = get_extra_conf_path("namespace.conf");

	if ((!conf_path) || (stat(conf_path, &buf) == -1)) {
		error("No namespace.conf file");
		return ENOENT;
	}
	debug("Reading namespace.conf file %s", conf_path);
	tbl = s_p_hashtbl_create(options);
	if (s_p_parse_file(tbl, NULL, conf_path, false) == SLURM_ERROR) {
		fatal("Could not open/read/parse namespace.conf file %s",
		      conf_path);
		return SLURM_ERROR;
	}

	if (!s_p_get_expline(&node_tbl, &node_count, "NodeName", tbl)) {
		error("Could not parse namespace.conf file %s", conf_path);
		return SLURM_ERROR;
	}
	for (i = 0; i < node_count; i++) {
		node = node_tbl[i];
		if (!s_p_get_string(&nodename, "NodeName", node)) {
			error("empty nodename detected, please verify namespace.conf is correct");
			rc = SLURM_ERROR;
			xfree(nodename);
			continue;
		}

		if (xstrcmp(nodename, conf->node_name)) {
			xfree(nodename);
			continue;
		}

		if (!s_p_get_string(&basepath, "BasePath", node)) {
			error("empty basepath detected, please verify namespace.conf is correct");
			rc = SLURM_ERROR;
			xfree(basepath);
			break;
		}
		slurm_ns_conf.basepath = basepath;

		if (!s_p_get_string(&initscript, "InitScript", node)) {
			debug3("empty init script detected");
			xfree(initscript);
			slurm_ns_conf.initscript = NULL;
		} else {
			slurm_ns_conf.initscript = initscript;
		}
		xfree(nodename);
		break;
	}

	if (!slurm_ns_conf.basepath) {
		error("Configuration for this node not found in namespace.conf");
		rc = SLURM_ERROR;
	}
	s_p_hashtbl_destroy(tbl);
	xfree(conf_path);

	return rc;
}

extern slurm_ns_conf_t *get_slurm_ns_conf(void)
{
	int rc;
	if (!slurm_ns_conf_inited) {
		memset(&slurm_ns_conf, 0, sizeof(slurm_ns_conf_t));
		rc = _read_slurm_ns_conf();
		if (rc == SLURM_ERROR)
			return NULL;
		slurm_ns_conf_inited = true;
	}

	return &slurm_ns_conf;
}

extern void free_ns_conf(void)
{
	if (slurm_ns_conf_inited) {
		xfree(slurm_ns_conf.basepath);
		xfree(slurm_ns_conf.initscript);
	}
	return;
}

/*****************************************************************************\
 *  nonstop.c - Define optional plugin for fault tolerant application support
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC
 *  Written by Morris Jette <jette@schedmd.com>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com>.
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

#include "src/common/slurm_xlator.h"	/* Must be first */
#include "src/plugins/slurmctld/nonstop/do_work.h"
#include "src/plugins/slurmctld/nonstop/read_config.h"
#include "src/plugins/slurmctld/nonstop/msg.h"
#include "src/slurmctld/slurmctld_plugstack.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - A string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - A string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "auth" for Slurm authentication) and <method> is a
 * description of how this plugin satisfies that application.  Slurm will
 * only load authentication plugins if the plugin_type string has a prefix
 * of "auth/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char	plugin_name[]	= "Slurmctld Fault Tolerance plugin";
const char	plugin_type[]	= "slurmctld/nonstop";
const uint32_t	plugin_version	= SLURM_VERSION_NUMBER;

extern int init(void)
{
	int rc;

	nonstop_read_config();
	init_job_db();
	create_hot_spare_resv();
	(void) restore_nonstop_state();
	rc = spawn_msg_thread() + spawn_state_thread();
	nonstop_ops.job_begin = job_begin_callback;
	nonstop_ops.job_fini  = job_fini_callback;
	nonstop_ops.node_fail = node_fail_callback;
	verbose("%s loaded", plugin_name);

	return rc;
}

extern int fini(void)
{
	term_msg_thread();
	term_state_thread();
	nonstop_free_config();
	term_job_db();

	return SLURM_SUCCESS;
}

/* Get node features plugin configuration */
extern void slurmctld_plugstack_p_get_config(config_plugin_params_t *p)
{
	xstrcat(p->name, plugin_type);
	nonstop_read_config_list(p->key_pairs);
}

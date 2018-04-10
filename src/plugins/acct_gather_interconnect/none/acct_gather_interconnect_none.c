/*****************************************************************************\
 *  acct_gather_interconnect_none.c - slurm interconnect accounting plugin for none.
 *****************************************************************************
 *  Copyright (C) 2013 BULL
 *  Written by Yiannis Georgiou <yiannis.georgiou@bull.net>,
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
 *
 *  This file is patterned after jobcomp_linux.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/


/*   acct_gather_interconnect_none
 * This plugin does not initiate a node-level thread.
 * It is the acct_gather_interconnect stub.
 */

#include "src/common/slurm_xlator.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/slurmd/common/proctrack.h"

#include <fcntl.h>
#include <signal.h>

#define _DEBUG 1
#define _DEBUG_INTERCONNECT 1

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "jobacct" for Slurm job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  Slurm will
 * only load job completion logging plugins if the plugin_type string has a
 * prefix of "jobacct/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[] = "AcctGatherInterconnect NONE plugin";
const char plugin_type[] = "acct_gather_interconnect/none";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;
/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	debug("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

extern int fini(void)
{
	return SLURM_SUCCESS;
}

extern int acct_gather_interconnect_p_node_update(void)
{
	return SLURM_SUCCESS;
}

extern void acct_gather_interconnect_p_conf_set(s_p_hashtbl_t *tbl)
{
	return;
}

extern void acct_gather_interconnect_p_conf_options(
	s_p_options_t **full_options, int *full_options_cnt)
{
	return;
}

extern void acct_gather_interconnect_p_conf_values(List *data)
{
	return;
}

extern int acct_gather_interconnect_p_get_data(acct_gather_data_t *data)
{
	return SLURM_SUCCESS;
}

/*****************************************************************************\
 *  slurmscriptd_protocol_defs.c - functions used for initializing and
 *	releasing storage for slurmscriptd RPC data structures.
 *	This is only used for communication between slurmctld and slurmscriptd.
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC.
 *  Written by Marshall Garey <marshall@schedmd.com>
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

#include "src/common/xmalloc.h"
#include "src/slurmctld/slurmscriptd_protocol_defs.h"

extern void slurmscriptd_free_reconfig(reconfig_msg_t *msg)
{
	if (!msg)
		return;

	xfree(msg->logfile);
	xfree(msg);
}

extern void slurmscriptd_free_run_script_msg(run_script_msg_t *msg)
{
	if (!msg)
		return;

	xfree_array(msg->argv);
	xfree_array(msg->env);
	xfree(msg->extra_buf);
	xfree(msg->script_name);
	xfree(msg->script_path);
	xfree(msg->tmp_file_env_name);
	xfree(msg->tmp_file_str);
	xfree(msg);
}

extern void slurmscriptd_free_script_complete(script_complete_t *msg)
{
	if (!msg)
		return;

	xfree(msg->resp_msg);
	xfree(msg->script_name);
	xfree(msg);
}

extern void slurmscriptd_free_msg(slurmscriptd_msg_t *msg)
{
	if (!msg)
		return;

	switch(msg->msg_type) {
	case SLURMSCRIPTD_REQUEST_RECONFIG:
		slurmscriptd_free_reconfig(msg->msg_data);
		break;
	case SLURMSCRIPTD_REQUEST_RUN_SCRIPT:
		slurmscriptd_free_run_script_msg(msg->msg_data);
		break;
	case SLURMSCRIPTD_REQUEST_SCRIPT_COMPLETE:
		slurmscriptd_free_script_complete(msg->msg_data);
		break;
	default:
		xfree(msg->msg_data); /* Nothing internal to free */
		break;
	}
	xfree(msg->key);
}

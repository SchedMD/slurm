/*****************************************************************************\
 *  jobacct_none.c - NO-OP slurm job completion logging plugin.
 *****************************************************************************
 *
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
 *  Written by Andy Riebs, <andy.riebs@hp.com>.
 *  CODE-OCEC-09-009. All rights reserved.
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
 *
 *  This file is patterned after jobcomp_none.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#include "src/common/slurm_xlator.h"
#include "src/common/slurm_jobacct_gather.h"

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
const char plugin_name[] = "Job accounting gather NOT_INVOKED plugin";
const char plugin_type[] = "jobacct_gather/none";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	debug("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	return SLURM_SUCCESS;
}

extern void jobacct_gather_p_poll_data(List task_list, bool pgid_plugin,
				       uint64_t cont_id)
{
	return;
}

extern jobacctinfo_t *jobacct_gather_p_create(jobacct_id_t *jobacct_id)
{
	return NULL;
}

extern void jobacct_gather_p_destroy(struct jobacctinfo *jobacct)
{
	return;
}

extern int jobacct_gather_p_setinfo(struct jobacctinfo *jobacct,
				    enum jobacct_data_type type, void *data)
{
	return SLURM_SUCCESS;

}

extern int jobacct_gather_p_getinfo(struct jobacctinfo *jobacct,
				    enum jobacct_data_type type, void *data)
{
	return SLURM_SUCCESS;
}

extern void jobacct_gather_p_pack(struct jobacctinfo *jobacct, Buf buffer)
{
	return;
}

extern int jobacct_gather_p_unpack(struct jobacctinfo **jobacct, Buf buffer)
{
	*jobacct = NULL;
	return SLURM_SUCCESS;
}

extern void jobacct_gather_p_aggregate(struct jobacctinfo *dest,
				       struct jobacctinfo *from)
{
	return;
}

extern int jobacct_gather_p_startpoll(uint16_t frequency)
{
	return SLURM_SUCCESS;
}

extern int jobacct_gather_p_endpoll(void)
{
	return SLURM_SUCCESS;
}

extern void jobacct_gather_p_suspend_poll(void)
{
	return;
}

extern void jobacct_gather_p_resume_poll(void)
{
	return;
}

extern int jobacct_gather_p_set_proctrack_container_id(uint64_t id)
{
	return SLURM_SUCCESS;
}

extern int jobacct_gather_p_add_task(pid_t pid, jobacct_id_t *jobacct_id)
{
	return SLURM_SUCCESS;
}


extern jobacctinfo_t *jobacct_gather_p_stat_task(pid_t pid)
{
	return NULL;
}

extern jobacctinfo_t *jobacct_gather_p_remove_task(pid_t pid)
{
	return NULL;
}

extern void jobacct_gather_p_2_stats(slurmdb_stats_t *stats,
				     struct jobacctinfo *jobacct)
{
	return;
}

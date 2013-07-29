/*****************************************************************************\
 *  switch_generic.c - Library for managing a generic switch resources.
 *                     Can be used to optimize network communications for
 *                     parallel jobs.
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC
 *  Written by Morris Jette <jette@schedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <net/if.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"

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
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "switch" for SLURM switch) and <method> is a description
 * of how this plugin satisfies that application.  SLURM will only load
 * a switch plugin if the plugin_type string has a prefix of "switch/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum version for their plugins as this API matures.
 */
const char plugin_name[]        = "switch generic plugin";
const char plugin_type[]        = "switch/generic";
const uint32_t plugin_version   = 110;

uint32_t debug_flags = 0;

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
int init(void)
{
	verbose("%s loaded", plugin_name);
	debug_flags = slurm_get_debug_flags();
	return SLURM_SUCCESS;
}

int fini(void)
{
	return SLURM_SUCCESS;
}

extern int switch_p_reconfig(void)
{
	debug_flags = slurm_get_debug_flags();
	return SLURM_SUCCESS;
}

/*
 * switch functions for global state save/restore
 */
int switch_p_libstate_save(char * dir_name)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_libstate_save() starting");
	return SLURM_SUCCESS;
}

int switch_p_libstate_restore(char * dir_name, bool recover )
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_libstate_restore() starting");
	return SLURM_SUCCESS;
}

int switch_p_libstate_clear(void)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_libstate_clear() starting");
	return SLURM_SUCCESS;
}

/*
 * switch functions for job step specific credential
 */
int switch_p_alloc_jobinfo(switch_jobinfo_t **switch_job,
			   uint32_t job_id, uint32_t step_id )
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_alloc_jobinfo() starting");
	return SLURM_SUCCESS;
}

int switch_p_build_jobinfo(switch_jobinfo_t *switch_job,
			   slurm_step_layout_t *step_layout, char *network)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_build_jobinfo() starting");
	return SLURM_SUCCESS;
}

switch_jobinfo_t *switch_p_copy_jobinfo(switch_jobinfo_t *switch_job)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_copy_jobinfo() starting");
	return NULL;
}

void switch_p_free_jobinfo(switch_jobinfo_t *switch_job)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_free_jobinfo() starting");
	return;
}

int switch_p_pack_jobinfo(switch_jobinfo_t *switch_job, Buf buffer)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_pack_jobinfo() starting");
	return 0;
}

int switch_p_unpack_jobinfo(switch_jobinfo_t *switch_job, Buf buffer)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_unpack_jobinfo() starting");
	return SLURM_SUCCESS;
}

void switch_p_print_jobinfo(FILE *fp, switch_jobinfo_t *jobinfo)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_print_jobinfo() starting");
	return;
}

char *switch_p_sprint_jobinfo(switch_jobinfo_t *switch_jobinfo, char *buf,
			      size_t size)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_sprint_jobinfo() starting");
	if ((buf != NULL) && size) {
		buf[0] = '\0';
		return buf;
	}

	return NULL;
}

/*
 * switch functions for job initiation
 */
int switch_p_node_init(void)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_node_init() starting");
	return SLURM_SUCCESS;
}

int switch_p_node_fini(void)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_node_fini() starting");
	return SLURM_SUCCESS;
}

int switch_p_job_preinit(switch_jobinfo_t *jobinfo)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_preinit() starting");
	return SLURM_SUCCESS;
}

extern int switch_p_job_init(stepd_step_rec_t *job)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_init() starting");
	return SLURM_SUCCESS;
}

extern int switch_p_job_suspend_test(switch_jobinfo_t *jobinfo)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_suspend_test() starting");
	return SLURM_SUCCESS;
}

extern void switch_p_job_suspend_info_get(switch_jobinfo_t *jobinfo,
					  void **suspend_info)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_suspend_info_get() starting");
	return;
}

extern void switch_p_job_suspend_info_pack(void *suspend_info, Buf buffer)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_suspend_info_pack() starting");
	return;
}

extern int switch_p_job_suspend_info_unpack(void **suspend_info, Buf buffer)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_suspend_info_unpack() starting");
	return SLURM_SUCCESS;
}

extern void switch_p_job_suspend_info_free(void *suspend_info)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_suspend_info_free() starting");
	return;
}

extern int switch_p_job_suspend(void *suspend_info, int max_wait)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_suspend() starting");
	return SLURM_SUCCESS;
}

extern int switch_p_job_resume(void *suspend_info, int max_wait)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_resume() starting");
	return SLURM_SUCCESS;
}

int switch_p_job_fini(switch_jobinfo_t *jobinfo)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_fini() starting");
	return SLURM_SUCCESS;
}

int switch_p_job_postfini(switch_jobinfo_t *jobinfo, uid_t pgid,
			  uint32_t job_id, uint32_t step_id)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_postfini() starting");
	/*
	 *  Kill all processes in the job's session
	 */
	if (pgid) {
		debug2("Sending SIGKILL to pgid %lu",
			(unsigned long) pgid);
		kill(-pgid, SIGKILL);
	} else
		debug("Job %u.%u: Bad pid valud %lu", job_id,
		      step_id, (unsigned long) pgid);

	return SLURM_SUCCESS;
}

int switch_p_job_attach(switch_jobinfo_t *jobinfo, char ***env,
			uint32_t nodeid, uint32_t procid, uint32_t nnodes,
			uint32_t nprocs, uint32_t rank)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_attach() starting");
	return SLURM_SUCCESS;
}

extern int switch_p_get_jobinfo(switch_jobinfo_t *switch_job,
				int key, void *resulting_data)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_get_jobinfoe() starting");
	slurm_seterrno(EINVAL);
	return SLURM_ERROR;
}

/*
 * switch functions for other purposes
 */
extern int switch_p_get_errno(void)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_get_errno() starting");
	return SLURM_SUCCESS;
}

extern char *switch_p_strerror(int errnum)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_strerror() starting");
	return NULL;
}

/*
 * node switch state monitoring functions
 * required for IBM Federation switch
 */
extern int switch_p_clear_node_state(void)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_clear_node_state() starting");
	return SLURM_SUCCESS;
}

extern int switch_p_alloc_node_info(switch_node_info_t **switch_node)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_alloc_node_inf0() starting");
	return SLURM_SUCCESS;
}

extern int switch_p_build_node_info(switch_node_info_t *switch_node)
{
	struct ifaddrs *if_array = NULL, *if_rec;
	void *addr_ptr = NULL;
	char addr_str[INET6_ADDRSTRLEN], *ip_vers;

	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_build_node_info() starting");
	if (getifaddrs(&if_array) == 0) {
		for (if_rec = if_array; if_rec; if_rec = if_rec->ifa_next) {
			if (!if_rec->ifa_addr->sa_data)
				continue;
	   		if (if_rec->ifa_flags & IFF_LOOPBACK)
				continue;
			if (if_rec->ifa_addr->sa_family == AF_INET) {
				addr_ptr = &((struct sockaddr_in *)
						if_rec->ifa_addr)->sin_addr;
				ip_vers = "IP_V4";
			} else if (if_rec->ifa_addr->sa_family == AF_INET6) {
				addr_ptr = &((struct sockaddr_in6 *)
						if_rec->ifa_addr)->sin6_addr;
				ip_vers = "IP_V6";
			} else {
				/* AF_PACKET (statistics) and others ignored */
				continue;
			}
			(void) inet_ntop(if_rec->ifa_addr->sa_family,
					 addr_ptr, addr_str, sizeof(addr_str));
			if (debug_flags & DEBUG_FLAG_SWITCH) {
				info("%s name=%s ip_version=%s address=%s",
				       plugin_type, if_rec->ifa_name, ip_vers,
				       addr_str);
			}
		}
	}
	freeifaddrs(if_array);

	return SLURM_SUCCESS;
}

extern int switch_p_pack_node_info(switch_node_info_t *switch_node,
				   Buf buffer)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_pack_node_info() starting");
	return 0;
}

extern int switch_p_unpack_node_info(switch_node_info_t *switch_node,
				     Buf buffer)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_unpack_node_info() starting");
	return SLURM_SUCCESS;
}

extern int switch_p_free_node_info(switch_node_info_t **switch_node)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_free_node_info() starting");
	return SLURM_SUCCESS;
}

extern char *switch_p_sprintf_node_info(switch_node_info_t *switch_node,
				        char *buf, size_t size)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_sprintf_node_info() starting");
	if ((buf != NULL) && size) {
		buf[0] = '\0';
		return buf;
	}

	return NULL;
}

extern int switch_p_job_step_complete(switch_jobinfo_t *jobinfo,
				      char *nodelist)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_step_complete() starting");
	return SLURM_SUCCESS;
}

extern int switch_p_job_step_part_comp(switch_jobinfo_t *jobinfo,
				       char *nodelist)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_step_part_comp() starting");
	return SLURM_SUCCESS;
}

extern bool switch_p_part_comp(void)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_part_comp() starting");
	return false;
}

extern int switch_p_job_step_allocated(switch_jobinfo_t *jobinfo,
				       char *nodelist)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_step_allocated() starting");
	return SLURM_SUCCESS;
}

extern int switch_p_slurmctld_init(void)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_slurmctld_init() starting");
	return SLURM_SUCCESS;
}

extern int switch_p_slurmd_init(void)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_slurmd_init() starting");
	return SLURM_SUCCESS;
}

extern int switch_p_slurmd_step_init(void)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_slurmd_step_init() starting");
	return SLURM_SUCCESS;
}

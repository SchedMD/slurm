/*****************************************************************************\
 * src/slurmd/common/slurmstepd_init.c - slurmstepd intialization code
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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
\*****************************************************************************/

#include "src/slurmd/common/slurmstepd_init.h"
#include "src/common/xstring.h"

#define PROTOCOL_VERSION	"PROTOCOL_VERSION"

/* Assume that the slurmd and slurmstepd are the same version level when slurmd
 * starts slurmstepd, so we do not need to support different protocol versions
 * for the different message formats. */
extern void pack_slurmd_conf_lite(slurmd_conf_t *conf, Buf buffer)
{
	xassert(conf != NULL);
	packstr(PROTOCOL_VERSION, buffer);
	pack16(SLURM_PROTOCOL_VERSION, buffer);

	packstr(conf->hostname, buffer);
	pack16(conf->cpus, buffer);
	pack16(conf->boards, buffer);
	pack16(conf->sockets, buffer);
	pack16(conf->cores, buffer);
	pack16(conf->threads, buffer);
	pack64(conf->real_memory_size, buffer);
	pack16(conf->block_map_size, buffer);
	pack16_array(conf->block_map, conf->block_map_size, buffer);
	pack16_array(conf->block_map_inv, conf->block_map_size, buffer);
	packstr(conf->spooldir, buffer);
	packstr(conf->node_name, buffer);
	packstr(conf->logfile, buffer);
	packstr(conf->task_prolog, buffer);
	packstr(conf->task_epilog, buffer);
	packstr(conf->job_acct_gather_freq, buffer);
	packstr(conf->job_acct_gather_type, buffer);
	pack16(conf->propagate_prio, buffer);
	pack64(conf->debug_flags, buffer);
	pack32(conf->debug_level, buffer);
	pack32(conf->daemonize, buffer);
	pack32((uint32_t)conf->slurm_user_id, buffer);
	pack16(conf->use_pam, buffer);
	pack32(conf->task_plugin_param, buffer);
	packstr(conf->node_topo_addr, buffer);
	packstr(conf->node_topo_pattern, buffer);
	pack32((uint32_t)conf->port, buffer);
	pack16(conf->log_fmt, buffer);
	pack16(conf->mem_limit_enforce, buffer);
	pack64(conf->msg_aggr_window_msgs, buffer);
	packstr(conf->tmpfs, buffer);
	packstr(conf->x11_params, buffer);
}

extern int unpack_slurmd_conf_lite_no_alloc(slurmd_conf_t *conf, Buf buffer)
{
	uint32_t uint32_tmp;
	uint16_t protocol_version;
	char *ver_str = NULL;

	safe_unpackstr_xmalloc(&ver_str, &uint32_tmp, buffer);
	safe_unpack16(&protocol_version, buffer);
	xfree(ver_str);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&conf->hostname, &uint32_tmp, buffer);
		safe_unpack16(&conf->cpus, buffer);
		safe_unpack16(&conf->boards, buffer);
		safe_unpack16(&conf->sockets, buffer);
		safe_unpack16(&conf->cores, buffer);
		safe_unpack16(&conf->threads, buffer);
		safe_unpack64(&conf->real_memory_size, buffer);
		safe_unpack16(&conf->block_map_size, buffer);
		safe_unpack16_array(&conf->block_map, &uint32_tmp, buffer);
		safe_unpack16_array(&conf->block_map_inv,  &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&conf->spooldir,    &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&conf->node_name,   &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&conf->logfile,     &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&conf->task_prolog, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&conf->task_epilog, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&conf->job_acct_gather_freq, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&conf->job_acct_gather_type, &uint32_tmp,
				       buffer);
		safe_unpack16(&conf->propagate_prio, buffer);
		safe_unpack64(&conf->debug_flags, buffer);
		safe_unpack32(&uint32_tmp, buffer);
		conf->debug_level = uint32_tmp;
		safe_unpack32(&uint32_tmp, buffer);
		conf->daemonize = uint32_tmp;
		safe_unpack32(&uint32_tmp, buffer);
		conf->slurm_user_id = (uid_t)uint32_tmp;
		safe_unpack16(&conf->use_pam, buffer);
		safe_unpack32(&conf->task_plugin_param, buffer);
		safe_unpackstr_xmalloc(&conf->node_topo_addr, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&conf->node_topo_pattern, &uint32_tmp, buffer);
		safe_unpack32(&uint32_tmp, buffer);
		conf->port = uint32_tmp;
		safe_unpack16(&conf->log_fmt, buffer);
		safe_unpack16(&conf->mem_limit_enforce, buffer);
		safe_unpack64(&conf->msg_aggr_window_msgs, buffer);
		safe_unpackstr_xmalloc(&conf->tmpfs, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&conf->x11_params, &uint32_tmp, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	error("unpack_error in unpack_slurmd_conf_lite_no_alloc: %m");
	xfree(conf->job_acct_gather_freq);
	xfree(conf->job_acct_gather_type);
	xfree(conf->hostname);
	xfree(conf->spooldir);
	xfree(conf->node_name);
	xfree(conf->logfile);
	xfree(conf->task_prolog);
	xfree(conf->task_epilog);
	xfree(conf->node_topo_addr);
	xfree(conf->node_topo_pattern);
	xfree(conf->tmpfs);
	xfree(conf->x11_params);
	return SLURM_ERROR;
}

/*****************************************************************************\
 * src/slurmd/common/slurmstepd_init.c - slurmstepd intialization code
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#include "src/slurmd/common/slurmstepd_init.h"

extern void pack_slurmd_conf_lite(slurmd_conf_t *conf, Buf buffer)
{
	xassert(conf != NULL);
	packstr(conf->hostname, buffer);
	pack16(conf->sockets, buffer);
	pack16(conf->cores, buffer);
	pack16(conf->threads, buffer);
	packstr(conf->spooldir, buffer);
	packstr(conf->node_name, buffer);
	packstr(conf->logfile, buffer);
	packstr(conf->task_prolog, buffer);
	packstr(conf->task_epilog, buffer);
	pack16(conf->job_acct_gather_freq, buffer);
	pack16(conf->propagate_prio, buffer);
	pack32(conf->debug_level, buffer);
	pack32(conf->daemonize, buffer);
	pack32((uint32_t)conf->slurm_user_id, buffer);
	pack16(conf->use_pam, buffer);
	pack16(conf->use_cpusets, buffer);
}

extern int unpack_slurmd_conf_lite_no_alloc(slurmd_conf_t *conf, Buf buffer)
{
	uint32_t uint32_tmp;

	safe_unpackstr_xmalloc(&conf->hostname, &uint32_tmp, buffer);
	safe_unpack16(&conf->sockets, buffer);
	safe_unpack16(&conf->cores, buffer);
	safe_unpack16(&conf->threads, buffer);
	safe_unpackstr_xmalloc(&conf->spooldir,    &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&conf->node_name,   &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&conf->logfile,     &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&conf->task_prolog, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&conf->task_epilog, &uint32_tmp, buffer);
	safe_unpack16(&conf->job_acct_gather_freq, buffer);
	safe_unpack16(&conf->propagate_prio, buffer);
	safe_unpack32(&uint32_tmp, buffer);
	conf->debug_level = uint32_tmp;
	safe_unpack32(&uint32_tmp, buffer);
	conf->daemonize = uint32_tmp;
	safe_unpack32(&uint32_tmp, buffer);
	conf->slurm_user_id = (uid_t)uint32_tmp;
	safe_unpack16(&conf->use_pam, buffer);
	safe_unpack16(&conf->use_cpusets, buffer);
	return SLURM_SUCCESS;

unpack_error:
	error("unpack_error in unpack_slurmd_conf_lite_no_alloc: %m");
	xfree(conf->hostname);
	xfree(conf->spooldir);
	xfree(conf->node_name);
	xfree(conf->logfile);
	xfree(conf->task_prolog);
	xfree(conf->task_epilog);
	return SLURM_ERROR;
}

/*****************************************************************************\
 * src/slurmd/common/slurmstepd_init.c - slurmstepd intialization code
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  UCRL-CODE-217948.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include "src/slurmd/common/slurmstepd_init.h"

extern void pack_slurmd_conf_lite(slurmd_conf_t *conf, Buf buffer)
{
	xassert(conf != NULL);
	packstr(conf->hostname, buffer);
	packstr(conf->spooldir, buffer);
	packstr(conf->node_name, buffer);
	packstr(conf->logfile, buffer);
	packstr(conf->cf.job_acct_parameters, buffer);
	pack32(conf->debug_level, buffer);
	pack32(conf->daemonize, buffer);
	pack32((uint32_t)conf->slurm_user_id, buffer);
}

extern int unpack_slurmd_conf_lite_no_alloc(slurmd_conf_t *conf, Buf buffer)
{
	uint16_t uint16_tmp;
	uint32_t uint32_tmp;
	safe_unpackstr_xmalloc(&conf->hostname, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc(&conf->spooldir, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc(&conf->node_name, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc(&conf->logfile, &uint16_tmp, buffer);
	safe_unpackstr_xmalloc(&conf->cf.job_acct_parameters, 
			       &uint16_tmp, buffer);
	safe_unpack32(&uint32_tmp, buffer);
	conf->debug_level = uint32_tmp;
	safe_unpack32(&uint32_tmp, buffer);
	conf->daemonize = uint32_tmp;
	safe_unpack32(&uint32_tmp, buffer);
	conf->slurm_user_id = (uid_t)uint32_tmp;
	return SLURM_SUCCESS;

unpack_error:
	error("unpack_error in unpack_slurmd_conf_lite_no_alloc: %m");
	return SLURM_ERROR;
}

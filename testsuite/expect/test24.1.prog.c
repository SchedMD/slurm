/*****************************************************************************\
 *  test24.1.prog.c - link and test algo of the multifactor plugin.
 *  
 *  Usage: test24.1.prog 
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
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
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif			/* HAVE_INTTYPES_H */

#include <time.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/common/slurm_priority.h"
#include "src/common/assoc_mgr.h"
#include "src/common/xstring.h"
#include "src/common/log.h"

int main (int argc, char **argv)
{
	log_options_t logopt = LOG_OPTS_STDERR_ONLY;
	slurm_ctl_conf_t *conf = NULL;

	log_init(xbasename(argv[0]), logopt, 0, NULL);
	xfree(slurmctld_conf.priority_type);
	
	conf = slurm_conf_lock();
	/* force priority type to be multifactor */
	xfree(conf->priority_type);
	conf->priority_type = xstrdup("priority/multifactor");
	/* set up a known environment to test against.  Since we are
	   only concerned about the fairshare we won't look at the other
	   factors here.
	*/
	conf->priority_decay_hl = DEFAULT_PRIORITY_DECAY;
	conf->priority_favor_small = 0;
	conf->priority_max_age = DEFAULT_PRIORITY_DECAY;
	conf->priority_weight_age = 0;
	conf->priority_weight_fs = 10000;
	conf->priority_weight_js = 0;
	conf->priority_weight_part = 0;
	conf->priority_weight_qos = 0;
	slurm_conf_unlock();

	info("success");
	return 0;
}

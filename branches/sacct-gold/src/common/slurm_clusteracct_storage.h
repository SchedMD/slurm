/*****************************************************************************\
 *  slurm_clusteracct_storage.h - Define storage plugin functions.
 *
 * $Id: slurm_clusteracct_storage.h 10574 2006-12-15 23:38:29Z jette $
 *****************************************************************************
 *  Copyright (C) 2004-2008 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  UCRL-CODE-226842.
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

#ifndef _SLURM_CLUSTERACCT_STORAGE_H 
#define _SLURM_CLUSTERACCT_STORAGE_H

#include "src/common/list.h"
#include "src/slurmctld/slurmctld.h"
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

typedef struct {
	char *cluster; /* cluster name */
	uint32_t cpu_count; /* number of cpus during time period */
	time_t period_start; /* when this record was started */
	time_t period_end; /* when it ended */
	uint32_t idle_secs; /* number of cpu seconds idle */
	uint32_t down_secs; /* number of cpu seconds down */
	uint32_t alloc_secs; /* number of cpu seconds allocated */
	uint32_t resv_secs; /* number of cpu seconds reserved */	
} clusteracct_rec_t;

extern void destroy_clusteracct_rec(void *object);

extern int slurm_clusteracct_storage_init(void); /* load the plugin */
extern int slurm_clusteracct_storage_fini(void); /* unload the plugin */


extern int clusteracct_storage_g_node_down(struct node_record *node_ptr,
					   time_t event_time,
					   char *reason);

extern int clusteracct_storage_g_node_up(struct node_record *node_ptr,
					 time_t event_time);

extern int clusteracct_storage_g_cluster_procs(uint32_t procs,
					       time_t event_time);

extern List clusteracct_storage_g_get_hourly_usage(char *cluster, time_t start, 
						   time_t end, void *params);

extern List clusteracct_storage_g_get_daily_usage(char *cluster, time_t start, 
						  time_t end, void *params);

extern List clusteracct_storage_g_get_monthly_usage(char *cluster, 
						    time_t start, 
						    time_t end,
						    void *params);
#endif /*_SLURM_CLUSTERACCT_STORAGE_H*/

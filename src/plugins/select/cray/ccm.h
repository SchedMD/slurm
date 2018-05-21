/*****************************************************************************\
 *  ccm.h
 *****************************************************************************
 *  Copyright (C) 2016 SchedMD LLC
 *  Copyright 2016 Cray Inc. All Rights Reserved.
 *  Written by Marlys Kohnke
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

#ifndef SELECT_CRAY_CCM_H
#define SELECT_CRAY_CCM_H

#include <inttypes.h>
#include <pthread.h>
#include <string.h>

#include "src/common/slurm_xlator.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/srun_comm.h"

/*
 * CCM will be used to provide app ssh launch using the Cray network
 * interconnect.
 */
#define CCM_MAX_EPILOG_DELAY	30
#define CCM_MAX_PTHREAD_RETRIES  6
#define CCM_PARTITION_MAX	32
#define CCM_CRAY_UNIQUE_FILENAME  "/tmp/crayCCMXXXXXX"
#define CCM_PROLOG_PATH		"/opt/cray/ccm/default/etc/ccm-prologue"
#define CCM_EPILOG_PATH		"/opt/cray/ccm/default/etc/ccm-epilogue"
#define CCM_CONF_PATH           "/etc/opt/cray/ccm/ccm.conf"

typedef struct {
	char *ccm_partition[CCM_PARTITION_MAX];
	int num_ccm_partitions;
	int ccm_enabled;
} ccm_config_t;

extern ccm_config_t ccm_config;
extern const char *ccm_prolog_path;
extern const char *ccm_epilog_path;


typedef struct {
	uint32_t  job_id;
	uint32_t  user_id;
	uint32_t  node_cnt;		/* Number of allocated nodes */
	uint32_t  num_tasks;		/* Number of app PEs/tasks to exec */
	uint32_t  num_cpu_groups;	/* Number of entries in cpus arrays */
	uint32_t *cpu_count_reps;	/* Number of reps of each cpu count */
	uint16_t *cpus_per_node;	/* Number of cpus per node */
	uint16_t  cpus_per_task;	/* Number of cpus per app task/PE */
	uint16_t  task_dist;
	uint16_t  plane_size;
	char    *nodelist;		/* Allocated node hostname list */
} ccm_info_t;

#define CRAY_ERR(fmt, ...) error("(%s: %d: %s) "fmt, THIS_FILE, __LINE__, \
				 __FUNCTION__, ##__VA_ARGS__);

extern void ccm_get_config(void);
extern int ccm_check_partitions(struct job_record *job_ptr);
extern void *ccm_begin(void *args);
extern void *ccm_fini(void *args);

#endif /* CRAY_SELECT_CCM_H */

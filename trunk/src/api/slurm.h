/*****************************************************************************\
 *  slurm.h - Definitions for all of the SLURM RPCs
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette1@llnl.gov>, Joey Ekstrom <ekstrom1@llnl.gov> et. al.
 *  UCRL-CODE-2002-040.
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
 *  with ConMan; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifndef _SLURM_H
#define _SLURM_H

#include <src/common/slurm_protocol_defs.h>
#include <stdio.h>

/*
 * slurm_allocate - allocate nodes for a job with supplied contraints. 
 * NOTE: the calling function must free the allocated storage at node_list[0]
 */
extern int slurm_allocate_resources (job_desc_msg_t * job_desc_msg , resource_allocation_response_msg_t ** job_alloc_resp_msg, int immediate ) ;
extern int slurm_allocate_resources_and_run (job_desc_msg_t * job_desc_msg , 			resource_allocation_and_run_response_msg_t ** slurm_alloc_msg );

extern int slurm_cancel_job (uint32_t job_id);
extern int slurm_cancel_job_step (uint32_t job_id, uint32_t step_id);

extern int slurm_complete_job (uint32_t job_id);
extern int slurm_complete_job_step (uint32_t job_id, uint32_t step_id);


/***************************
 * slurm_ctl_conf.c
 ***************************/

/*
 * slurm_free_slurm_ctl_conf - free the build information buffer (if allocated)
 * NOTE: buffer is loaded by slurm_load_ctl_conf.
 */
extern void slurm_free_ctl_conf ( slurm_ctl_conf_t* slurm_ctl_conf_ptr);
/*
 * slurm_print_slurm_ctl_conf - prints the build information buffer (if allocated)
 * NOTE: buffer is loaded by slurm_load_slurm_ctl_conf.
 */
extern void slurm_print_ctl_conf ( FILE * out, slurm_ctl_conf_t* slurm_ctl_conf ) ;

/*
 * slurm_free_job_info - free the job information buffer (if allocated)
 * NOTE: buffer is loaded by slurm_load_job.
 */
extern void slurm_free_job_info_msg (job_info_msg_t * job_buffer_ptr);

/*
 * slurm_free_node_info - free the node information buffer (if allocated)
 * NOTE: buffer is loaded by slurm_load_node.
 */
extern void slurm_free_node_info_msg (node_info_msg_t * node_buffer_ptr);

/* 
 * slurm_print_job_info_msg - prints the job information buffer (if allocated)
 * NOTE: buffer is loaded by slurm_load_job_info .
 */
extern void slurm_print_job_info_msg ( FILE* , job_info_msg_t * job_info_msg_ptr ) ;

/* slurm_print_job_info - prints the job table object (if allocated) */
extern void slurm_print_job_info ( FILE*, job_info_t * job_ptr );

/* 
 * slurm_print_node_info_msg - prints the node information buffer (if allocated)
 * NOTE: buffer is loaded by slurm_load_node_info .
 */
extern void slurm_print_node_info_msg ( FILE*, node_info_msg_t * node_info_msg_ptr ) ;

/* slurm_print_node_table - prints the node table object (if allocated) */
extern void slurm_print_node_table ( FILE*, node_info_t * node_ptr );

/*
 * slurm_free_part_info - free the partition information buffer (if allocated)
 * NOTE: buffer is loaded by load_part.
 */
extern void slurm_free_partition_info_msg ( partition_info_msg_t * part_info_ptr);
extern void slurm_print_partition_info_msg ( FILE*, partition_info_msg_t * part_info_ptr ) ;
extern void slurm_print_partition_info ( FILE*, partition_info_t * part_ptr ) ;

/* slurm_load_ctl_conf - load the slurm configuration information if changed. */
extern int slurm_load_ctl_conf (time_t update_time, 
	slurm_ctl_conf_t  **slurm_ctl_conf_ptr);

/* slurm_load_job - load the supplied job information buffer if changed */
extern int slurm_load_jobs (time_t update_time, job_info_msg_t **job_info_msg_pptr);

/* slurm_load_node - load the supplied node information buffer if changed */
extern int slurm_load_node (time_t update_time, node_info_msg_t **node_info_msg_pptr);

/* slurm_load_node - load the supplied partition information buffer if changed */
extern int slurm_load_partitions (time_t update_time, partition_info_msg_t **part_buffer_ptr);

/* slurm_submit_job - load the supplied node information buffer if changed */
extern int slurm_submit_batch_job (job_desc_msg_t * job_desc_msg, 
		submit_response_msg_t ** slurm_alloc_msg );

extern int slurm_get_job_steps (time_t update_time, uint32_t job_id, uint32_t step_id, job_step_info_response_msg_t **step_response_pptr);
extern void  slurm_print_job_step_info_msg ( FILE* out, job_step_info_response_msg_t * job_step_info_msg_ptr );
extern void slurm_print_job_step_info ( FILE* out, job_step_info_t * job_step_ptr );

/* slurm_will_run - determine if a job would execute immediately if submitted. */
extern int slurm_job_will_run (job_desc_msg_t * job_desc_msg , resource_allocation_response_msg_t ** job_alloc_resp_msg );

/* slurm_reconfigure - request that slurmctld re-read the configuration files */
extern int slurm_reconfigure ();

/* slurm_shutdown - request that slurmctld terminate gracefully */
extern int slurm_shutdown ();

/* update a job, node, or partition's configuration, root access only */ 
extern int slurm_update_job ( job_desc_msg_t * job_msg ) ;
extern int slurm_update_node ( update_node_msg_t * node_msg ) ;
extern int slurm_update_partition ( update_part_msg_t * part_msg ) ;
#endif


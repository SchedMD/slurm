/*****************************************************************************\
 *  slurm.h - Definitions for all of the SLURM RPCs
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette1@llnl.gov>, 
 *	Joey Ekstrom <ekstrom1@llnl.gov> et. al.
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

#include <stdio.h>			/* for FILE definitions */

#include <src/common/slurm_protocol_defs.h>

/*****************************************************************************\
 *	DEFINITIONS FOR INPUT VALUES
\*****************************************************************************/

/* INFINITE is used to identify unlimited configurations,  */
/* eg. the maximum count of nodes any job may use in some partition */
#define	INFINITE (0xffffffff)
#define NO_VAL	 (0xfffffffe)

#define SLURM_JOB_DESC_DEFAULT_CONTIGUOUS	NO_VAL
#define SLURM_JOB_DESC_DEFAULT_KILL_NODE_FAIL	NO_VAL
#define SLURM_JOB_DESC_DEFAULT_ENVIRONMENT	((char **) NULL)
#define SLURM_JOB_DESC_DEFAULT_ENV_SIZE 	0
#define SLURM_JOB_DESC_DEFAULT_FEATURES		NULL
#define SLURM_JOB_DESC_DEFAULT_JOB_ID		NO_VAL
#define SLURM_JOB_DESC_DEFAULT_JOB_NAME 	NULL
#define SLURM_JOB_DESC_DEFAULT_MIN_PROCS	NO_VAL
#define SLURM_JOB_DESC_DEFAULT_MIN_MEMORY	NO_VAL
#define SLURM_JOB_DESC_DEFAULT_MIN_TMP_DISK	NO_VAL
#define SLURM_JOB_DESC_DEFAULT_PARTITION	NULL
#define SLURM_JOB_DESC_DEFAULT_PRIORITY		NO_VAL
#define SLURM_JOB_DESC_DEFAULT_REQ_NODES	NULL
#define SLURM_JOB_DESC_DEFAULT_JOB_SCRIPT	NULL
#define SLURM_JOB_DESC_DEFAULT_SHARED	 	NO_VAL
#define SLURM_JOB_DESC_DEFAULT_TIME_LIMIT	NO_VAL
#define SLURM_JOB_DESC_DEFAULT_NUM_PROCS	NO_VAL
#define SLURM_JOB_DESC_DEFAULT_NUM_NODES	NO_VAL
#define SLURM_JOB_DESC_DEFAULT_USER_ID		NO_VAL
#define SLURM_JOB_DESC_DEFAULT_WORKING_DIR	NULL

/*****************************************************************************\
 *	RESOURCE ALLOCATION FUNCTIONS
\*****************************************************************************/

/* slurm_init_job_desc_msg - set default job descriptor values */
extern void slurm_init_job_desc_msg (job_desc_msg_t * job_desc_msg);

/*
 * slurm_allocate_resources - allocate resources for a job request
 * NOTE: free the response using slurm_free_resource_allocation_response_msg
 */
extern int slurm_allocate_resources (job_desc_msg_t * job_desc_msg , 
		resource_allocation_response_msg_t ** job_alloc_resp_msg, 
		int immediate ) ;

/*
 * slurm_free_resource_allocation_response_msg - free slurm resource
 *	allocation response message
 * NOTE: buffer is loaded by either slurm_allocate_resources or 
 *	slurm_confirm_allocation
 */
extern void slurm_free_resource_allocation_response_msg (
		resource_allocation_response_msg_t * msg);

/*
 * slurm_allocate_resources_and_run - allocate resources for a job request and 
 *	initiate a job step
 * NOTE: free the response using 
 *	slurm_free_resource_allocation_and_run_response_msg
 */
extern int slurm_allocate_resources_and_run (job_desc_msg_t * job_desc_msg,
		resource_allocation_and_run_response_msg_t ** slurm_alloc_msg );

/*
 * slurm_free_resource_allocation_and_run_response_msg - free slurm 
 *	resource allocation and run job step response message
 * NOTE: buffer is loaded by slurm_allocate_resources_and_run
 */
extern void slurm_free_resource_allocation_and_run_response_msg ( 
		resource_allocation_and_run_response_msg_t * msg);

/*
 * slurm_confirm_allocation - confirm an existing resource allocation
 * NOTE: free the response using slurm_free_resource_allocation_response_msg
 */
extern int slurm_confirm_allocation (old_job_alloc_msg_t * job_desc_msg , 
		resource_allocation_response_msg_t ** slurm_alloc_msg ) ;

/*
 * slurm_job_step_create - create a job step for a given job id
 * NOTE: free the response using slurm_free_job_step_create_response_msg
 */
extern int slurm_job_step_create (
		job_step_create_request_msg_t * slurm_step_alloc_req_msg, 
		job_step_create_response_msg_t ** slurm_step_alloc_resp_msg );

/*
 * slurm_free_job_step_create_response_msg - free slurm 
 *	job step create response message
 * NOTE: buffer is loaded by slurm_job_step_create
 */
extern void slurm_free_job_step_create_response_msg ( 
		job_step_create_response_msg_t *msg);

/*
 * slurm_submit_batch_job - issue RPC to submit a job for later 
 *	execution
 * NOTE: free the response using 
 *	slurm_free_submit_response_response_msg
 */
extern int slurm_submit_batch_job (job_desc_msg_t * job_desc_msg, 
		submit_response_msg_t ** slurm_alloc_msg );

/*
 * slurm_free_submit_response_response_msg - free slurm 
 *	job submit response message
 * NOTE: buffer is loaded by slurm_submit_batch_job
 */
extern void slurm_free_submit_response_response_msg (
		submit_response_msg_t *msg);

/*
 * slurm_job_will_run - determine if a job would execute immediately if 
 *	submitted now
 * NOTE: free the response using slurm_free_resource_allocation_response_msg
 */
extern int slurm_job_will_run (job_desc_msg_t * job_desc_msg , 
		resource_allocation_response_msg_t ** job_alloc_resp_msg );


/*****************************************************************************\
 *	JOB/STEP CANCELATION FUNCTIONS
\*****************************************************************************/

/* slurm_cancel_job - cancel an existing job and all of its steps */
extern int slurm_cancel_job (uint32_t job_id);

/* slurm_cancel_job_step - cancel a specific job step */
extern int slurm_cancel_job_step (uint32_t job_id, uint32_t step_id);


/*****************************************************************************\
 *	JOB/STEP COMPLETION FUNCTIONS
\*****************************************************************************/

/* slurm_complete_job - note the completion of a job and all of its steps */
extern int slurm_complete_job (uint32_t job_id);

/* slurm_complete_job_step - note the completion of a specific job step */
extern int slurm_complete_job_step (uint32_t job_id, uint32_t step_id);


/*****************************************************************************\
 *	SLURM CONTROL CONFIGURATION READ/PRINT/UPDATE FUNCTIONS
\*****************************************************************************/

/* make_time_str - convert time_t to string with "month/date hour:min:sec" */
extern void make_time_str (time_t *time, char *string);

/*
 * slurm_load_ctl_conf - issue RPC to get slurm control configuration  
 *	information if changed since update_time 
 * NOTE: free the response using slurm_free_ctl_conf
 */
extern int slurm_load_ctl_conf (time_t update_time, 
		slurm_ctl_conf_t  **slurm_ctl_conf_ptr);

/*
 * slurm_free_ctl_conf - free slurm control information response message
 * NOTE: buffer is loaded by slurm_load_ctl_conf
 */
extern void slurm_free_ctl_conf (slurm_ctl_conf_t* slurm_ctl_conf_ptr);

/*
 * slurm_print_ctl_conf - output the contents of slurm control configuration
 *	message as loaded using slurm_load_ctl_conf
 */
extern void slurm_print_ctl_conf ( FILE * out, 
		slurm_ctl_conf_t* slurm_ctl_conf ) ;


/*****************************************************************************\
 *	SLURM JOB CONTROL CONFIGURATION READ/PRINT/UPDATE FUNCTIONS
\*****************************************************************************/

/*
 * slurm_load_jobs - issue RPC to get slurm all job configuration  
 *	information if changed since update_time 
 * NOTE: free the response using slurm_free_job_info_msg
 */
extern int slurm_load_jobs (time_t update_time, 
		job_info_msg_t **job_info_msg_pptr);

/*
 * slurm_free_job_info_msg - free the job information response message
 * NOTE: buffer is loaded by slurm_load_job.
 */
extern void slurm_free_job_info_msg (job_info_msg_t * job_buffer_ptr);

/*
 * slurm_print_job_info_msg - output information about all Slurm 
 *	jobs based upon message as loaded using slurm_load_jobs
 */
extern void slurm_print_job_info_msg ( FILE * out, 
		job_info_msg_t * job_info_msg_ptr ) ;

/*
 * slurm_print_job_info - output information about a specific Slurm 
 *	job based upon message as loaded using slurm_load_jobs
 */
extern void slurm_print_job_info ( FILE*, job_info_t * job_ptr );

/*
 * slurm_update_job - issue RPC to a job's configuration per request, 
 *	only usable by user root or (for some parameters) the job's owner
 */
extern int slurm_update_job ( job_desc_msg_t * job_msg ) ;


/*****************************************************************************\
 *	SLURM JOB STEP CONFIGURATION READ/PRINT/UPDATE FUNCTIONS
\*****************************************************************************/

/*
 * slurm_get_job_steps - issue RPC to get specific slurm job step   
 *	configuration information if changed since update_time.
 *	a job_id value of zero implies all jobs, a step_id value of 
 *	zero implies all steps
 * NOTE: free the response using slurm_free_job_step_info_response_msg
 */
extern int slurm_get_job_steps (time_t update_time, uint32_t job_id, 
		uint32_t step_id, 
		job_step_info_response_msg_t **step_response_pptr);

/*
 * slurm_free_job_step_info_response_msg - free the job step 
 *	information response message
 * NOTE: buffer is loaded by slurm_get_job_steps.
 */
extern void slurm_free_job_step_info_response_msg (
		job_step_info_response_msg_t * msg);

/*
 * slurm_print_job_step_info_msg - output information about all Slurm 
 *	job steps based upon message as loaded using slurm_get_job_steps
 */
extern void slurm_print_job_step_info_msg ( FILE * out, 
		job_step_info_response_msg_t * job_step_info_msg_ptr );

/*
 * slurm_print_job_step_info - output information about a specific Slurm 
 *	job step based upon message as loaded using slurm_get_job_steps
 */
extern void slurm_print_job_step_info ( FILE*, job_step_info_t * step_ptr );


/*****************************************************************************\
 *	SLURM NODE CONFIGURATION READ/PRINT/UPDATE FUNCTIONS
\*****************************************************************************/

/*
 * slurm_load_node - issue RPC to get slurm all node configuration  
 *	information if changed since update_time 
 * NOTE: free the response using slurm_free_node_info_msg
 */
extern int slurm_load_node (time_t update_time, 
		node_info_msg_t **node_info_msg_pptr);

/*
 * slurm_free_node_info_msg - free the node message information buffer
 * NOTE: buffer is loaded by slurm_load_node.
 */
extern void slurm_free_node_info_msg (node_info_msg_t * node_buffer_ptr);

/*
 * slurm_print_node_info_msg - output information about all Slurm nodes
 *	based upon message as loaded using slurm_load_node
 */
extern void slurm_print_node_info_msg ( FILE * out, 
		node_info_msg_t * node_info_msg_ptr ) ;

/*
 * slurm_print_node_table - output information about a specific Slurm node
 *	based upon message as loaded using slurm_load_node
 */
extern void slurm_print_node_table ( FILE * out, node_info_t * node_ptr );

/*
 * slurm_update_node - issue RPC to a nodes's configuration per request, 
 *	only usable by user root
 */
extern int slurm_update_node ( update_node_msg_t * node_msg ) ;


/*****************************************************************************\
 *	SLURM PARTITION CONFIGURATION READ/PRINT/UPDATE FUNCTIONS
\*****************************************************************************/

/* slurm_init_part_desc_msg - set default partition configuration parameters */
void slurm_init_part_desc_msg (update_part_msg_t * update_part_msg);

/*
 * slurm_load_partitions - issue RPC to get slurm all partition   
 *	configuration information if changed since update_time 
 * NOTE: free the response using slurm_free_partition_info_msg
 */
extern int slurm_load_partitions (time_t update_time, 
		partition_info_msg_t **part_buffer_ptr);

/*
 * slurm_free_partition_info_msg - free the partition information buffer
 * NOTE: buffer is loaded by slurm_load_partitions
 */
extern void slurm_free_partition_info_msg ( partition_info_msg_t * part_info_ptr);

/*
 * slurm_print_partition_info_msg - output information about all Slurm 
 *	partitions based upon message as loaded using slurm_load_partitions
 */
extern void slurm_print_partition_info_msg ( FILE * out, 
		partition_info_msg_t * part_info_ptr ) ;

/*
 * slurm_print_partition_info - output information about a specific Slurm 
 *	partition based upon message as loaded using slurm_load_partitions
 */
extern void slurm_print_partition_info ( FILE *out , 
		partition_info_t * part_ptr ) ;

/*
 * slurm_update_partition - issue RPC to a partition's configuration per  
 *	request, only usable by user root
 */
extern int slurm_update_partition ( update_part_msg_t * part_msg ) ;


/*****************************************************************************\
 *	SLURM RECONFIGURE/SHUTDOWN FUNCTIONS
\*****************************************************************************/

/*
 * slurm_reconfigure - issue RPC to have Slurm controller (slurmctld)
 * reload its configuration file 
 */
extern int slurm_reconfigure ( void );

/*
 * slurm_shutdown - issue RPC to have Slurm controller (slurmctld)
 *	cease operations, both the primary and backup controller 
 *	are shutdown.
 * core(I) - controller generates a core file if set
 */
extern int slurm_shutdown (uint16_t core);

#endif


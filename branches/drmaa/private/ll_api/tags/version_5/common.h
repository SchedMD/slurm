/*****************************************************************************\
 *  common.h - Common SLURM data structures and functions for the 
 *  LoadLeveler API.
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory.
 *  Written by Morris Jette <jette1@llnl.gov>
 * 
 *  This file is part of slurm_ll_api, a collection of LoadLeveler-compatable
 *  interfaces to Simple Linux Utility for Resource Managment (SLURM).  These 
 *  interfaces are used by POE (IBM's Parallel Operating Environment) to 
 *  initiated SLURM jobs. For details, see <http://www.llnl.gov/linux/slurm/>.
 *
 *  This notice is required to be provided under our contract with the U.S.
 *  Department of Energy (DOE).  This work was produced at the University
 *  of California, Lawrence Livermore National Laboratory under Contract
 *  No. W-7405-ENG-48 with the DOE.
 * 
 *  Neither the United States Government nor the University of California
 *  nor any of their employees, makes any warranty, express or implied, or
 *  assumes any liability or responsibility for the accuracy, completeness,
 *  or usefulness of any information, apparatus, product, or process
 *  disclosed, or represents that its use would not infringe
 *  privately-owned rights.
 *
 *  Also, reference herein to any specific commercial products, process, or
 *  services by trade name, trademark, manufacturer or otherwise does not
 *  necessarily constitute or imply its endorsement, recommendation, or
 *  favoring by the United States Government or the University of
 *  California.  The views and opinions of authors expressed herein do not
 *  necessarily state or reflect those of the United States Government or
 *  the University of California, and shall not be used for advertising or
 *  product endorsement purposes.
 * 
 *  The precise terms and conditions for copying, distribution and
 *  modification are specified in the file "COPYING".
\*****************************************************************************/
#ifndef _COMMON_H
#define _COMMON_H

#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <slurm/slurm.h>

#include "config.h"
#include "hostlist.h"
#include "llapi.h"

/*
 * SLURM specific versions of the LL_element structures 
 */
enum slurm_elem_types { ADAPTER_ELEM, CLUSTER_QUERY, CLUSTER_ELEM, 
			JOB_INIT, JOB_QUERY,
			NODE_ELEM, STEP_ELEM, SWITCH_ELEM, TASK_ELEM, 
			TASK_INST_ELEM };

typedef struct {
	enum slurm_elem_types	type;
	void *			data;
} slurm_elem_t;

/* For ADAPTER_ELEM */
typedef struct {
	slurm_elem_t *		taski_elem;
	char *			protocol;
	char *			mode;
	int32_t			window;
	char *			device;
	char *                  address;
	int32_t                 network_id;
	uint16_t                unique_id;
} slurm_adapter_elem_t;

/* For CLUSTER_ELEM */
typedef struct {
	char * tbd;
} slurm_cluster_data_t;

/* For CLUSTER_QUERY */
typedef struct {
	slurm_elem_t *		cluster_elem;
} slurm_cluster_query_t;

/* For JOB_INIT */
typedef struct {
	int			session_type;
	int			bulk_xfer;
	uint16_t		task_dist;
	job_desc_msg_t *	slurm_job_desc;
	resource_allocation_response_msg_t * job_alloc_resp;
	slurm_elem_t *		first_step_elem;
	enum job_states		job_state;
	char *			messages;
} slurm_job_init_t;

/* For JOB_QUERY */
typedef struct {
	char *			filter;
} slurm_job_query_t;

/* For NODE_ELEM */
typedef struct {
	char *			node_name;
	char *			node_addr;
	int			node_inx;
	int			task_cnt;
	uint32_t *		task_ids;
	slurm_elem_t *		step_elem;
	int			next_task_inx;
} slurm_node_elem_t;

/* For STEP_ELEM */
typedef struct step_elem {
	slurm_elem_t *		job_init_elem;
	node_info_msg_t *	node_info_msg;
	hostset_t 		host_set;
	hostset_t		host_set_copy;
	int			session_type;
	int			node_cnt;
	int *			node_inx_array;
	int *			fd_array;
	slurm_step_ctx		ctx;
	char *			step_id;
	uint32_t *		tasks_per_node;
} slurm_step_elem_t;

/* For SWITCH_ELEM */
typedef struct {
	int	job_key;
} slurm_switch_elem_t;

/* For MACHINE_ELEM */
typedef struct {
	int	job_key;
} slurm_machine_elem_t;

/* For TASK_ELEM */
typedef struct {
	slurm_elem_t *		node_elem;
	slurm_elem_t *		taski_elem;
	int			node_inx;
	int			task_inx;
	int 			task_id;
} slurm_task_elem_t;

/* For TASK_INST_ELEM */
typedef struct {
	slurm_elem_t *		task_elem;
	int			node_inx;
	int			task_id;
} slurm_taski_elem_t;

/*
 * Common function definitions
 */

/*
 * Build a slurm job step context for a given job and step element
 * RET -1 on error, 0 otherwise
 */
extern int build_step_ctx(slurm_elem_t *job_elem, slurm_elem_t *step_elem);

/* Given a LL_element, return a string indicating its type */
extern char *elem_name(enum slurm_elem_types type);

/* Convert LL state to a string */
extern char *ll_state_str(enum StepState state);

/* Convert QueryType object into equivalent string */
extern char *query_type_str(enum QueryType query_type);

/* Convert SLURM job states into equivalent LoadLeveler step states */
extern enum StepState remap_slurm_state(enum job_states slurm_job_state);

/* Logging functions */
#define ERROR(...)							\
	do {								\
		char *str = getenv("SLURM_LL_API_DEBUG");		\
		int num = 1;						\
		if (str)						\
			num = atoi(str);				\
		if (num > 2) {						\
			char fname[128];				\
			FILE *log;					\
			sprintf(fname, "/tmp/slurm.log.%d", getpid());	\
			log = fopen(fname, "a");			\
			fprintf(log, "ERROR: " __VA_ARGS__);		\
			fflush(log);					\
			fclose(log);					\
		} else if (num > 0) {					\
			fprintf(stderr, "SLURMERROR: " __VA_ARGS__);	\
			fflush(stderr);					\
		}							\
	} while(0)

#define VERBOSE(...)							\
	do {								\
		char *str = getenv("SLURM_LL_API_DEBUG");		\
		int num = 1;						\
		if (str)						\
			num = atoi(str);				\
		if (num > 2) {						\
			char fname[128];				\
			FILE *log;					\
			sprintf(fname, "/tmp/slurm.log.%d", getpid());	\
			log = fopen(fname, "a");			\
			fprintf(log, __VA_ARGS__);			\
			fflush(log);					\
			fclose(log);					\
		} else if (num > 1) {					\
			fprintf(stderr, __VA_ARGS__);			\
			fflush(stderr);					\
		}							\
	} while(0)

#endif  /* _COMMON_H */


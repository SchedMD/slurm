/*****************************************************************************\
 *  ll_no_op.c - Define LoadLeveler functions unused by POE. They are 
 *  still needed in order to open this shared object.
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

#include "common.h"
#include "config.h"
#include "llapi.h"

extern int llsubmit(char *a, char *b, char *c, LL_job *d, int e)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("llsubmit  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return 0;
}

extern void llfree_job_info(LL_job *a, int b)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("llfree_job_info  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
}

extern int GetHistory(char *a, int (*b)(LL_job *), int c)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("GetHistory  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return 0;
}

extern int ll_get_hostlist(struct JM_JOB_INFO *a)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_get_hostlist  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return 0;
}

extern int ll_start_host(char *a, char *b)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_start_host  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return 0;
}

extern int llinit(int a)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("llinit  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return 0;
}

extern int llinitiate(LL_job *a, int b)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("llinitiate  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return 0;
}

extern int llwait(LL_job **a, LL_job_step **b, int c)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("llwait  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return 0;
}

extern int ll_get_jobs(LL_get_jobs_info *a)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_get_jobs  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return 0;
}

extern int ll_free_jobs(LL_get_jobs_info *a)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_free_jobs  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return 0;
}

extern int ll_get_nodes(LL_get_nodes_info *a)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_get_nodes  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return 0;
}

extern int ll_free_nodes(LL_get_nodes_info *a)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_free_nodes  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return 0;
}

extern int ll_start_job(LL_start_job_info *a)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_start_job  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return 0;
}

extern int ll_terminate_job(LL_terminate_job_info *a)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_terminate_job  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return 0;
}

extern LL_element *llpd_allocate(void)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("llpd_allocate  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return (LL_element *)NULL;
}

extern int ll_update(LL_element *a, enum LL_Daemon b)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_update  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return 0;
}

extern int ll_reset_request(LL_element *a)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_reset_request  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return 0;
}

extern LL_element *ll_next_obj(LL_element *a)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_next_obj  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return (LL_element *)NULL;
}

extern int ll_control(int a, enum LL_control_op b, char **c, char **d, 
		char **e, char **f, int g)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_control  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return 0;
}

extern int ll_ckpt(LL_ckpt_info *a)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_ckpt  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return 0;
}

extern int ll_modify(int a, LL_element **b, LL_modify_param **c, char **d)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_modify  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return 0;
}

extern int ll_preempt(int a, LL_element **b, char *c, enum LL_preempt_op d)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_preempt  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return 0;
}
extern int ll_preempt_api(int a, void *b, char *c, enum LL_preempt_op d)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_preempt  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return 0;
}

extern void ckpt(void)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ckpt  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return;
}
extern int ckpt_api(void)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ckpt_api  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return 0;
}

extern int ll_init_ckpt(LL_ckpt_info *a)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_init_ckpt NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return 0;
}

extern int ll_set_ckpt_callbacks(callbacks_t *a)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_set_ckpt_callbacks  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return 0;
}

extern int ll_unset_ckpt_callbacks(int a)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_unset_ckpt_callbacks  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return 0;
}

extern char *ll_error(LL_element **a, int b)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_error  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return NULL;
}

extern int ll_free_objs(LL_element *a)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_free_objs  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return 0;
}

extern int ll_parse_file(LL_element *a, char *b, LL_element ** c,
		int d, char * e, LL_element **f)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_parse_file  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return 0;
}

extern int ll_parse_verify(LL_element *a, LL_element *b, LL_element **c)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_parse_verify  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return 0;
}

extern int ll_task_inst_pid_update(int *pid_list, int pid_cnt)
{
	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_task_inst_pid_update  NO-OP\n");
	VERBOSE("--------------------------------------------------\n");
	return 0;
}

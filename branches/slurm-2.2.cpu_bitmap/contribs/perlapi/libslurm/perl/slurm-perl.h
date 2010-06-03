/*
 * slurm-perl.h - prototypes of msg-hv converting functions
 */

#ifndef _SLURMDB_PERL_H
#define _SLURMDB_PERL_H

#include <msg.h>


extern int hv_to_job_desc_msg(HV* hv, job_desc_msg_t* job_desc_msg);
extern void free_job_desc_msg_memory(job_desc_msg_t *msg);
extern int resource_allocation_response_msg_to_hv(
    resource_allocation_response_msg_t* resp_msg, HV* hv);
extern int job_alloc_info_response_msg_to_hv(job_alloc_info_response_msg_t
					     *resp_msg, HV* hv);
extern int submit_response_msg_to_hv(submit_response_msg_t *resp_msg, HV* hv);

extern int job_info_msg_to_hv(job_info_msg_t* job_info_msg, HV* hv);
extern int job_step_info_response_msg_to_hv(job_step_info_response_msg_t*
					    job_step_info_msg, HV* hv);
extern int slurm_step_layout_to_hv(slurm_step_layout_t* step_layout, HV* hv);

extern int node_info_msg_to_hv(node_info_msg_t* node_info_msg, HV* hv);
extern int hv_to_update_node_msg(HV* hv, update_node_msg_t *update_msg);

extern int partition_info_msg_to_hv(partition_info_msg_t* part_info_msg,
				    HV* hv);
extern int hv_to_update_part_msg(HV* hv, update_part_msg_t* part_msg);

extern int slurm_ctl_conf_to_hv(slurm_ctl_conf_t* conf, HV* hv);

extern int trigger_info_to_hv(trigger_info_t *info, HV* hv);
extern int trigger_info_msg_to_hv(trigger_info_msg_t *msg, HV* hv);
extern int hv_to_trigger_info(HV* hv, trigger_info_t* info);

extern int hv_to_slurm_step_ctx_params(HV* hv, slurm_step_ctx_params_t* params);
extern int hv_to_slurm_step_launch_params(HV* hv, slurm_step_launch_params_t
					  *params);
extern void free_slurm_step_launch_params_memory(slurm_step_launch_params_t
						 *params);

#endif /* _SLURMDB_PERL_H */

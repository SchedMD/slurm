/*
 * node.c - convert data between node related messages and perl HVs
 */

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>

#include <slurm/slurm.h>
#include "msg.h"

/*
 * convert perl HV to slurm_step_ctx_params_t
 */
int
hv_to_slurm_step_ctx_params(HV* hv, slurm_step_ctx_params_t* params)
{
	slurm_step_ctx_params_t_init(params);

	FETCH_FIELD(hv, params, job_id, uint32_t, TRUE);
	FETCH_FIELD(hv, params, uid, uint32_t, FALSE);
	FETCH_FIELD(hv, params, name, charp, FALSE);
	FETCH_FIELD(hv, params, node_count, uint32_t, FALSE);
	FETCH_FIELD(hv, params, cpu_count, uint32_t, FALSE);
	FETCH_FIELD(hv, params, task_count, uint32_t, FALSE);
	FETCH_FIELD(hv, params, relative, uint16_t, FALSE);
	FETCH_FIELD(hv, params, task_dist, uint16_t, FALSE);
	FETCH_FIELD(hv, params, plane_size, uint16_t, FALSE);
	FETCH_FIELD(hv, params, node_list, charp, FALSE);
	FETCH_FIELD(hv, params, network, charp, FALSE);
	FETCH_FIELD(hv, params, overcommit, bool, FALSE);
	FETCH_FIELD(hv, params, mem_per_task, uint16_t, FALSE);
	return 0;
}

/*
 * convert perl HV to slurm_step_launch_params_t
 */
int
hv_to_slurm_step_launch_params(HV* hv, slurm_step_launch_params_t* params)
{
	int i, num_keys;
	STRLEN vlen;
	I32 klen;
	SV** svp;
	HV* environ_hv;
	AV* argv_av;
	SV* val;
	char *env_key, *env_val;

	slurm_step_launch_params_t_init(params);

	if((svp = hv_fetch(hv, "argv", 4, FALSE))) {
		if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
			argv_av = (AV*)SvRV(*svp);
			params->argc = av_len(argv_av) + 1;
			if (params->argc > 0) {
				Newz(0, params->argv, (int32_t)(params->argc + 1), char*);
				for(i = 0; i < params->argc; i ++) {
					if((svp = av_fetch(argv_av, i, FALSE)))
						*(params->argv + i) = (char*) SvPV_nolen(*svp);
					else {
						Perl_warn(aTHX_ "error fetching `argv' of job descriptor");
						free_slurm_step_launch_params_memory(params);
						return -1;
					}
				}
			}
		} else {
			Perl_warn(aTHX_ "`argv' of job descriptor is not an array reference");
			return -1;
		}
	} else {
		Perl_warn(aTHX_ "`argv' missing in step launching params");
		return -1;
	}
	if((svp = hv_fetch(hv, "env", 3, FALSE))) {
		if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVHV) {
			environ_hv = (HV*)SvRV(*svp);
			num_keys = HvKEYS(environ_hv);
			params->envc = num_keys;
			Newz(0, params->env, num_keys + 1, char*);
			
			hv_iterinit(environ_hv);
			i = 0;
			while((val = hv_iternextsv(environ_hv, &env_key, &klen))) {
				env_val = SvPV(val, vlen);
				Newz(0, (*(params->env + i)), klen + vlen + 2, char);
				sprintf((*params->env + i), "%s=%s", env_key, env_val);
				i ++;
			}
		}
		else {
			Perl_warn(aTHX_ "`env' of job descriptor is not a hash reference, ignored");
		}
	}
	FETCH_FIELD(hv, params, cwd, charp, FALSE);
	FETCH_FIELD(hv, params, user_managed_io, bool, FALSE);
	FETCH_FIELD(hv, params, msg_timeout, uint32_t, FALSE);
	FETCH_FIELD(hv, params, buffered_stdio, bool, FALSE);
	FETCH_FIELD(hv, params, labelio, bool, FALSE);
	FETCH_FIELD(hv, params, remote_output_filename, charp, FALSE);
	FETCH_FIELD(hv, params, remote_error_filename, charp, FALSE);
	FETCH_FIELD(hv, params, remote_input_filename, charp, FALSE);
	/* TODO: local_fds */
	FETCH_FIELD(hv, params, gid, uint32_t, FALSE);
	FETCH_FIELD(hv, params, multi_prog, bool, FALSE);
	FETCH_FIELD(hv, params, slurmd_debug, uint32_t, FALSE);
	FETCH_FIELD(hv, params, parallel_debug, bool, FALSE);
	FETCH_FIELD(hv, params, task_prolog, charp, FALSE);
	FETCH_FIELD(hv, params, task_epilog, charp, FALSE);
	FETCH_FIELD(hv, params, cpu_bind_type, uint16_t, FALSE);
	FETCH_FIELD(hv, params, cpu_bind, charp, FALSE);
	FETCH_FIELD(hv, params, mem_bind_type, uint16_t, FALSE);
	FETCH_FIELD(hv, params, mem_bind, charp, FALSE);

	FETCH_FIELD(hv, params, max_sockets, uint16_t, FALSE);
	FETCH_FIELD(hv, params, max_cores, uint16_t, FALSE);
	FETCH_FIELD(hv, params, max_threads, uint16_t, FALSE);
	FETCH_FIELD(hv, params, cpus_per_task, uint16_t, FALSE);
	FETCH_FIELD(hv, params, task_dist, uint16_t, FALSE);
	FETCH_FIELD(hv, params, preserve_env, bool, FALSE);

	FETCH_FIELD(hv, params, mpi_plugin_name, charp, FALSE);
	FETCH_FIELD(hv, params, open_mode, uint8_t, FALSE);
	FETCH_FIELD(hv, params, acctg_freq, uint16_t, FALSE);
	FETCH_FIELD(hv, params, pty, bool, FALSE);
	FETCH_FIELD(hv, params, ckpt_path, charp, FALSE);
	
	return 0;
}

/*
 * free allocated environment variable memory for job_desc_msg_t 
 */
static void
_free_env(char** environ)
{
	int i;
	if(! environ)
		return;
	for(i = 0; *(environ + i) ; i ++)
		Safefree(*(environ + i));
	Safefree(environ);
}

/*
 * free allocate memory for job_desc_msg_t
 */
void
free_slurm_step_launch_params_memory(slurm_step_launch_params_t *params)
{
	_free_env(params->env);
	if (params->argv)
		Safefree (params->argv);
}


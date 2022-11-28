/*****************************************************************************\
 *  opt.c
 *****************************************************************************
 *  Copyright (C) 2020 SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
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

#include "src/common/proc_args.h"
#include "src/common/xstring.h"

#include "scrontab.h"

extern void fill_job_desc_from_opts(job_desc_msg_t *desc)
{
	desc->contiguous = opt.contiguous ? 1 : 0;
	if (opt.core_spec != NO_VAL16)
		desc->core_spec = opt.core_spec;
	desc->features = xstrdup(opt.constraint);
	desc->name = xstrdup(opt.job_name);
	desc->reservation = xstrdup(opt.reservation);
	desc->wckey = xstrdup(opt.wckey);

	desc->req_nodes = xstrdup(opt.nodelist);
	desc->extra = xstrdup(opt.extra);
	desc->exc_nodes = xstrdup(opt.exclude);
	desc->partition = xstrdup(opt.partition);
	desc->profile = opt.profile;
	desc->licenses = xstrdup(opt.licenses);

	if (opt.nodes_set) {
		desc->min_nodes = opt.min_nodes;
		if (opt.max_nodes) {
			desc->max_nodes = opt.max_nodes;
			if (opt.job_size_str)
				desc->job_size_str = xstrdup(opt.job_size_str);
			else
				desc->job_size_str = NULL;
		}
	} else if (opt.ntasks_set && (opt.ntasks == 0)) {
		desc->min_nodes = 0;
		desc->job_size_str = NULL;
	}
	if (opt.ntasks_per_node)
		desc->ntasks_per_node = opt.ntasks_per_node;
	desc->user_id = opt.uid;
	desc->group_id = opt.gid;
	desc->dependency = xstrdup(opt.dependency);

	desc->mem_bind = xstrdup(opt.mem_bind);
	if (opt.mem_bind_type)
		desc->mem_bind_type = opt.mem_bind_type;
	if (opt.plane_size != NO_VAL)
		desc->plane_size = opt.plane_size;
	desc->task_dist = opt.distribution;

	desc->network = xstrdup(opt.network);
	if (opt.nice != NO_VAL)
		desc->nice = NICE_OFFSET + opt.nice;
	if (opt.priority)
		desc->priority = opt.priority;

	desc->mail_type = opt.mail_type;
	desc->mail_user = xstrdup(opt.mail_user);
	if (opt.begin)
		desc->begin_time = opt.begin;
	if (opt.deadline)
		desc->deadline = opt.deadline;
	if (opt.delay_boot != NO_VAL)
		desc->delay_boot = opt.delay_boot;
	desc->account = xstrdup(opt.account);
	desc->comment = xstrdup(opt.comment);
	desc->qos = xstrdup(opt.qos);

	/* job constraints */
	if (opt.pn_min_cpus > -1)
		desc->pn_min_cpus = opt.pn_min_cpus;
	if (opt.pn_min_memory != NO_VAL64)
		desc->pn_min_memory = opt.pn_min_memory;
	else if (opt.mem_per_cpu != NO_VAL64)
		desc->pn_min_memory = opt.mem_per_cpu | MEM_PER_CPU;
	if (opt.pn_min_tmp_disk != NO_VAL64)
		desc->pn_min_tmp_disk = opt.pn_min_tmp_disk;
	if (opt.overcommit) {
		desc->min_cpus = MAX(opt.min_nodes, 1);
		desc->overcommit = opt.overcommit;
	} else if (opt.cpus_set)
		desc->min_cpus = opt.ntasks * opt.cpus_per_task;
	else if (opt.nodes_set && (opt.min_nodes == 0))
		desc->min_cpus = 0;
	else
		desc->min_cpus = opt.ntasks;

	if (opt.ntasks_set)
		desc->num_tasks = opt.ntasks;
	if (opt.cpus_set)
		desc->cpus_per_task = opt.cpus_per_task;
	if (opt.ntasks_per_socket > -1)
		desc->ntasks_per_socket = opt.ntasks_per_socket;
	if (opt.ntasks_per_core > -1)
		desc->ntasks_per_core = opt.ntasks_per_core;

	/* node constraints */
	if (opt.sockets_per_node != NO_VAL)
		desc->sockets_per_node = opt.sockets_per_node;
	if (opt.cores_per_socket != NO_VAL)
		desc->cores_per_socket = opt.cores_per_socket;
	if (opt.threads_per_core != NO_VAL)
		desc->threads_per_core = opt.threads_per_core;

	if (opt.no_kill)
		desc->kill_on_node_fail = 0;
	if (opt.time_limit != NO_VAL)
		desc->time_limit = opt.time_limit;
	if (opt.time_min != NO_VAL)
		desc->time_min = opt.time_min;
	if (opt.shared != NO_VAL16)
		desc->shared = opt.shared;

	if (opt.warn_flags)
		desc->warn_flags = opt.warn_flags;
	if (opt.warn_signal)
		desc->warn_signal = opt.warn_signal;
	if (opt.warn_time)
		desc->warn_time = opt.warn_time;

	desc->open_mode = opt.open_mode;
	desc->std_err = xstrdup(opt.efname);
	desc->std_in = xstrdup(opt.ifname);
	desc->std_out = xstrdup(opt.ofname);
	desc->work_dir = xstrdup(opt.chdir);

	desc->acctg_freq = xstrdup(opt.acctg_freq);

	desc->cpu_freq_min = opt.cpu_freq_min;
	desc->cpu_freq_max = opt.cpu_freq_max;
	desc->cpu_freq_gov = opt.cpu_freq_gov;

	if (opt.req_switch >= 0)
		desc->req_switch = opt.req_switch;
	if (opt.wait4switch >= 0)
		desc->wait4switch = opt.wait4switch;

	desc->power_flags = opt.power;
	if (opt.job_flags)
		desc->bitflags = opt.job_flags;
	desc->mcs_label = xstrdup(opt.mcs_label);

	if (opt.cpus_per_gpu)
		xstrfmtcat(desc->cpus_per_tres, "gres:gpu:%d", opt.cpus_per_gpu);
	desc->tres_bind = xstrdup(opt.tres_bind);
	desc->tres_freq = xstrdup(opt.tres_freq);
	xfmt_tres(&desc->tres_per_job, "gres:gpu", opt.gpus);
	xfmt_tres(&desc->tres_per_node, "gres:gpu", opt.gpus_per_node);
	/* --gres=none for jobs means no GRES, so don't send it to slurmctld */
	if (opt.gres && xstrcasecmp(opt.gres, "NONE")) {
		if (desc->tres_per_node)
			xstrfmtcat(desc->tres_per_node, ",%s", opt.gres);
		else
			desc->tres_per_node = xstrdup(opt.gres);
	}
	xfmt_tres(&desc->tres_per_socket, "gres:gpu", opt.gpus_per_socket);
	xfmt_tres(&desc->tres_per_task, "gres:gpu", opt.gpus_per_task);
	if (opt.mem_per_gpu != NO_VAL64)
		xstrfmtcat(desc->mem_per_tres, "gres:gpu:%"PRIu64, opt.mem_per_gpu);
}

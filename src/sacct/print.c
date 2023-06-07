/*****************************************************************************\
 *  print.c - print functions for sacct
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
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

#include "sacct.h"
#include "src/common/cpu_frequency.h"
#include "src/common/parse_time.h"
#include "slurm/slurm.h"

print_field_t *field = NULL;
int curr_inx = 1;
char outbuf[FORMAT_STRING_SIZE];

#define SACCT_TRES_AVE  0x0001
#define SACCT_TRES_OUT  0x0002
#define SACCT_TRES_MIN  0x0004
#define SACCT_TRES_TOT  0x0008

static char *_elapsed_time(uint64_t secs, uint64_t usecs)
{
	uint64_t days, hours, minutes, seconds, subsec = 0;
	char *str = NULL;

	if (secs == NO_VAL64)
		return NULL;

	if (usecs >= 1E6) {
		secs += usecs / 1E6;
		usecs = usecs % (int)1E6;
	}
	if (usecs > 0) {
		/* give me 3 significant digits to tack onto the sec */
		subsec = (usecs/1000);
	}
	seconds =  secs % 60;
	minutes = (secs / 60)   % 60;
	hours   = (secs / 3600) % 24;
	days    =  secs / 86400;

	if (days)
		str = xstrdup_printf("%"PRIu64"-%2.2"PRIu64":%2.2"PRIu64":%2.2"PRIu64"",
				     days, hours, minutes, seconds);
	else if (hours)
		str = xstrdup_printf("%2.2"PRIu64":%2.2"PRIu64":%2.2"PRIu64"",
				     hours, minutes, seconds);
	else if (subsec)
		str = xstrdup_printf("%2.2"PRIu64":%2.2"PRIu64".%3.3"PRIu64"",
				     minutes, seconds, subsec);
	else
		str = xstrdup_printf("00:%2.2"PRIu64":%2.2"PRIu64"",
				     minutes, seconds);
	return str;
}

static char *_find_qos_name_from_list(List qos_list, int qosid)
{
	slurmdb_qos_rec_t *qos;

	if (!qos_list || qosid == NO_VAL)
		return NULL;

	qos = list_find_first(qos_list, slurmdb_find_qos_in_list, &qosid);

	if (qos)
		return qos->name;
	else
		return "Unknown";
}

static void _print_small_double(
	char *outbuf, int buf_size, double dub, int units)
{
	if (fuzzy_equal(dub, NO_VAL))
		return;

	if (dub > 1)
		convert_num_unit((double)dub, outbuf, buf_size, units,
				 params.units, params.convert_flags);
	else if (dub > 0)
		snprintf(outbuf, buf_size, "%.2fM", dub);
	else
		snprintf(outbuf, buf_size, "0");
}

static char *_get_tres_node(int type, void *object, int tres_pos,
			    uint16_t flags)
{
	slurmdb_stats_t *stats = NULL;
	char *tmp_char = NULL;
	char *nodes = NULL;
	slurmdb_step_rec_t *step = object;

	if (type != JOBSTEP)
		return NULL;

	stats = &step->stats;
	nodes = step->nodes;

	if (!stats)
		return NULL;

	if (flags & SACCT_TRES_OUT)
		tmp_char = (flags & SACCT_TRES_MIN) ?
			stats->tres_usage_out_min_nodeid :
			stats->tres_usage_out_max_nodeid;
	else
		tmp_char = (flags & SACCT_TRES_MIN) ?
			stats->tres_usage_in_min_nodeid :
			stats->tres_usage_in_max_nodeid;

	return find_hostname(
		slurmdb_find_tres_count_in_string(tmp_char, tres_pos),
		nodes);
}

static uint32_t _get_tres_task(int type, void *object, int tres_pos,
			       uint16_t flags)
{
	slurmdb_stats_t *stats = NULL;
	uint32_t tmp_uint32 = NO_VAL;
	uint64_t tmp_uint64 = NO_VAL64;
	char *tmp_char = NULL;
	slurmdb_step_rec_t *step = object;

	if (type != JOBSTEP)
		return tmp_uint32;

	stats = &step->stats;

	if (!stats)
		return tmp_uint32;

	if (flags & SACCT_TRES_OUT)
		tmp_char = (flags & SACCT_TRES_MIN) ?
			stats->tres_usage_out_min_taskid :
			stats->tres_usage_out_max_taskid;
	else
		tmp_char = (flags & SACCT_TRES_MIN) ?
			stats->tres_usage_in_min_taskid :
			stats->tres_usage_in_max_taskid;

	tmp_uint64 = slurmdb_find_tres_count_in_string(tmp_char, tres_pos);

	if (tmp_uint64 == INFINITE64)
		tmp_uint64 = NO_VAL64;

	if (tmp_uint64 != NO_VAL64)
		tmp_uint32 = tmp_uint64;

	return tmp_uint32;
}

static uint64_t _get_tres_cnt(int type, void *object, int tres_pos,
			      uint16_t flags)
{
	slurmdb_stats_t *stats = NULL;
	uint64_t tmp_uint64 = NO_VAL64;
	char *tmp_char = NULL;
	slurmdb_step_rec_t *step = object;

	if (type != JOBSTEP)
		return NO_VAL64;

	stats = &step->stats;

	if (!stats)
		return tmp_uint64;

	if (flags & SACCT_TRES_OUT) {
		if (flags & SACCT_TRES_AVE)
			tmp_char = stats->tres_usage_out_ave;
		else if (flags & SACCT_TRES_TOT)
			tmp_char = stats->tres_usage_out_tot;
		else
			tmp_char = (flags & SACCT_TRES_MIN) ?
				stats->tres_usage_out_min :
				stats->tres_usage_out_max;
	} else {
		if (flags & SACCT_TRES_AVE)
			tmp_char = stats->tres_usage_in_ave;
		else if (flags & SACCT_TRES_TOT)
			tmp_char = stats->tres_usage_in_tot;
		else
			tmp_char = (flags & SACCT_TRES_MIN) ?
				stats->tres_usage_in_min :
				stats->tres_usage_in_max;
	}

	tmp_uint64 = slurmdb_find_tres_count_in_string(tmp_char, tres_pos);

	if (tmp_uint64 == INFINITE64)
		tmp_uint64 = NO_VAL64;

	return tmp_uint64;
}

static void _print_tres_field(char *tres_in, char *nodes, bool convert,
			      uint32_t tres_flags)
{
	char *temp = NULL;

	if (!g_tres_list) {
		slurmdb_tres_cond_t tres_cond;
		memset(&tres_cond, 0, sizeof(slurmdb_tres_cond_t));
		tres_cond.with_deleted = 1;
		g_tres_list = slurmdb_tres_get(acct_db_conn, &tres_cond);
	}

	temp = slurmdb_make_tres_string_from_simple(tres_in, g_tres_list,
						    convert ?
						    params.units : NO_VAL,
						    convert ?
						    params.convert_flags :
						    CONVERT_NUM_UNIT_RAW,
						    tres_flags,
						    nodes);

	field->print_routine(field, temp, (curr_inx == field_count));
	xfree(temp);
	return;
}

static void _print_expanded_array_job(slurmdb_job_rec_t *job)
{
	int i_first, i_last;
	bitstr_t *bitmap;

	bitmap = bit_alloc(slurm_conf.max_array_sz);
	(void) bit_unfmt_hexmask(bitmap, job->array_task_str);
	xfree(job->array_task_str);

	i_first = bit_ffs(bitmap);
	if (i_first == -1)
		i_last = -2;
	else
		i_last = bit_fls(bitmap);
	for (int i = i_first; i <= i_last; i++) {
		if (!bit_test(bitmap, i))
			continue;
		job->array_task_id = i;
		print_fields(JOB, job);
	}
	FREE_NULL_BITMAP(bitmap);
}

extern void print_fields(type_t type, void *object)
{
	slurmdb_job_rec_t *job = (slurmdb_job_rec_t *)object;
	slurmdb_step_rec_t *step = (slurmdb_step_rec_t *)object;
	jobcomp_job_rec_t *job_comp = (jobcomp_job_rec_t *)object;
	struct passwd *pw = NULL;
	struct	group *gr = NULL;
	int cpu_tres_rec_count = 0;
	int step_cpu_tres_rec_count = 0;
	char tmp1[128];
	char *nodes = NULL;

	if (!object) {
		fatal("Job or step record is NULL");
		return;
	}

	if (params.opt_array && job->array_task_str && (type == JOB)) {
		_print_expanded_array_job(job);
		return;
	}

	switch (type) {
	case JOB:
		job_comp = NULL;
		cpu_tres_rec_count = slurmdb_find_tres_count_in_string(
			(job->tres_alloc_str && job->tres_alloc_str[0]) ?
			job->tres_alloc_str : job->tres_req_str,
			TRES_CPU);
		break;
	case JOBSTEP:
		job = step->job_ptr;

		if ((step_cpu_tres_rec_count =
		     slurmdb_find_tres_count_in_string(
			     step->tres_alloc_str, TRES_CPU)) == INFINITE64)
			step_cpu_tres_rec_count =
				slurmdb_find_tres_count_in_string(
					(job->tres_alloc_str &&
					 job->tres_alloc_str[0]) ?
					job->tres_alloc_str : job->tres_req_str,
					TRES_CPU);

		job_comp = NULL;
		break;
	case JOBCOMP:
		job = NULL;
		step = NULL;
		break;
	default:
		break;
	}

	if ((uint64_t)cpu_tres_rec_count == INFINITE64)
		cpu_tres_rec_count = 0;

	if ((uint64_t)step_cpu_tres_rec_count == INFINITE64)
		step_cpu_tres_rec_count = 0;

	curr_inx = 1;
	list_iterator_reset(print_fields_itr);
	while ((field = list_next(print_fields_itr))) {
		char *tmp_char = NULL, *id = NULL;
		int exit_code, tmp_int = NO_VAL, tmp_int2 = NO_VAL;
		time_t tmp_time = 0;
		double tmp_dub = (double)NO_VAL; /* don't use NO_VAL64
						    unless we can
						    confirm the values
						    coming in are
						    NO_VAL64 */
		uint32_t tmp_uint32 = NO_VAL, tmp2_uint32 = NO_VAL;
		uint64_t tmp_uint64 = NO_VAL64, tmp2_uint64 = NO_VAL64;

		memset(&outbuf, 0, sizeof(outbuf));
		switch (field->type) {
		case PRINT_ALLOC_CPUS:
			switch(type) {
			case JOB:
				tmp_int = cpu_tres_rec_count;
				break;
			case JOBSTEP:
				tmp_int = step_cpu_tres_rec_count;
				break;
			case JOBCOMP:
				tmp_int = job_comp->proc_cnt;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     &tmp_int,
					     (curr_inx == field_count));
			break;
		case PRINT_ALLOC_NODES:
			switch(type) {
			case JOB:
				tmp_uint32 = job->alloc_nodes;
				tmp_char = job->tres_alloc_str;
				break;
			case JOBSTEP:
				tmp_uint32 = step->nnodes;
				tmp_char = step->tres_alloc_str;
				break;
			case JOBCOMP:
				tmp_int = job_comp->node_cnt;
				break;
			default:
				break;
			}

			if (!tmp_uint32 && tmp_char) {
				if ((tmp_uint64 =
				     slurmdb_find_tres_count_in_string(
					     tmp_char, TRES_NODE))
				    != INFINITE64)
					tmp_uint32 = tmp_uint64;
			}
			convert_num_unit((double)tmp_uint32, outbuf,
					 sizeof(outbuf), UNIT_NONE, NO_VAL,
					 params.convert_flags);
			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_ACCOUNT:
			switch(type) {
			case JOB:
				tmp_char = job->account;
				break;
			case JOBSTEP:
				tmp_char = step->job_ptr->account;
				break;
			case JOBCOMP:
				tmp_char = job_comp->account;
				break;
			default:
				tmp_char = "n/a";
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_ACT_CPUFREQ:
			switch (type) {
			case JOBSTEP:
				tmp_dub = step->stats.act_cpufreq;
				break;
			default:
				break;
			}

			if (!fuzzy_equal(tmp_dub, NO_VAL))
				convert_num_unit2((double)tmp_dub, outbuf,
						  sizeof(outbuf), UNIT_KILO,
						  params.units, 1000,
						  params.convert_flags &
						  (~CONVERT_NUM_UNIT_EXACT));

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_ADMIN_COMMENT:
			switch(type) {
			case JOB:
				tmp_char = job->admin_comment;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_ASSOCID:
			switch(type) {
			case JOB:
				tmp_uint32 = job->associd;
				break;
			case JOBSTEP:
				tmp_uint32 = step->job_ptr->associd;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     &tmp_uint32,
					     (curr_inx == field_count));
			break;
		case PRINT_AVECPU:
			tmp_uint64 = _get_tres_cnt(
				type, object, TRES_CPU, SACCT_TRES_AVE);
			if (tmp_uint64 != NO_VAL64) {
				tmp_uint64 /= CPU_TIME_ADJ;
				tmp_char = _elapsed_time(tmp_uint64, 0);
			}

			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_AVEDISKREAD:
			tmp_uint64 = _get_tres_cnt(
				type, object, TRES_FS_DISK,
				SACCT_TRES_AVE);

			if (tmp_uint64 != NO_VAL64)
				tmp_dub = (double)tmp_uint64 / (1 << 20);

			_print_small_double(outbuf, sizeof(outbuf),
					    tmp_dub, UNIT_MEGA);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_AVEDISKWRITE:
			tmp_uint64 = _get_tres_cnt(
				type, object, TRES_FS_DISK,
				SACCT_TRES_OUT | SACCT_TRES_AVE);

			if (tmp_uint64 != NO_VAL64)
				tmp_dub = (double)tmp_uint64 / (1 << 20);

			_print_small_double(outbuf, sizeof(outbuf),
					    tmp_dub, UNIT_MEGA);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_AVEPAGES:
			tmp_uint64 = _get_tres_cnt(type, object, TRES_PAGES,
						   SACCT_TRES_AVE);

			if (tmp_uint64 != NO_VAL64)
				convert_num_unit((double)tmp_uint64, outbuf,
						 sizeof(outbuf), UNIT_NONE,
						 params.units,
						 params.convert_flags);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_AVERSS:
			tmp_uint64 = _get_tres_cnt(type, object, TRES_MEM,
						   SACCT_TRES_AVE);

			if (tmp_uint64 != NO_VAL64)
				convert_num_unit((double)tmp_uint64, outbuf,
						 sizeof(outbuf), UNIT_NONE,
						 params.units,
						 params.convert_flags);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_AVEVSIZE:
			tmp_uint64 = _get_tres_cnt(type, object, TRES_VMEM,
						   SACCT_TRES_AVE);

			if (tmp_uint64 != NO_VAL64)
				convert_num_unit((double)tmp_uint64, outbuf,
						 sizeof(outbuf), UNIT_NONE,
						 params.units,
						 params.convert_flags);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_BLOCKID:
			switch(type) {
			case JOB:
				tmp_char = job->blockid;
				break;
			case JOBCOMP:
				tmp_char = job_comp->blockid;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_CLUSTER:
			switch(type) {
			case JOB:
				tmp_char = job->cluster;
				break;
			case JOBSTEP:
				tmp_char = step->job_ptr->cluster;
				break;
			case JOBCOMP:
				tmp_char = job_comp->cluster;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_COMMENT:
			switch(type) {
			case JOB:
				tmp_char = job->derived_es;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_CONSTRAINTS:
			switch(type) {
			case JOB:
				tmp_char = job->constraints;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_CONTAINER:
			switch(type) {
			case JOB:
				tmp_char = job->container;
				break;
			case JOBSTEP:
				tmp_char = step->container;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_CONSUMED_ENERGY:
			switch (type) {
			case JOB:
				if ((tmp_uint64 =
				     slurmdb_find_tres_count_in_string(
					     job->tres_alloc_str,
					     TRES_ENERGY))
				    == INFINITE64)
					tmp_uint64 = 0;
				break;
			case JOBSTEP:
				tmp_uint64 = step->stats.consumed_energy;
				break;
			default:
				break;
			}

			if (!fuzzy_equal(tmp_uint64, NO_VAL64))
				convert_num_unit2((double)tmp_uint64, outbuf,
						  sizeof(outbuf), UNIT_NONE,
						  params.units, 1000,
						  params.convert_flags &
						  (~CONVERT_NUM_UNIT_EXACT));

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_CONSUMED_ENERGY_RAW:
			switch (type) {
			case JOB:
				if ((tmp_uint64 =
				     slurmdb_find_tres_count_in_string(
					     job->tres_alloc_str,
					     TRES_ENERGY))
				    == INFINITE64)
					tmp_uint64 = 0;
				break;
			case JOBSTEP:
				tmp_uint64 = step->stats.consumed_energy;
				break;
			default:
				break;
			}

			field->print_routine(field,
					     &tmp_uint64,
					     (curr_inx == field_count));
			break;
		case PRINT_CPU_TIME:
			switch(type) {
			case JOB:
				tmp_uint64 = (uint64_t)job->elapsed
					* (uint64_t)cpu_tres_rec_count;
				break;
			case JOBSTEP:
				tmp_uint64 = (uint64_t)step->elapsed
					* (uint64_t)step_cpu_tres_rec_count;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     &tmp_uint64,
					     (curr_inx == field_count));
			break;
		case PRINT_CPU_TIME_RAW:
			switch(type) {
			case JOB:
				tmp_uint64 = (uint64_t)job->elapsed
					* (uint64_t)cpu_tres_rec_count;
				break;
			case JOBSTEP:
				tmp_uint64 = (uint64_t)step->elapsed
					* (uint64_t)step_cpu_tres_rec_count;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     &tmp_uint64,
					     (curr_inx == field_count));
			break;
		case PRINT_DB_INX:
			switch(type) {
			case JOB:
				tmp_uint64 = job->db_index;
				break;
			case JOBSTEP:
				tmp_uint64 = step->job_ptr->db_index;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     &tmp_uint64,
					     (curr_inx == field_count));
			break;
		case PRINT_DERIVED_EC:
			tmp_int = tmp_int2 = 0;
			switch (type) {
			case JOB:
				if (job->derived_ec == NO_VAL)
					;
				else if (WIFSIGNALED(job->derived_ec))
					tmp_int2 = WTERMSIG(job->derived_ec);
				else if (WIFEXITED(job->derived_ec))
					tmp_int = WEXITSTATUS(job->derived_ec);

				snprintf(outbuf, sizeof(outbuf), "%d:%d",
					 tmp_int, tmp_int2);
				break;
			case JOBCOMP:
				if (job_comp->derived_ec)
					snprintf(outbuf, sizeof(outbuf), "%s",
						 job_comp->derived_ec);
				break;
			default:
				break;
			}

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_ELAPSED:
			switch(type) {
			case JOB:
				tmp_uint64 = job->elapsed;
				break;
			case JOBSTEP:
				tmp_uint64 = step->elapsed;
				break;
			case JOBCOMP:
				tmp_uint64 = job_comp->elapsed_time;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     &tmp_uint64,
					     (curr_inx == field_count));
			break;
		case PRINT_ELAPSED_RAW:
			switch(type) {
			case JOB:
				tmp_uint32 = job->elapsed;
				break;
			case JOBSTEP:
				tmp_uint32 = step->elapsed;
				break;
			case JOBCOMP:
				tmp_uint32 = job_comp->elapsed_time;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     &tmp_uint32,
					     (curr_inx == field_count));
			break;
		case PRINT_ELIGIBLE:
			switch(type) {
			case JOB:
				tmp_time = job->eligible;
				break;
			case JOBSTEP:
				tmp_time = step->start;
				break;
			case JOBCOMP:
				tmp_time = parse_time(job_comp->eligible_time,
						      1);
				break;
			default:
				break;
			}
			field->print_routine(field,
					     &tmp_time,
					     (curr_inx == field_count));
			break;
		case PRINT_END:
			switch(type) {
			case JOB:
				tmp_time = job->end;
				break;
			case JOBSTEP:
				tmp_time = step->end;
				break;
			case JOBCOMP:
				tmp_time = parse_time(job_comp->end_time, 1);
				break;
			default:
				break;
			}
			field->print_routine(field,
					     &tmp_time,
					     (curr_inx == field_count));
			break;
		case PRINT_EXITCODE:
			exit_code = NO_VAL;
			switch (type) {
			case JOB:
				exit_code = job->exitcode;
				break;
			case JOBSTEP:
				exit_code = step->exitcode;
				break;
			case JOBCOMP:
				if (job_comp->exit_code)
					snprintf(outbuf, sizeof(outbuf), "%s",
						 job_comp->exit_code);
				break;
			default:
				break;
			}
			tmp_int = tmp_int2 = 0;
			if (exit_code != NO_VAL) {
				if (WIFSIGNALED(exit_code))
					tmp_int2 = WTERMSIG(exit_code);
				else if (WIFEXITED(exit_code))
					tmp_int = WEXITSTATUS(exit_code);
				if (tmp_int >= 128)
					tmp_int -= 128;
				snprintf(outbuf, sizeof(outbuf), "%d:%d",
					 tmp_int, tmp_int2);
			}
			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_EXTRA:
			switch(type) {
			case JOB:
				tmp_char = job->extra;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_FAILED_NODE:
			switch (type) {
			case JOB:
				tmp_char = job->failed_node;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_FLAGS:
			switch(type) {
			case JOB:
				tmp_uint32 = job->flags;
				break;
			default:
				tmp_uint32 = SLURMDB_JOB_FLAG_NONE;
				break;
			}
			if (tmp_uint32 != SLURMDB_JOB_FLAG_NONE)
				tmp_char = slurmdb_job_flags_str(tmp_uint32);

			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_GID:
			switch(type) {
			case JOB:
				tmp_uint32 = job->gid;
				break;
			case JOBCOMP:
				tmp_uint32 = job_comp->gid;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     &tmp_uint32,
					     (curr_inx == field_count));
			break;
		case PRINT_GROUP:
			switch(type) {
			case JOB:
				tmp_uint32 = job->gid;
				break;
			case JOBCOMP:
				tmp_uint32 = job_comp->gid;
				break;
			default:
				break;
			}
			tmp_char = NULL;
			if ((gr=getgrgid(tmp_uint32)))
				tmp_char=gr->gr_name;

			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_JOBID:
			if (type == JOBSTEP)
				job = step->job_ptr;

			if (job)
				id = slurmdb_get_job_id_str(job);

			switch (type) {
			case JOB:
				tmp_char = id;
				id = NULL;
				break;
			case JOBSTEP:
				tmp_int = FORMAT_STRING_SIZE;
				tmp_char = xmalloc(tmp_int);
				tmp_int2 =
					snprintf(tmp_char, tmp_int, "%s.", id);
				xfree(id);
				tmp_int -= tmp_int2;
				log_build_step_id_str(&step->step_id,
						      tmp_char + tmp_int2,
						      tmp_int,
						      STEP_ID_FLAG_NO_PREFIX |
						      STEP_ID_FLAG_NO_JOB);
				break;
			case JOBCOMP:
				tmp_char = xstrdup_printf("%u",
							  job_comp->jobid);
				break;
			default:
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_JOBIDRAW:
			switch (type) {
			case JOB:
				tmp_char = xstrdup_printf("%u", job->jobid);
				break;
			case JOBSTEP:
				tmp_int = FORMAT_STRING_SIZE;
				tmp_char = xmalloc(tmp_int);
				tmp_int2 = snprintf(tmp_char, tmp_int, "%u.",
						    step->job_ptr->jobid);
				tmp_int -= tmp_int2;
				log_build_step_id_str(&step->step_id,
						      tmp_char + tmp_int2,
						      tmp_int,
						      STEP_ID_FLAG_NO_PREFIX |
						      STEP_ID_FLAG_NO_JOB);
				break;
			case JOBCOMP:
				tmp_char = xstrdup_printf("%u",
							  job_comp->jobid);
				break;
			default:
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_JOBNAME:
			switch(type) {
			case JOB:
				tmp_char = job->jobname;
				break;
			case JOBSTEP:
				tmp_char = step->stepname;
				break;
			case JOBCOMP:
				tmp_char = job_comp->jobname;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_LAYOUT:
		{
			char *name = NULL;

			switch(type) {
			case JOBSTEP:
				name = slurm_step_layout_type_name(
					step->task_dist);
				break;
			default:
				break;
			}
			field->print_routine(field, name,
					     (curr_inx == field_count));
			xfree(name);
			break;
		}
		case PRINT_LICENSES:
			switch(type) {
			case JOB:
				tmp_char = job->licenses;
				break;
			case JOBSTEP:
				break;
			case JOBCOMP:
				break;
			default:
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXDISKREAD:
			tmp_uint64 = _get_tres_cnt(
				type, object, TRES_FS_DISK, 0);

			if (tmp_uint64 != NO_VAL64)
				tmp_dub = (double)tmp_uint64 / (1 << 20);

			_print_small_double(outbuf, sizeof(outbuf),
					    tmp_dub, UNIT_MEGA);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXDISKREADNODE:
			tmp_char = _get_tres_node(
				type, object, TRES_FS_DISK, 0);

			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_MAXDISKREADTASK:
			tmp_uint32 = _get_tres_task(
				type, object, TRES_FS_DISK, 0);

			field->print_routine(field,
					     &tmp_uint32,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXDISKWRITE:
			tmp_uint64 = _get_tres_cnt(type, object, TRES_FS_DISK,
						   SACCT_TRES_OUT);

			if (tmp_uint64 != NO_VAL64)
				tmp_dub = (double)tmp_uint64 / (1 << 20);

			_print_small_double(outbuf, sizeof(outbuf),
					    tmp_dub, UNIT_MEGA);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXDISKWRITENODE:
			tmp_char = _get_tres_node(type, object, TRES_FS_DISK,
						  SACCT_TRES_OUT);

			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_MAXDISKWRITETASK:
			tmp_uint32 = _get_tres_task(type, object, TRES_FS_DISK,
						    SACCT_TRES_OUT);

			field->print_routine(field,
					     &tmp_uint32,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXPAGES:
			tmp_uint64 = _get_tres_cnt(type, object, TRES_PAGES, 0);

			if (tmp_uint64 != NO_VAL64)
				convert_num_unit(
					(double)tmp_uint64,
					outbuf, sizeof(outbuf),
					UNIT_NONE, params.units,
					params.convert_flags);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXPAGESNODE:
			tmp_char = _get_tres_node(type, object, TRES_PAGES, 0);

			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_MAXPAGESTASK:
			tmp_uint32 = _get_tres_task(
				type, object, TRES_PAGES, 0);

			field->print_routine(field,
					     &tmp_uint32,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXRSS:
			tmp_uint64 = _get_tres_cnt(type, object, TRES_MEM, 0);

			if (tmp_uint64 != NO_VAL64)
				convert_num_unit((double)tmp_uint64,
						 outbuf, sizeof(outbuf),
						 UNIT_NONE, params.units,
						 params.convert_flags);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXRSSNODE:
			tmp_char = _get_tres_node(type, object, TRES_MEM, 0);

			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_MAXRSSTASK:
			tmp_uint32 = _get_tres_task(type, object, TRES_MEM, 0);

			field->print_routine(field,
					     &tmp_uint32,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXVSIZE:
			tmp_uint64 = _get_tres_cnt(type, object, TRES_VMEM, 0);

			if (tmp_uint64 != NO_VAL64)
				convert_num_unit((double)tmp_uint64,
						 outbuf, sizeof(outbuf),
						 UNIT_NONE, params.units,
						 params.convert_flags);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_TRESUIA:
			switch(type) {
			case JOBSTEP:
				tmp_char =
					step->stats.tres_usage_in_ave;
				break;
			default:
				break;
			}

			_print_tres_field(tmp_char, NULL, 1,
					  TRES_STR_FLAG_BYTES);
			break;
		case PRINT_TRESUIM:
			switch(type) {
			case JOBSTEP:
				tmp_char = step->stats.tres_usage_in_max;
				break;
			default:
				break;
			}

			_print_tres_field(tmp_char, NULL, 1,
					  TRES_STR_FLAG_BYTES);
			break;
		case PRINT_TRESUIMN:
			switch(type) {
			case JOBSTEP:
				tmp_char = step->stats.tres_usage_in_max_nodeid;
				nodes = step->nodes;
				break;
			default:
				break;
			}

			_print_tres_field(tmp_char, nodes, 0, 0);
			break;
		case PRINT_TRESUIMT:
			switch(type) {
			case JOBSTEP:
				tmp_char = step->stats.tres_usage_in_max_taskid;
				break;
			default:
				break;
			}
			_print_tres_field(tmp_char, NULL, 0, 0);
			break;
		case PRINT_TRESUIMI:
			switch(type) {
			case JOBSTEP:
				tmp_char = step->stats.tres_usage_in_min;
				break;
			default:
				break;
			}

			_print_tres_field(tmp_char, NULL, 1,
					  TRES_STR_FLAG_BYTES);
			break;
		case PRINT_TRESUIMIN:
			switch(type) {
			case JOBSTEP:
				tmp_char = step->stats.tres_usage_in_min_nodeid;
				nodes = step->nodes;
				break;
			default:
				break;
			}

			_print_tres_field(tmp_char, nodes, 0, 0);
			break;
		case PRINT_TRESUIMIT:
			switch(type) {
			case JOBSTEP:
				tmp_char = step->stats.tres_usage_in_min_taskid;
				break;
			default:
				break;
			}
			_print_tres_field(tmp_char, NULL, 0, 0);
			break;
		case PRINT_TRESUIT:
			switch(type) {
			case JOBSTEP:
				tmp_char =
					step->stats.tres_usage_in_tot;
				break;
			default:
				break;
			}
			_print_tres_field(tmp_char, NULL, 1,
					  TRES_STR_FLAG_BYTES);
			break;
		case PRINT_TRESUOA:
			switch(type) {
			case JOBSTEP:
				tmp_char =
					step->stats.tres_usage_out_ave;
				break;
			default:
				break;
			}

			_print_tres_field(tmp_char, NULL, 1,
					  TRES_STR_FLAG_BYTES);
			break;
		case PRINT_TRESUOM:
			switch(type) {
			case JOBSTEP:
				tmp_char = step->stats.tres_usage_out_max;
				break;
			default:
				break;
			}

			_print_tres_field(tmp_char, NULL, 1,
					  TRES_STR_FLAG_BYTES);
			break;
		case PRINT_TRESUOMN:
			switch(type) {
			case JOBSTEP:
				tmp_char =
					step->stats.tres_usage_out_max_nodeid;
				nodes = step->nodes;
				break;
			default:
				break;
			}

			_print_tres_field(tmp_char, nodes, 0, 0);
			break;
		case PRINT_TRESUOMT:
			switch(type) {
			case JOBSTEP:
				tmp_char =
					step->stats.tres_usage_out_max_taskid;
				break;
			default:
				break;
			}

			_print_tres_field(tmp_char, NULL, 0, 0);
			break;
		case PRINT_TRESUOMI:
			switch(type) {
			case JOBSTEP:
				tmp_char = step->stats.tres_usage_out_min;
				break;
			default:
				break;
			}

			_print_tres_field(tmp_char, NULL, 1,
					  TRES_STR_FLAG_BYTES);
			break;
		case PRINT_TRESUOMIN:
			switch(type) {
			case JOBSTEP:
				tmp_char =
					step->stats.tres_usage_out_min_nodeid;
				nodes = step->nodes;
				break;
			default:
				break;
			}

			_print_tres_field(tmp_char, nodes, 0, 0);
			break;
		case PRINT_TRESUOMIT:
			switch(type) {
			case JOBSTEP:
				tmp_char =
					step->stats.tres_usage_out_min_taskid;
				break;
			default:
				break;
			}

			_print_tres_field(tmp_char, NULL, 0, 0);
			break;
		case PRINT_TRESUOT:
			switch(type) {
			case JOBSTEP:
				tmp_char =
					step->stats.tres_usage_out_tot;
				break;
			default:
				break;
			}

			_print_tres_field(tmp_char, NULL, 1,
					  TRES_STR_FLAG_BYTES);
			break;
		case PRINT_MAXVSIZENODE:
			tmp_char = _get_tres_node(type, object, TRES_VMEM, 0);

			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_MAXVSIZETASK:
			tmp_uint32 = _get_tres_task(type, object, TRES_VMEM, 0);

			field->print_routine(field,
					     &tmp_uint32,
					     (curr_inx == field_count));
			break;
		case PRINT_MCS_LABEL:
			switch(type) {
			case JOB:
				if (job->mcs_label)
					tmp_char = job->mcs_label;
				break;
			case JOBSTEP:
				break;
			case JOBCOMP:
				break;
			default:
				tmp_char = "n/a";
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_MINCPU:
			tmp_uint64 = _get_tres_cnt(type, object, TRES_CPU,
						   SACCT_TRES_MIN);

			if (tmp_uint64 != NO_VAL64) {
				tmp_uint64 /= CPU_TIME_ADJ;
				tmp_char = _elapsed_time(tmp_uint64, 0);
			}

			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_MINCPUNODE:
			tmp_char = _get_tres_node(type, object, TRES_CPU,
						  SACCT_TRES_MIN);

			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_MINCPUTASK:
			tmp_uint32 = _get_tres_task(type, object, TRES_CPU,
						    SACCT_TRES_MIN);

			field->print_routine(field,
					     &tmp_uint32,
					     (curr_inx == field_count));
			break;
		case PRINT_NODELIST:
			switch(type) {
			case JOB:
				tmp_char = job->nodes;
				break;
			case JOBSTEP:
				tmp_char = step->nodes;
				break;
			case JOBCOMP:
				tmp_char = job_comp->nodelist;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_NNODES:
			switch(type) {
			case JOB:
				tmp_uint32 = job->alloc_nodes;
				tmp_char = (job->tres_alloc_str &&
					    job->tres_alloc_str[0])
					? job->tres_alloc_str :
					job->tres_req_str;
				break;
			case JOBSTEP:
				tmp_uint32 = step->nnodes;
				tmp_char = step->tres_alloc_str;
				break;
			case JOBCOMP:
				tmp_uint32 = job_comp->node_cnt;
				break;
			default:
				break;
			}

			if (!tmp_uint32 && tmp_char) {
				if ((tmp_uint64 =
				     slurmdb_find_tres_count_in_string(
					     tmp_char, TRES_NODE))
				    != INFINITE64)
					tmp_uint32 = tmp_uint64;
			}
			field->print_routine(field,
					     &tmp_uint32,
					     (curr_inx == field_count));
			break;
		case PRINT_NTASKS:
			switch(type) {
			case JOBSTEP:
				tmp_uint32 = step->ntasks;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     &tmp_uint32,
					     (curr_inx == field_count));
			break;
		case PRINT_PRIO:
			switch(type) {
			case JOB:
				tmp_uint32 = job->priority;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     &tmp_uint32,
					     (curr_inx == field_count));
			break;
		case PRINT_PARTITION:
			switch(type) {
			case JOB:
				tmp_char = job->partition;
				break;
			case JOBCOMP:
				tmp_char = job_comp->partition;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_QOS:
			switch(type) {
			case JOB:
				tmp_int = job->qosid;
				if (!g_qos_list) {
					slurmdb_qos_cond_t qos_cond;
					memset(&qos_cond, 0,
					       sizeof(slurmdb_qos_cond_t));
					qos_cond.with_deleted = 1;
					g_qos_list = slurmdb_qos_get(
						acct_db_conn, &qos_cond);
				}

				tmp_char = _find_qos_name_from_list(g_qos_list,
								    tmp_int);
				break;
			case JOBCOMP:
				tmp_char = job_comp->qos_name;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_QOSRAW:
			switch(type) {
			case JOB:
				tmp_uint32 = job->qosid;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     &tmp_uint32,
					     (curr_inx == field_count));
			break;
		case PRINT_REASON:
			switch(type) {
			case JOB:
				tmp_char = job_reason_string(
					job->state_reason_prev);
				break;
			default:
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_REQ_CPUFREQ_MIN:
			switch (type) {
			case JOBSTEP:
				tmp_uint32 = step->req_cpufreq_min;
				break;
			default:
				break;
			}
			cpu_freq_to_string(outbuf, sizeof(outbuf), tmp_uint32);
			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_REQ_CPUFREQ_MAX:
			switch (type) {
			case JOBSTEP:
				tmp_uint32 = step->req_cpufreq_max;
				break;
			default:
				break;
			}
			cpu_freq_to_string(outbuf, sizeof(outbuf), tmp_uint32);
			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_REQ_CPUFREQ_GOV:
			switch (type) {
			case JOBSTEP:
				tmp_uint32 = step->req_cpufreq_gov;
				break;
			default:
				break;
			}
			cpu_freq_to_string(outbuf, sizeof(outbuf), tmp_uint32);
			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_REQ_CPUS:
			switch(type) {
			case JOB:
				tmp_uint32 = job->req_cpus;
				break;
			case JOBSTEP:
				tmp_uint32 = step_cpu_tres_rec_count;
				break;
			default:

				break;
			}
			field->print_routine(field,
					     &tmp_uint32,
					     (curr_inx == field_count));
			break;
		case PRINT_REQ_MEM:
			switch(type) {
			case JOB:
				tmp_uint64 = slurmdb_find_tres_count_in_string(
					job->tres_req_str, TRES_MEM);
				break;
			default:
				break;
			}

			if (tmp_uint64 != NO_VAL64) {
				convert_num_unit((double)tmp_uint64,
						 outbuf, sizeof(outbuf),
						 UNIT_MEGA, params.units,
						 params.convert_flags);
			}
			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_REQ_NODES:
			switch(type) {
			case JOB:
				tmp_uint32 = 0;
				tmp_char = job->tres_req_str;
				break;
			case JOBSTEP:
				tmp_uint32 = step->nnodes;
				tmp_char = step->tres_alloc_str;
				break;
			case JOBCOMP:
				tmp_uint32 = job_comp->node_cnt;
				break;
			default:
				break;
			}

			if (!tmp_uint32 && tmp_char) {
				if ((tmp_uint64 =
				     slurmdb_find_tres_count_in_string(
					     tmp_char, TRES_NODE))
				    != INFINITE64)
					tmp_uint32 = tmp_uint64;
			}
			convert_num_unit((double)tmp_uint32, outbuf,
					 sizeof(outbuf), UNIT_NONE,
					 params.units, params.convert_flags);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_RESERVATION:
			switch(type) {
			case JOB:
				if (job->resv_name) {
					tmp_char = job->resv_name;
				}
				break;
			case JOBCOMP:
				tmp_char = job_comp->resv_name;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_RESERVATION_ID:
			switch(type) {
			case JOB:
				if (job->resvid)
					tmp_uint32 = job->resvid;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     &tmp_uint32,
					     (curr_inx == field_count));
			break;
		case PRINT_PLANNED:
			/*
			 * If eligible is 0 or -1, then the job was never
			 * eligible to run, so planned time is 0.
			 */
			switch(type) {
			case JOB:
				if (!job->eligible ||
				    (job->eligible == INFINITE))
					tmp_uint64 = 0;
				else if (job->start)
					tmp_uint64 = job->start - job->eligible;
				else
					tmp_uint64 = time(NULL) - job->eligible;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     &tmp_uint64,
					     (curr_inx == field_count));
			break;
		case PRINT_PLANNED_CPU:
			/*
			 * If eligible is 0 or -1, then the job was never
			 * eligible to run, so planned time is 0.
			 */
			switch(type) {
			case JOB:
				if (!job->eligible ||
				    (job->eligible == INFINITE)) {
					tmp_uint64 = 0;
				} else if (job->start) {
					tmp_uint64 = job->start -
							job->eligible;
					tmp_uint64 *= job->req_cpus;
				} else {
					tmp_uint64 = time(NULL) -
							job->eligible;
					tmp_uint64 *= job->req_cpus;
				}
				break;
			default:

				break;
			}
			field->print_routine(field,
					     &tmp_uint64,
					     (curr_inx == field_count));
			break;
		case PRINT_PLANNED_CPU_RAW:
			/*
			 * If eligible is 0 or -1, then the job was never
			 * eligible to run, so planned time is 0.
			 */
			switch(type) {
			case JOB:
				if (!job->eligible ||
				    (job->eligible == INFINITE))
					tmp_int = 0;
				else if (job->start)
					tmp_int = (job->start - job->eligible)
						* job->req_cpus;
				else
					tmp_int = (time(NULL) - job->eligible)
						* job->req_cpus;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     &tmp_int,
					     (curr_inx == field_count));
			break;
		case PRINT_START:
			switch(type) {
			case JOB:
				tmp_time = job->start;
				break;
			case JOBSTEP:
				tmp_time = step->start;
				break;
			case JOBCOMP:
				tmp_time = parse_time(job_comp->start_time, 1);
				break;
			default:
				break;
			}
			field->print_routine(field,
					     &tmp_time,
					     (curr_inx == field_count));
			break;
		case PRINT_STATE:
			switch(type) {
			case JOB:
				tmp_uint32 = job->state;
				tmp2_uint32 = job->requid;
				break;
			case JOBSTEP:
				tmp_uint32 = step->state;
				tmp2_uint32 = step->requid;
				break;
			case JOBCOMP:
				tmp_char = job_comp->state;
				break;
			default:
				break;
			}

			if (((tmp_uint32 & JOB_STATE_BASE) == JOB_CANCELLED) &&
			    (tmp2_uint32 != INFINITE))
				snprintf(outbuf, FORMAT_STRING_SIZE,
					 "%s by %u",
					 job_state_string(tmp_uint32),
					 tmp2_uint32);
			else if (tmp_uint32 != NO_VAL)
				snprintf(outbuf, FORMAT_STRING_SIZE,
					 "%s",
					 job_state_string(tmp_uint32));
			else if (tmp_char)
				snprintf(outbuf, FORMAT_STRING_SIZE,
					 "%s",
					 tmp_char);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_SUBMIT:
			switch(type) {
			case JOB:
				tmp_time = job->submit;
				break;
			case JOBSTEP:
				tmp_time = step->start;
				break;
			case JOBCOMP:
				tmp_time = parse_time(job_comp->start_time, 1);
				break;
			default:
				break;
			}
			field->print_routine(field,
					     &tmp_time,
					     (curr_inx == field_count));
			break;
		case PRINT_SUBMIT_LINE:
			switch(type) {
			case JOB:
				tmp_char = job->submit_line;
				break;
			case JOBSTEP:
				tmp_char = step->submit_line;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_SUSPENDED:
			switch(type) {
			case JOB:
				tmp_uint64 = job->suspended;
				break;
			case JOBSTEP:
				tmp_uint64 = step->suspended;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     &tmp_uint64,
					     (curr_inx == field_count));
			break;
		case PRINT_SYSTEMCPU:
			switch(type) {
			case JOB:
				tmp_uint64 = job->sys_cpu_sec;
				tmp2_uint64 = job->sys_cpu_usec;
				break;
			case JOBSTEP:
				tmp_uint64 = step->sys_cpu_sec;
				tmp2_uint64 = step->sys_cpu_usec;
				break;
			default:
				break;
			}
			tmp_char = _elapsed_time(tmp_uint64, tmp2_uint64);

			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_SYSTEM_COMMENT:
			switch(type) {
			case JOB:
				tmp_char = job->system_comment;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_TIMELIMIT:
			switch (type) {
			case JOB:
				if (job->timelimit == INFINITE)
					tmp_char = "UNLIMITED";
				else if (job->timelimit == NO_VAL)
					tmp_char = "Partition_Limit";
				else if (job->timelimit) {
					mins2time_str(job->timelimit,
						      tmp1, sizeof(tmp1));
					tmp_char = tmp1;
				}
				break;
			case JOBCOMP:
				tmp_char = job_comp->timelimit;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
                case PRINT_TIMELIMIT_RAW:
                        switch (type) {
                        case JOB:
                                if (job->timelimit == INFINITE)
                                        tmp_char = "UNLIMITED";
                                else if (job->timelimit == NO_VAL)
                                        tmp_char = "Partition_Limit";
                                else if (job->timelimit) {
					tmp_int = 1;
                                        tmp_char = xstrdup_printf("%u",
							job->timelimit);
                                }
                                break;
                        case JOBCOMP:
                                tmp_char = job_comp->timelimit;
                                break;
                        default:
                                break;
                        }
                        field->print_routine(field,
                                             tmp_char,
                                             (curr_inx == field_count));
			if (tmp_int == 1)
				xfree(tmp_char);
                        break;
		case PRINT_TOTALCPU:
			switch(type) {
			case JOB:
				tmp_uint64 = job->tot_cpu_sec;
				tmp2_uint64 = job->tot_cpu_usec;
				break;
			case JOBSTEP:
				tmp_uint64 = step->tot_cpu_sec;
				tmp2_uint64 = step->tot_cpu_usec;
				break;
			default:
				break;
			}
			tmp_char = _elapsed_time(tmp_uint64, tmp2_uint64);

			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_TRESA:
			switch(type) {
			case JOB:
				tmp_char = job->tres_alloc_str;
				break;
			case JOBSTEP:
				tmp_char = step->tres_alloc_str;
				break;
			default:
				break;
			}

			_print_tres_field(tmp_char, NULL, 1, 0);
			break;
		case PRINT_TRESR:
			switch(type) {
			case JOB:
				tmp_char = job->tres_req_str;
				break;
			default:
				break;
			}

			_print_tres_field(tmp_char, NULL, 1, 0);
			break;
		case PRINT_UID:
			switch(type) {
			case JOB:
				if (params.use_local_uid && job->user &&
				    (pw = getpwnam(job->user)))
					tmp_uint32 = pw->pw_uid;
				else
					tmp_uint32 = job->uid;
				break;
			case JOBCOMP:
				tmp_uint32 = job_comp->uid;
				break;
			default:
				break;
			}

			field->print_routine(field,
					     &tmp_uint32,
					     (curr_inx == field_count));
			break;
		case PRINT_USER:
			switch(type) {
			case JOB:
				if (job->user)
					tmp_char = job->user;
				else if ((pw=getpwuid(job->uid)))
						tmp_char = pw->pw_name;
				break;
			case JOBCOMP:
				tmp_char = job_comp->uid_name;
				break;
			default:
				break;
			}

			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_USERCPU:
			switch(type) {
			case JOB:
				tmp_uint64 = job->user_cpu_sec;
				tmp2_uint64 = job->user_cpu_usec;
				break;
			case JOBSTEP:
				tmp_uint64 = step->user_cpu_sec;
				tmp2_uint64 = step->user_cpu_usec;
				break;
			default:
				break;
			}

			tmp_char = _elapsed_time(tmp_uint64, tmp2_uint64);

			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_WCKEY:
			switch(type) {
			case JOB:
				tmp_char = job->wckey;
				break;
			case JOBCOMP:
				tmp_char = job_comp->wckey;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_WCKEYID:
			switch(type) {
			case JOB:
				tmp_uint32 = job->wckeyid;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     &tmp_uint32,
					     (curr_inx == field_count));
			break;
		case PRINT_WORK_DIR:
			switch(type) {
			case JOB:
				tmp_char = job->work_dir;
				break;
			case JOBCOMP:
				tmp_char = job_comp->work_dir;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		default:
			break;
		}
		curr_inx++;
	}
	printf("\n");
}

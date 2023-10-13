/*****************************************************************************\
 *  launch.c
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#include <jwt.h>
#include <inttypes.h>
#include <sys/types.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/cred.h"
#include "src/interfaces/serializer.h"

#include "src/plugins/auth/slurm/auth_slurm.h"

#define job_set_field_int(field) \
	data_set_int(data_key_set(data_job, XSTRINGIFY(field)), \
		     cred_arg->job_##field)

#define job_set_field_string(field) \
	data_set_string(data_key_set(data_job, XSTRINGIFY(field)), \
			cred_arg->job_##field)

#define step_set_field_string(field) \
	data_set_string(data_key_set(data_step, XSTRINGIFY(field)), \
			cred_arg->step_##field)

#define job_get_field_int(field) \
	cred_arg->job_##field = \
		data_get_int(data_key_get(data_job, XSTRINGIFY(field)));

#define job_get_field_string(field) \
	cred_arg->job_##field = \
		xstrdup(data_get_string(data_key_get(data_job, \
						     XSTRINGIFY(field))));

#define step_get_field_string(field) \
	cred_arg->step_##field = \
		xstrdup(data_get_string(data_key_get(data_step, \
						     XSTRINGIFY(field))));

extern char *encode_launch(slurm_cred_arg_t *cred_arg)
{
	data_t *data = NULL, *data_launch = NULL;
	data_t *data_job = NULL, *data_step = NULL;
	char *json = NULL;

	data = identity_to_data(cred_arg->id);

	data_launch = data_set_dict(data_key_set(data, "launch"));

	data_set_int(data_key_set(data_launch, "job_id"),
		     cred_arg->step_id.job_id);
	data_set_int(data_key_set(data_launch, "step_id"),
		     cred_arg->step_id.step_id);
	data_set_int(data_key_set(data_launch, "step_het_comp"),
		     cred_arg->step_id.step_het_comp);

	data_job = data_set_dict(data_key_set(data_launch, "job"));
	data_step = data_set_dict(data_key_set(data_launch, "step"));

	job_set_field_int(core_spec);
	job_set_field_int(end_time);
	job_set_field_int(nhosts);
	job_set_field_int(ntasks);
	job_set_field_int(oversubscribe);
	job_set_field_int(restart_cnt);
	job_set_field_int(start_time);
	job_set_field_int(x11);

	job_set_field_string(account);
	job_set_field_string(alias_list);
	job_set_field_string(comment);
	job_set_field_string(constraints);
	job_set_field_string(extra);
	job_set_field_string(hostlist);
	job_set_field_string(licenses);
	job_set_field_string(partition);
	job_set_field_string(reservation);
	job_set_field_string(std_err);
	job_set_field_string(std_in);
	job_set_field_string(std_out);
	job_set_field_string(selinux_context);

	step_set_field_string(hostlist);

	data_t *data_list;

	data_list = data_set_list(data_key_set(data_job, "mem_alloc"));
	for (int i = 0; i < cred_arg->job_mem_alloc_size; i++)
		data_set_int(data_list_append(data_list),
			     cred_arg->job_mem_alloc[i]);
	data_list = data_set_list(data_key_set(data_job, "mem_alloc_rep_count"));
	for (int i = 0; i < cred_arg->job_mem_alloc_size; i++)
		data_set_int(data_list_append(data_list),
			     cred_arg->job_mem_alloc_rep_count[i]);

	data_list = data_set_list(data_key_set(data_step, "mem_alloc"));
	for (int i = 0; i < cred_arg->step_mem_alloc_size; i++)
		data_set_int(data_list_append(data_list),
			     cred_arg->step_mem_alloc[i]);
	data_list = data_set_list(data_key_set(data_step, "mem_alloc_rep_count"));
	for (int i = 0; i < cred_arg->step_mem_alloc_size; i++)
		data_set_int(data_list_append(data_list),
			     cred_arg->step_mem_alloc_rep_count[i]);

	data_list = data_set_list(data_key_set(data_launch, "cores_per_socket"));
	for (int i = 0; i < cred_arg->core_array_size; i++)
		data_set_int(data_list_append(data_list),
			     cred_arg->cores_per_socket[i]);
	data_list = data_set_list(data_key_set(data_launch, "sockets_per_node"));
	for (int i = 0; i < cred_arg->core_array_size; i++)
		data_set_int(data_list_append(data_list),
			     cred_arg->sockets_per_node[i]);
	data_list = data_set_list(data_key_set(data_launch, "sockets_core_rep_count"));
	for (int i = 0; i < cred_arg->core_array_size; i++)
		data_set_int(data_list_append(data_list),
			     cred_arg->sock_core_rep_count[i]);

	data_list = data_set_list(data_key_set(data_launch, "cpu_array"));
	for (int i = 0; i < cred_arg->cpu_array_count; i++)
		data_set_int(data_list_append(data_list),
			     cred_arg->cpu_array[i]);
	data_list = data_set_list(data_key_set(data_launch, "cpu_array_reps"));
	for (int i = 0; i < cred_arg->cpu_array_count; i++)
		data_set_int(data_list_append(data_list),
			     cred_arg->cpu_array_reps[i]);

#if 0


		(void) gres_job_state_pack(cred->job_gres_list, buffer,
					   cred->step_id.job_id, false,
					   protocol_version);
		gres_step_state_pack(cred->step_gres_list, buffer,
				     &cred->step_id, protocol_version);

		slurm_pack_addr_array(
			cred->job_node_addrs,
			cred->job_node_addrs ? cred->job_nhosts : 0,
			buffer);

		if (cred->job_core_bitmap)
			tot_core_cnt = bit_size(cred->job_core_bitmap);
		pack32(tot_core_cnt, buffer);
		pack_bit_str_hex(cred->job_core_bitmap, buffer);
		pack_bit_str_hex(cred->step_core_bitmap, buffer);



		pack32(cred->step_mem_alloc_size, buffer);
			pack64_array(cred->step_mem_alloc,
			pack32_array(cred->step_mem_alloc_rep_count,

#endif

	serialize_g_data_to_string(&json, NULL, data, MIME_TYPE_JSON,
				   SER_FLAGS_COMPACT);

	FREE_NULL_DATA(data);

	return json;
}

#define DATA_FOR_EACH_FUNC(field) \
static data_for_each_cmd_t _each_##field(const data_t *data, void *arg)	\
{									\
	slurm_cred_arg_t *cred_arg = arg;				\
	cred_arg->field[cred_arg->iterator++] = data_get_int(data);	\
	return DATA_FOR_EACH_CONT;					\
}

DATA_FOR_EACH_FUNC(job_mem_alloc);
DATA_FOR_EACH_FUNC(job_mem_alloc_rep_count);
DATA_FOR_EACH_FUNC(step_mem_alloc);
DATA_FOR_EACH_FUNC(step_mem_alloc_rep_count);
DATA_FOR_EACH_FUNC(cores_per_socket);
DATA_FOR_EACH_FUNC(sockets_per_node);
DATA_FOR_EACH_FUNC(sock_core_rep_count);
DATA_FOR_EACH_FUNC(cpu_array);
DATA_FOR_EACH_FUNC(cpu_array_reps);

#define FILL_FIELD(list, field)						\
do {									\
	data_list = data_key_get(list, XSTRINGIFY(field));		\
	cred_arg->iterator = 0;						\
	data_list_for_each_const(data_list, _each_##field, cred_arg);	\
	xassert(cred_arg->iterator == count);				\
} while (0)

extern slurm_cred_t *extract_launch(char *json)
{
	data_t *data_launch = NULL;
	data_t *data_job = NULL, *data_step = NULL;
	data_t *data_list = NULL;
	slurm_cred_t *cred = NULL;
	slurm_cred_arg_t *cred_arg = NULL;
	int count;

	if (serialize_g_string_to_data(&data_launch, json, strlen(json),
				       MIME_TYPE_JSON)) {
		error("%s: failed to decode net field", __func__);
		return NULL;
	}

	cred = slurm_cred_alloc(true);
	cred_arg = cred->arg;

	xassert(cred_arg);

	cred_arg->step_id.job_id =
		data_get_int(data_key_get(data_launch, "job_id"));
	cred_arg->step_id.step_id =
		data_get_int(data_key_get(data_launch, "step_id"));
	cred_arg->step_id.step_het_comp =
		data_get_int(data_key_get(data_launch, "step_het_comp"));

	data_job = data_key_get(data_launch, "job");
	data_step = data_key_get(data_launch, "step");

	job_get_field_int(core_spec);
	job_get_field_int(end_time);
	job_get_field_int(nhosts);
	job_get_field_int(ntasks);
	job_get_field_int(oversubscribe);
	job_get_field_int(restart_cnt);
	job_get_field_int(start_time);
	job_get_field_int(x11);

	job_get_field_string(account);
	job_get_field_string(alias_list);
	job_get_field_string(comment);
	job_get_field_string(constraints);
	job_get_field_string(extra);
	job_get_field_string(hostlist);
	job_get_field_string(licenses);
	job_get_field_string(partition);
	job_get_field_string(reservation);
	job_get_field_string(std_err);
	job_get_field_string(std_in);
	job_get_field_string(std_out);
	job_get_field_string(selinux_context);

	step_get_field_string(hostlist);

	data_list = data_key_get(data_job, "mem_alloc");
	count = cred_arg->job_mem_alloc_size = data_get_list_length(data_list);
	cred_arg->job_mem_alloc = xcalloc(count, sizeof(uint64_t));
	cred_arg->job_mem_alloc_rep_count = xcalloc(count, sizeof(uint32_t));
	FILL_FIELD(data_job, mem_alloc);
	FILL_FIELD(data_job, mem_alloc_rep_count);

	data_list = data_key_get(data_step, "mem_alloc");
	count = cred_arg->step_mem_alloc_size = data_get_list_length(data_list);
	cred_arg->step_mem_alloc = xcalloc(count, sizeof(uint64_t));
	cred_arg->step_mem_alloc_rep_count = xcalloc(count, sizeof(uint32_t));
	FILL_FIELD(data_launch, mem_alloc);
	FILL_FIELD(data_launch, mem_alloc_rep_count);

	data_list = data_key_get(data_launch, "cores_per_socket");
	count = cred_arg->core_array_size = data_get_list_length(data_list);
	cred_arg->cores_per_socket = xcalloc(count, sizeof(uint16_t));
	cred_arg->sockets_per_node = xcalloc(count, sizeof(uint16_t));
	cred_arg->sock_core_rep_count = xcalloc(count, sizeof(uint32_t));
	FILL_FIELD(data_launch, cores_per_socket);
	FILL_FIELD(data_launch, sockets_per_node);
	FILL_FIELD(data_launch, sock_core_rep_count);

	data_list = data_key_get(data_launch, "cpu_array");
	count = cred_arg->cpu_array_count = data_get_list_length(data_list);
	cred_arg->cpu_array = xcalloc(count, sizeof(uint16_t));
	cred_arg->cpu_array_reps = xcalloc(count, sizeof(uint32_t));
	FILL_FIELD(cpu_array);
	FILL_FIELD(cpu_array_reps);

	FREE_NULL_DATA(data_launch);
	return cred;
}

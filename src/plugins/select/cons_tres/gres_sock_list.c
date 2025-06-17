/*****************************************************************************\
 *  gres_sched.c - Scheduling functions used by cons_tres
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *  Derived in large part from code previously in common/gres.c
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

#include "src/common/slurm_xlator.h"

#include "gres_sock_list.h"

#include "src/common/xstring.h"

typedef struct {
	bitstr_t *core_bitmap;
	uint16_t cores_per_sock;
	bitstr_t *gpu_spec_bitmap;
	gres_state_t *gres_state_node;
	const uint32_t node_inx;
	uint32_t res_cores_per_gpu;
	uint16_t sockets;
} foreach_res_gpu_t;

typedef struct {
	gres_sock_list_create_t *create_args;
	gres_job_state_t **gres_js_resv;
	list_t *gres_list_resv;
} foreach_gres_sock_list_create_t;

static void _handle_gres_exc_topo(resv_exc_t *resv_exc_ptr, int node_inx,
				  int topo_inx, gres_node_state_t *gres_ns,
				  uint64_t *avail_gres, bool use_total_gres,
				  char *gres_name)
{
	gres_job_state_t *gres_js;
	uint64_t gres_cnt = 0;
	uint64_t orig_avail_gres = *avail_gres;

	if (!resv_exc_ptr)
		return;

	/*
	 * If this job is not in a reservation we must exclude all gres in
	 * reservations.
	 * Otherwise, if this job is in a reservation we must include only gres
	 * included in the reservation.
	 */
	gres_js = resv_exc_ptr->gres_js_exc ?
		resv_exc_ptr->gres_js_exc : resv_exc_ptr->gres_js_inc;

	if (!gres_js || !gres_js->gres_bit_alloc ||
	    !gres_js->gres_bit_alloc[node_inx]) {
		if (resv_exc_ptr->gres_list_inc) { /* In a reservation */
			log_flag(SELECT_TYPE, "Can't use %s (topo:%d) on node %s because it is not included in the reservation",
				 gres_name, topo_inx,
				 node_record_table_ptr[node_inx]->name);
			*avail_gres = 0;
		}
		return;
	}

	if (!use_total_gres && !gres_ns->no_consume) {
		bitstr_t *tmp_bitmap =
			bit_copy(gres_ns->topo_gres_bitmap[topo_inx]);
		bit_and(tmp_bitmap, gres_js->gres_bit_alloc[node_inx]);
		bit_and_not(tmp_bitmap, gres_ns->gres_bit_alloc);
		gres_cnt = bit_set_count(tmp_bitmap);
		FREE_NULL_BITMAP(tmp_bitmap);
	} else {
		gres_cnt = bit_overlap(gres_js->gres_bit_alloc[node_inx],
				       gres_ns->topo_gres_bitmap[topo_inx]);
	}

	if (resv_exc_ptr->gres_js_exc) {
		*avail_gres -= MIN(gres_cnt, *avail_gres);
	} else
		*avail_gres = gres_cnt;

	/* If we change the avail_gres print a message */
	if (orig_avail_gres != *avail_gres) {
		log_flag(SELECT_TYPE, "%s (topo: %d) avail_gres for node %s is now %"PRIu64" because of reservations",
			 gres_name, topo_inx,
			 node_record_table_ptr[node_inx]->name, *avail_gres);
	}

	return;
}

static void _handle_gres_exc_by_type(resv_exc_t *resv_exc_ptr,
				     gres_job_state_t *gres_js_in,
				     int node_inx, uint64_t *avail_gres)
{
	gres_job_state_t *gres_js;

	if (!resv_exc_ptr)
		return;

	gres_js = resv_exc_ptr->gres_js_exc ?
		resv_exc_ptr->gres_js_exc : resv_exc_ptr->gres_js_inc;

	if (!gres_js ||
	    (gres_js->type_name && (gres_js->type_id != gres_js_in->type_id))) {
		if (resv_exc_ptr->gres_list_inc) { /* In a reservation */
			*avail_gres = 0;
		}
		return;
	}

	if (resv_exc_ptr->gres_js_exc) {
		if (gres_js->gres_cnt_node_alloc[node_inx] >= *avail_gres)
			*avail_gres = 0;
		else
			*avail_gres -= gres_js->gres_cnt_node_alloc[node_inx];
	} else
		*avail_gres = gres_js->gres_cnt_node_alloc[node_inx];

	log_flag(SELECT_TYPE, "avail_gres for node %d is now %"PRIu64,
		 node_inx, *avail_gres);

	return;
}

static void _handle_gres_exc_basic(resv_exc_t *resv_exc_ptr,
				   gres_job_state_t *gres_js_in,
				   int node_inx, uint64_t *avail_gres)
{
	gres_job_state_t *gres_js;

	if (!resv_exc_ptr)
		return;

	gres_js = resv_exc_ptr->gres_js_exc ?
		resv_exc_ptr->gres_js_exc : resv_exc_ptr->gres_js_inc;

	if (!gres_js) {
		if (resv_exc_ptr->gres_list_inc) { /* In a reservation */
			*avail_gres = 0;
		}
		return;
	}
	if (resv_exc_ptr->gres_js_exc) {
		if (gres_js->gres_cnt_node_alloc[node_inx] >= *avail_gres)
			*avail_gres = 0;
		else
			*avail_gres -= gres_js->gres_cnt_node_alloc[node_inx];
	} else
		*avail_gres = gres_js->gres_cnt_node_alloc[node_inx];

	log_flag(SELECT_TYPE, "avail_gres for node %d is now %"PRIu64,
		 node_inx, *avail_gres);

	return;
}

static void _handle_gres_exc_bit_and_not(resv_exc_t *resv_exc_ptr,
					 bitstr_t *bits_by_sock, int node_inx)
{
	gres_job_state_t *gres_js;

	if (!resv_exc_ptr)
		return;

	gres_js = resv_exc_ptr->gres_js_exc;

	if (!gres_js || !gres_js->gres_bit_alloc ||
	    !gres_js->gres_bit_alloc[node_inx])
		return;

	bit_and_not(bits_by_sock, gres_js->gres_bit_alloc[node_inx]);

	return;
}

/*
 * Determine how many GRES of a given type can be used by this job on a
 * given node and return a structure with the details. Note that multiple
 * GRES of a given type model can be distributed over multiple topo structures,
 * so we need to OR the core_bitmap over all of them.
 */
static sock_gres_t *_build_sock_gres_by_topo(
	gres_state_t *gres_state_job,
	gres_state_t *gres_state_node,
	gres_sock_list_create_t *create_args)
{
	gres_job_state_t *gres_js = gres_state_job->gres_data;
	gres_node_state_t *gres_ns = gres_state_node->gres_data;
	resv_exc_t *resv_exc_ptr = create_args->resv_exc_ptr;
	bool use_total_gres = create_args->use_total_gres;
	bitstr_t *core_bitmap = create_args->core_bitmap;
	uint16_t sockets = create_args->sockets;
	uint16_t cores_per_sock = create_args->cores_per_sock;
	uint32_t res_cores_per_gpu = create_args->res_cores_per_gpu;
	char *node_name = create_args->node_name;
	bool enforce_binding = create_args->enforce_binding;
	uint32_t s_p_n = NO_VAL; /* No need to optimize socket */

	gres_node_state_t *alt_gres_ns = NULL;
	int i, j, s, c;
	uint32_t tot_cores;
	sock_gres_t *sock_gres;
	int64_t add_gres;
	uint64_t avail_gres, min_gres = 0;
	bool match = false;
	bool use_busy_dev = gres_use_busy_dev(gres_state_node, use_total_gres);

	if (gres_ns->gres_cnt_avail == 0)
		return NULL;

	if (!use_total_gres)
		alt_gres_ns = gres_ns->alt_gres ?
			gres_ns->alt_gres->gres_data : NULL;

	sock_gres = xmalloc(sizeof(sock_gres_t));
	sock_gres->sock_cnt = sockets;
	sock_gres->bits_by_sock = xcalloc(sockets, sizeof(bitstr_t *));
	sock_gres->cnt_by_sock = xcalloc(sockets, sizeof(uint64_t));

	for (i = 0; i < gres_ns->topo_cnt; i++) {
		bool use_all_sockets = false;
		if (gres_js->type_name &&
		    (gres_js->type_id != gres_ns->topo_type_id[i]))
			continue;	/* Wrong type_model */
		if (use_busy_dev &&
		    (gres_ns->topo_gres_cnt_alloc[i] == 0))
			continue;
		if (!use_total_gres && !gres_ns->no_consume &&
		    (gres_ns->topo_gres_cnt_alloc[i] >=
		     gres_ns->topo_gres_cnt_avail[i])) {
			continue;	/* No GRES remaining */
		}

		if (!use_total_gres && !gres_ns->no_consume) {
			avail_gres = gres_ns->topo_gres_cnt_avail[i] -
				gres_ns->topo_gres_cnt_alloc[i];
		} else {
			avail_gres = gres_ns->topo_gres_cnt_avail[i];
		}
		if (avail_gres == 0)
			continue;

		_handle_gres_exc_topo(resv_exc_ptr, create_args->node_inx, i,
				      gres_ns, &avail_gres, use_total_gres,
				      gres_state_node->gres_name);
		if (avail_gres == 0)
			continue;

		/*
		 * Job requested SHARING or SHARED. Filter out resources already
		 * allocated to the other GRES type.
		 */
		if (alt_gres_ns && alt_gres_ns->gres_bit_alloc &&
		    gres_ns->topo_gres_bitmap[i]) {
			c = bit_overlap(gres_ns->topo_gres_bitmap[i],
					alt_gres_ns->gres_bit_alloc);
			if (c > 0) {
				/*
				 * Here we are using the main one to determine
				 * if the alt is shared or not. If it main one
				 * is shared then we just continue here
				 * otherwise we know the alt is shared.
				 */
				if (gres_id_shared(
					    gres_state_node->config_flags))
					continue;
				else {
					avail_gres -= c;
					if (avail_gres == 0)
						continue;
				}
			}
		}

		/*
		 * Check that shared avail_gres is at least equal to
		 * the amount of gres_per_task if defined, we definitely do not
		 * want to accumulate shards per socket that could not be really
		 * used by the tasks on the node at the end (see bug 23010)
		 * This is because we allow only one sharing gres per task.
		 * (See _set_shared_task_bits() in gres_select_filter.c)
		 */
		if (gres_id_shared(gres_state_node->config_flags) &&
		    gres_js->gres_per_task &&
		    gres_js->gres_per_task > avail_gres)
			continue;

		/* By default only allow one sharing gres per job */
		if (gres_id_shared(gres_state_node->config_flags) &&
		    !(slurm_conf.select_type_param &
		      MULTIPLE_SHARING_GRES_PJ) &&
		    (avail_gres > sock_gres->max_node_gres) && !use_total_gres)
			/*
			 * Test use_total_gres so we don't reject shared gres
			 * jobs as never runnable (see bug 15283)
			 */
			sock_gres->max_node_gres = avail_gres;

		tot_cores = sockets * cores_per_sock;
		if ((core_bitmap && (tot_cores != bit_size(core_bitmap))) ||
		    (gres_ns->topo_core_bitmap[i] &&
		     (tot_cores != bit_size(gres_ns->topo_core_bitmap[i])))) {
			error("%s: Core bitmaps size mismatch on node %s",
			      __func__, node_name);
			match = false;
			break;
		}
		/*
		 * If some GRES is available on every socket,
		 * treat like no topo_core_bitmap is specified
		 */
		if (gres_ns->topo_core_bitmap &&
		    gres_ns->topo_core_bitmap[i] &&
		    !res_cores_per_gpu) {
			use_all_sockets = true;
			for (s = 0; s < sockets; s++) {
				bool use_this_socket = false;
				for (c = 0; c < cores_per_sock; c++) {
					j = (s * cores_per_sock) + c;
					if (bit_test(gres_ns->
						     topo_core_bitmap[i], j)) {
						use_this_socket = true;
						break;
					}
				}
				if (!use_this_socket) {
					use_all_sockets = false;
					break;
				}
			}
		}

		if (gres_ns->topo_gres_bitmap[i] &&
		    (!gres_ns->topo_core_bitmap ||
		     !gres_ns->topo_core_bitmap[i] ||
		     use_all_sockets)) {
			/*
			 * Not constrained by core, but only specific
			 * GRES may be available (save their bitmap)
			 */
			sock_gres->cnt_any_sock += avail_gres;
			sock_gres->total_cnt += avail_gres;
			if (!sock_gres->bits_any_sock) {
				sock_gres->bits_any_sock =
					bit_copy(gres_ns->
						 topo_gres_bitmap[i]);
			} else {
				bit_or(sock_gres->bits_any_sock,
				       gres_ns->topo_gres_bitmap[i]);
			}
			match = true;
			continue;
		}

		/* Constrained by core */
		for (s = 0; ((s < sockets) && avail_gres); s++) {
			if (enforce_binding && core_bitmap) {
				for (c = 0; c < cores_per_sock; c++) {
					j = (s * cores_per_sock) + c;
					if (bit_test(core_bitmap, j))
						break;
				}
				if (c >= cores_per_sock) {
					/* No available cores on this socket */
					continue;
				}
			}
			for (c = 0; c < cores_per_sock; c++) {
				j = (s * cores_per_sock) + c;
				if (gres_ns->topo_core_bitmap[i] &&
				    !bit_test(gres_ns->topo_core_bitmap[i],
					      j))
					continue;
				if (!gres_ns->topo_gres_bitmap[i]) {
					error("%s: topo_gres_bitmap NULL on node %s",
					      __func__, node_name);
					continue;
				}
				if (!sock_gres->bits_by_sock[s]) {
					sock_gres->bits_by_sock[s] =
						bit_copy(gres_ns->
							 topo_gres_bitmap[i]);
				} else {
					bit_or(sock_gres->bits_by_sock[s],
					       gres_ns->topo_gres_bitmap[i]);
				}

				_handle_gres_exc_bit_and_not(
					resv_exc_ptr,
					sock_gres->bits_by_sock[s],
					create_args->node_inx);
				sock_gres->cnt_by_sock[s] += avail_gres;
				sock_gres->total_cnt += avail_gres;
				avail_gres = 0;
				match = true;
				break;
			}
		}
	}

	/* Process per-GRES limits */
	if (match && gres_js->gres_per_socket) {
		/*
		 * Clear core bitmap on sockets with insufficient GRES
		 * and disable excess GRES per socket
		 */
		for (s = 0; s < sockets; s++) {
			if (sock_gres->cnt_by_sock[s] <
			    gres_js->gres_per_socket) {
				/* Insufficient GRES, clear count */
				sock_gres->total_cnt -=
					sock_gres->cnt_by_sock[s];
				sock_gres->cnt_by_sock[s] = 0;
				if (enforce_binding && core_bitmap) {
					i = s * cores_per_sock;
					bit_nclear(core_bitmap, i,
						   i + cores_per_sock - 1);
				}
			} else if (sock_gres->cnt_by_sock[s] >
				   gres_js->gres_per_socket) {
				/* Excess GRES, reduce count */
				i = sock_gres->cnt_by_sock[s] -
					gres_js->gres_per_socket;
				sock_gres->cnt_by_sock[s] =
					gres_js->gres_per_socket;
				sock_gres->total_cnt -= i;
			}
		}
	}

	/* Maximize GRES per node */
	if (gres_js->gres_per_job && !gres_js->gres_per_socket)
		s_p_n = create_args->s_p_n;

	/*
	 * Satisfy sockets-per-node (s_p_n) limit by selecting the sockets with
	 * the most GRES. Sockets with low GRES counts have their core_bitmap
	 * cleared so that _allocate_sc() in cons_tres/job_test.c does not
	 * remove sockets needed to satisfy the job's GRES specification.
	 */
	if (match && enforce_binding && core_bitmap && (s_p_n < sockets)) {
		int avail_sock = 0;
		bool *avail_sock_flag = xcalloc(sockets, sizeof(bool));
		for (s = 0; s < sockets; s++) {
			if (sock_gres->cnt_by_sock[s] == 0)
				continue;
			for (c = 0; c < cores_per_sock; c++) {
				i = (s * cores_per_sock) + c;
				if (!bit_test(core_bitmap, i))
					continue;
				avail_sock++;
				avail_sock_flag[s] = true;
				break;
			}
		}
		while (avail_sock > s_p_n) {
			int low_gres_sock_inx = -1;
			for (s = 0; s < sockets; s++) {
				if (!avail_sock_flag[s])
					continue;
				if ((low_gres_sock_inx == -1) ||
				    (sock_gres->cnt_by_sock[s] <
				     sock_gres->cnt_by_sock[low_gres_sock_inx]))
					low_gres_sock_inx = s;
			}
			if (low_gres_sock_inx == -1)
				break;
			s = low_gres_sock_inx;
			i = s * cores_per_sock;
			bit_nclear(core_bitmap, i, i + cores_per_sock - 1);
			sock_gres->total_cnt -= sock_gres->cnt_by_sock[s];
			sock_gres->cnt_by_sock[s] = 0;
			avail_sock--;
			avail_sock_flag[s] = false;
		}
		xfree(avail_sock_flag);
	}

	if (match) {
		if (gres_js->gres_per_node)
			min_gres = gres_js->gres_per_node;
		if (gres_js->gres_per_task)
			min_gres = MAX(min_gres, gres_js->gres_per_task);
		if (sock_gres->total_cnt < min_gres)
			match = false;
	}


	/*
	 * Identify sockets which are required to satisfy
	 * gres_per_node or task specification so that allocated tasks
	 * can be distributed over multiple sockets if necessary.
	 */
	add_gres = min_gres - sock_gres->cnt_any_sock;
	if (match && core_bitmap && (add_gres > 0)) {
		int best_sock_inx = -1;
		bool *avail_sock_flag = xcalloc(sockets, sizeof(bool));
		for (s = 0; s < sockets; s++) {
			if (sock_gres->cnt_by_sock[s] == 0)
				continue;
			for (c = 0; c < cores_per_sock; c++) {
				i = (s * cores_per_sock) + c;
				if (!bit_test(core_bitmap, i))
					continue;
				avail_sock_flag[s] = true;
				if ((best_sock_inx == -1) ||
				    (sock_gres->cnt_by_sock[s] >
				     sock_gres->cnt_by_sock[best_sock_inx])) {
					best_sock_inx = s;
				}
				break;
			}
		}
		while ((best_sock_inx != -1) && (add_gres > 0)) {
			if (!create_args->req_sock_map)
				create_args->req_sock_map = bit_alloc(sockets);
			bit_set(create_args->req_sock_map, best_sock_inx);
			add_gres -= sock_gres->cnt_by_sock[best_sock_inx];
			avail_sock_flag[best_sock_inx] = false;
			if (add_gres <= 0)
				break;
			/* Find next best socket */
			best_sock_inx = -1;
			for (s = 0; s < sockets; s++) {
				if ((sock_gres->cnt_by_sock[s] == 0) ||
				    !avail_sock_flag[s])
					continue;
				if ((best_sock_inx == -1) ||
				    (sock_gres->cnt_by_sock[s] >
				     sock_gres->cnt_by_sock[best_sock_inx])) {
					best_sock_inx = s;
				}
			}
		}
		xfree(avail_sock_flag);
	}

	if (!match) {
		gres_sock_delete(sock_gres);
		sock_gres = NULL;
	}
	return sock_gres;
}

/*
 * Determine how many GRES of a given type can be used by this job on a
 * given node and return a structure with the details. Note that multiple
 * GRES of a given type model can be configured, so pick the right one.
 */
static sock_gres_t *_build_sock_gres_by_type(
	gres_job_state_t *gres_js,
	gres_node_state_t *gres_ns,
	gres_sock_list_create_t *create_args)
{
	bool use_total_gres = create_args->use_total_gres;
	int i;
	sock_gres_t *sock_gres;
	uint64_t avail_gres, min_gres = 1, gres_tmp;
	bool match = false;

	if (gres_js->gres_per_node)
		min_gres = gres_js-> gres_per_node;
	if (gres_js->gres_per_socket)
		min_gres = MAX(min_gres, gres_js->gres_per_socket);
	if (gres_js->gres_per_task)
		min_gres = MAX(min_gres, gres_js->gres_per_task);
	sock_gres = xmalloc(sizeof(sock_gres_t));
	for (i = 0; i < gres_ns->type_cnt; i++) {
		if (gres_js->type_name &&
		    (gres_js->type_id != gres_ns->type_id[i]))
			continue;	/* Wrong type_model */
		if (!use_total_gres &&
		    (gres_ns->type_cnt_alloc[i] >=
		     gres_ns->type_cnt_avail[i])) {
			continue;	/* No GRES remaining */
		} else if (!use_total_gres) {
			avail_gres = gres_ns->type_cnt_avail[i] -
				gres_ns->type_cnt_alloc[i];
		} else {
			avail_gres = gres_ns->type_cnt_avail[i];
		}

		_handle_gres_exc_by_type(create_args->resv_exc_ptr, gres_js,
					 create_args->node_inx, &avail_gres);

		gres_tmp = gres_ns->gres_cnt_avail;
		if (!use_total_gres)
			gres_tmp -= gres_ns->gres_cnt_alloc;
		avail_gres = MIN(avail_gres, gres_tmp);
		if (avail_gres < min_gres)
			continue;	/* Insufficient GRES remaining */
		sock_gres->cnt_any_sock += avail_gres;
		sock_gres->total_cnt += avail_gres;
		match = true;
	}
	if (!match)
		xfree(sock_gres);

	return sock_gres;
}

/*
 * Determine how many GRES of a given type can be used by this job on a
 * given node and return a structure with the details. No GRES type.
 */
static sock_gres_t *_build_sock_gres_basic(
	gres_job_state_t *gres_js,
	gres_node_state_t *gres_ns,
	gres_sock_list_create_t *create_args)
{
	sock_gres_t *sock_gres;
	uint64_t avail_gres, min_gres = 1;

	if (gres_js->type_name)
		return NULL;
	if (!create_args->use_total_gres &&
	    (gres_ns->gres_cnt_alloc >= gres_ns->gres_cnt_avail))
		return NULL;	/* No GRES remaining */

	if (gres_js->gres_per_node)
		min_gres = gres_js-> gres_per_node;
	if (gres_js->gres_per_socket)
		min_gres = MAX(min_gres, gres_js->gres_per_socket);
	if (gres_js->gres_per_task)
		min_gres = MAX(min_gres, gres_js->gres_per_task);
	if (!create_args->use_total_gres) {
		avail_gres = gres_ns->gres_cnt_avail -
			gres_ns->gres_cnt_alloc;
	} else
		avail_gres = gres_ns->gres_cnt_avail;

	_handle_gres_exc_basic(create_args->resv_exc_ptr, gres_js,
			       create_args->node_inx, &avail_gres);

	if (avail_gres < min_gres)
		return NULL;	/* Insufficient GRES remaining */

	sock_gres = xmalloc(sizeof(sock_gres_t));
	sock_gres->cnt_any_sock += avail_gres;
	sock_gres->total_cnt += avail_gres;

	return sock_gres;
}

static int _foreach_sock_gres_log(void *x, void *arg)
{
	sock_gres_t *sock_gres = x;
	gres_job_state_t *gres_js = sock_gres->gres_state_job->gres_data;
	char tmp[32] = "";
	int len = -1;

	info("Gres:%s Type:%s TotalCnt:%"PRIu64" MaxNodeGres:%"PRIu64,
	     sock_gres->gres_state_job->gres_name, gres_js->type_name,
	     sock_gres->total_cnt, sock_gres->max_node_gres);
	if (sock_gres->bits_any_sock) {
		bit_fmt(tmp, sizeof(tmp), sock_gres->bits_any_sock);
		len = bit_size(sock_gres->bits_any_sock);
	}
	info("  Sock[ANY]Cnt:%"PRIu64" Bits:%s of %d",
	     sock_gres->cnt_any_sock, tmp, len);

	for (int i = 0; i < sock_gres->sock_cnt; i++) {
		if (sock_gres->cnt_by_sock[i] == 0)
			continue;

		tmp[0] = '\0';
		len = -1;

		if (sock_gres->bits_by_sock && sock_gres->bits_by_sock[i]) {
			bit_fmt(tmp, sizeof(tmp), sock_gres->bits_by_sock[i]);
			len = bit_size(sock_gres->bits_by_sock[i]);
		}
		info("  Sock[%d]Cnt:%"PRIu64" Bits:%s of %d", i,
		     sock_gres->cnt_by_sock[i], tmp, len);
	}
	return 0;
}

static void _sock_gres_log(list_t *sock_gres_list, char *node_name)
{
	if (!sock_gres_list)
		return;

	info("Sock_gres state for %s", node_name);
	(void) list_for_each(sock_gres_list, _foreach_sock_gres_log, NULL);
}

/* Return true if group_size cores could be selected in the given range */
static bool _pick_core_group(bitstr_t *gpu_res_core_bitmap,
			     bitstr_t *core_bitmap,
			     gres_job_state_t *gres_js, int cur_inx,
			     int max_inx, uint16_t group_size,
			     int *picked_cores)
{
	xassert(picked_cores);
	int cnt = 0;

	while (cnt != group_size && (cur_inx < max_inx)) {
		if (!bit_test(gpu_res_core_bitmap, cur_inx) ||
		    !bit_test(core_bitmap, cur_inx)) {
			cur_inx++;
			continue;
		}
		picked_cores[cnt] = cur_inx;
		cnt++;
		cur_inx++;
	}
	return cnt == group_size;
}

/*
 * Reduce the number of restricted cores to just that of the gpu type requested
 */
static void _pick_restricted_cores(bitstr_t *core_bitmap,
				   bitstr_t *gpu_spec_cpy,
				   gres_job_state_t *gres_js,
				   gres_node_state_t *gres_ns,
				   uint32_t res_cores_per_gpu,
				   uint16_t sockets,
				   uint16_t cores_per_sock,
				   uint32_t node_i)
{
	int *picked_cores = xcalloc(res_cores_per_gpu, sizeof(int));

	if (!gres_js->res_gpu_cores) {
		gres_js->res_array_size = node_record_count;
		gres_js->res_gpu_cores = xcalloc(gres_js->res_array_size,
						 sizeof(bitstr_t *));
	}
	/*
	 * This function can be called multiple times for the same node_i while
	 * a job is pending. Free any existing gres_js->res_gpu_cores[node_i]
	 * first.
	 */
	FREE_NULL_BITMAP(gres_js->res_gpu_cores[node_i]);
	gres_js->res_gpu_cores[node_i] = bit_alloc(bit_size(core_bitmap));

	for (int i = 0; i < gres_ns->topo_cnt; i++) {
		if (!gres_ns->topo_res_core_bitmap[i])
			continue;
		if (gres_js->type_name &&
		    (gres_js->type_id != gres_ns->topo_type_id[i]))
			continue;
		for (int s = 0; s < sockets; s++) {
			int max_inx = (s + 1) * cores_per_sock;
			for (int c = 0; c < cores_per_sock; c++) {
				int cur_inx = (s * cores_per_sock) + c;
				/*
				 * Need to pick in groups of res_cores_per_gpu
				 * since not every gpu job will use all the
				 * restricted cores allowed.
				 */
				if (!_pick_core_group(
					    gres_ns->topo_res_core_bitmap[i],
					    core_bitmap, gres_js,
					    cur_inx, max_inx,
					    res_cores_per_gpu,
					    picked_cores))
					break;

				c = picked_cores[res_cores_per_gpu - 1] -
					(s * cores_per_sock);
				for (int j = 0; j < res_cores_per_gpu; j++) {
					bit_set(gpu_spec_cpy, picked_cores[j]);
					bit_set(gres_js->res_gpu_cores[node_i],
						picked_cores[j]);
				}
			}
		}
	}

	xfree(picked_cores);
}

static int _foreach_restricted_gpu(void *x, void *arg)
{
	gres_state_t *gres_state_job = x;
	foreach_res_gpu_t *args = arg;
	gres_job_state_t  *gres_js;

	if ((gres_state_job->plugin_id != gres_get_gpu_plugin_id()) ||
	    !args->res_cores_per_gpu)
		return SLURM_SUCCESS;
	gres_js = gres_state_job->gres_data;

	_pick_restricted_cores(args->core_bitmap, args->gpu_spec_bitmap,
			       gres_js, args->gres_state_node->gres_data,
			       args->res_cores_per_gpu, args->sockets,
			       args->cores_per_sock, args->node_inx);

	return SLURM_SUCCESS;
}

static void _gres_limit_reserved_cores(gres_sock_list_create_t *create_args)
{
	gres_state_t *gres_state_node;
	gres_node_state_t *gres_ns;
	bitstr_t *gpu_spec_cpy;
	uint32_t gpu_plugin_id = gres_get_gpu_plugin_id();
	foreach_res_gpu_t args = {
		.core_bitmap = create_args->core_bitmap,
		.cores_per_sock = create_args->cores_per_sock,
		.node_inx = create_args->node_inx,
		.res_cores_per_gpu = create_args->res_cores_per_gpu,
		.sockets = create_args->sockets,
	};

	if (!create_args->gpu_spec_bitmap || !create_args->core_bitmap ||
	    !create_args->job_gres_list || !create_args->node_gres_list)
		return;

	gres_state_node = list_find_first(create_args->node_gres_list,
					  gres_find_id,
					  &gpu_plugin_id);
	if (!gres_state_node)
		return;

	gres_ns = gres_state_node->gres_data;

	if (!gres_ns || !gres_ns->topo_cnt || !gres_ns->topo_core_bitmap)
		return;

	gpu_spec_cpy = bit_copy(create_args->gpu_spec_bitmap);
	args.gpu_spec_bitmap = gpu_spec_cpy;
	args.gres_state_node = gres_state_node;

	list_for_each(create_args->job_gres_list, _foreach_restricted_gpu,
		      &args);
	bit_and(create_args->core_bitmap, gpu_spec_cpy);
	bit_free(gpu_spec_cpy);
}

static int _foreach_gres_sock_list_create(void *x, void *arg)
{
	gres_state_t *gres_state_job = x;
	foreach_gres_sock_list_create_t *foreach_create_args = arg;
	gres_sock_list_create_t *create_args = foreach_create_args->create_args;
	sock_gres_t *sock_gres = NULL;
	gres_job_state_t *gres_js = gres_state_job->gres_data;
	gres_node_state_t *gres_ns;
	gres_state_t *gres_state_node =
		list_find_first(create_args->node_gres_list,
				gres_find_id,
				&gres_state_job->plugin_id);
	node_record_t *node_ptr = node_record_table_ptr[create_args->node_inx];

	if (!gres_state_node) {
		/* node lack GRES of type required by the job */
		FREE_NULL_LIST(create_args->sock_gres_list);
		return -1;
	}
	gres_ns = gres_state_node->gres_data;

	if (foreach_create_args->gres_list_resv) {
		gres_key_t job_search_key = {
			.config_flags = gres_state_job->config_flags,
			.plugin_id = gres_state_job->plugin_id,
			.type_id = gres_js->type_id,
		};

		gres_state_t *gres_state_job_tmp =
			list_find_first(foreach_create_args->gres_list_resv,
					gres_find_job_by_key_exact_type,
					&job_search_key);
		if (gres_state_job_tmp)
			*foreach_create_args->gres_js_resv =
				gres_state_job_tmp->gres_data;
		else
			*foreach_create_args->gres_js_resv = NULL;
	}

	if (create_args->core_bitmap &&
	    (bit_ffs(create_args->core_bitmap) == -1)) {
		sock_gres = NULL; /* No cores available */
	} else if (gres_ns->topo_cnt &&
		   (gres_ns->gres_cnt_found != NO_VAL64 ||
		    !(IS_NODE_UNKNOWN(node_ptr) ||
		      IS_NODE_DOWN(node_ptr) ||
		      IS_NODE_DRAIN(node_ptr) ||
		      IS_NODE_NO_RESPOND(node_ptr)))) {
		/*
		 * If the node has not yet registered and isn't
		 * available to allocate jobs, we build the list with
		 * _build_sock_gres_by_type() (which uses the newest
		 * slurm.conf gres configuration) so that it won't be
		 * rejected as never runnable.
		 */
		sock_gres = _build_sock_gres_by_topo(
			gres_state_job,	gres_state_node, create_args);
	} else if (gres_ns->type_cnt) {
		sock_gres = _build_sock_gres_by_type(
			gres_js, gres_ns, create_args);
	} else {
		sock_gres = _build_sock_gres_basic(
			gres_js, gres_ns, create_args);
	}
	if (!sock_gres) {
		/* node lack available resources required by the job */
		bit_clear_all(create_args->core_bitmap);
		FREE_NULL_LIST(create_args->sock_gres_list);
		return -1;
	}
	sock_gres->use_total_gres = create_args->use_total_gres;
	sock_gres->gres_state_job = gres_state_job;
	sock_gres->gres_state_node = gres_state_node;
	list_append(create_args->sock_gres_list, sock_gres);

	return 0;
}

extern void gres_sock_list_create(gres_sock_list_create_t *create_args)
{
	foreach_gres_sock_list_create_t foreach_create_args = {
		.create_args = create_args,
	};

	FREE_NULL_LIST(create_args->sock_gres_list);

	if (!create_args->job_gres_list ||
	    (list_count(create_args->job_gres_list) == 0)) {
		if (create_args->gpu_spec_bitmap && create_args->core_bitmap)
			bit_and(create_args->core_bitmap,
				create_args->gpu_spec_bitmap);
		return;
	}
	if (!create_args->node_gres_list) /* Node lacks GRES to match */
		return;
	(void) gres_init();

	if (!(create_args->cr_type & CR_SOCKET))
		_gres_limit_reserved_cores(create_args);

	if (create_args->resv_exc_ptr) {
		if (create_args->resv_exc_ptr->gres_list_exc) {
			foreach_create_args.gres_list_resv =
				create_args->resv_exc_ptr->gres_list_exc;
			foreach_create_args.gres_js_resv = (gres_job_state_t **)
				&create_args->resv_exc_ptr->gres_js_exc;
		} else if (create_args->resv_exc_ptr->gres_list_inc) {
			foreach_create_args.gres_list_resv =
				create_args->resv_exc_ptr->gres_list_inc;
			foreach_create_args.gres_js_resv = (gres_job_state_t **)
				&create_args->resv_exc_ptr->gres_js_inc;
		}
	}

	create_args->sock_gres_list = list_create(gres_sock_delete);
	(void) list_for_each(create_args->job_gres_list,
			     _foreach_gres_sock_list_create,
			     &foreach_create_args);

	if (slurm_conf.debug_flags & DEBUG_FLAG_GRES)
		_sock_gres_log(create_args->sock_gres_list,
			       create_args->node_name);
}

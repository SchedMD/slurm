/*
 * Interface between lower-level ALPS XML-RPC functions and SLURM.
 *
 * Copyright (c) 2010-11 Centro Svizzero di Calcolo Scientifico (CSCS)
 * Licensed under GPLv2.
 */
#include "basil_interface.h"
#include "basil_alps.h"
#include "src/common/gres.h"
#include "src/common/slurm_accounting_storage.h"

#define _DEBUG 0

int dim_size[3] = {0, 0, 0};

typedef struct args_sig_basil {
	uint32_t resv_id;
	int      signal;
	uint16_t delay;
} args_sig_basil_t;

/*
 * Following routines are from src/plugins/select/bluegene/plugin/jobinfo.c
 */
static int _set_select_jobinfo(select_jobinfo_t *jobinfo,
			       enum select_jobdata_type data_type, void *data)
{
	uint32_t *uint32 = (uint32_t *) data;
	uint8_t  *uint8  = (uint8_t *)  data;

	if (jobinfo == NULL) {
		error("cray/set_select_jobinfo: jobinfo not set");
		return SLURM_ERROR;
	}
	if (jobinfo->magic != JOBINFO_MAGIC) {
		error("cray/set_select_jobinfo: jobinfo magic bad");
		return SLURM_ERROR;
	}

	switch (data_type) {
	case SELECT_JOBDATA_CONFIRMED:
		jobinfo->confirmed = *uint8;
		break;
	case SELECT_JOBDATA_RESV_ID:
		jobinfo->reservation_id = *uint32;
		break;
	default:
		error("cray/set_select_jobinfo: data_type %d invalid",
		      data_type);
	}

	return SLURM_SUCCESS;
}

static int _get_select_jobinfo(select_jobinfo_t *jobinfo,
			       enum select_jobdata_type data_type, void *data)
{
	uint64_t *uint64 = (uint64_t *) data;
	uint32_t *uint32 = (uint32_t *) data;
	uint8_t  *uint8  = (uint8_t *)  data;

	if (jobinfo == NULL) {
		error("cray/get_select_jobinfo: jobinfo not set");
		return SLURM_ERROR;
	}
	if (jobinfo->magic != JOBINFO_MAGIC) {
		error("cray/get_select_jobinfo: jobinfo magic bad");
		return SLURM_ERROR;
	}

	switch (data_type) {
	case SELECT_JOBDATA_CONFIRMED:
		*uint8 = jobinfo->confirmed;
		break;
	case SELECT_JOBDATA_RESV_ID:
		*uint32 = jobinfo->reservation_id;
		break;
	case SELECT_JOBDATA_PAGG_ID:
		*uint64 = jobinfo->confirm_cookie;
		break;
	default:
		error("cray/get_select_jobinfo: data_type %d invalid",
		      data_type);
	}

	return SLURM_SUCCESS;
}

/** Convert between Cray NID and slurm nodename format */
static struct node_record *_find_node_by_basil_id(uint32_t node_id)
{
	char nid[9];	/* nid%05d\0 */

	snprintf(nid, sizeof(nid), "nid%05u", node_id);

	return find_node_record(nid);
}

extern int basil_node_ranking(struct node_record *node_array, int node_cnt)
{
	enum basil_version version = get_basil_version();
	struct basil_inventory *inv;
	struct basil_node *node;
	int rank_count = 0, i;
	hostlist_t hl = hostlist_create(NULL);
	bool bad_node = 0;

	node_rank_inv = 1;
	/*
	 * When obtaining the initial configuration, we can not allow ALPS to
	 * fail. If there is a problem at this stage it is better to restart
	 * SLURM completely, after investigating (and/or fixing) the cause.
	 */
	inv = get_full_inventory(version);
	if (inv == NULL)
		fatal("failed to get BASIL %s ranking", bv_names_long[version]);
	else if (!inv->batch_total)
		fatal("system has no usable batch compute nodes");
	else if (inv->batch_total < node_cnt)
		info("Warning: ALPS sees only %d/%d slurm.conf nodes, "
		     "check DownNodes", inv->batch_total, node_cnt);

	debug("BASIL %s RANKING INVENTORY: %d/%d batch nodes",
	      bv_names_long[version], inv->batch_avail, inv->batch_total);

	/*
	 * Node ranking is based on a subset of the inventory: only nodes in
	 * batch allocation mode which are up and not allocated. Assign a
	 * 'NO_VAL' rank to all other nodes, which will translate as a very
	 * high value, (unsigned)-2, to put those nodes last in the ranking.
	 * The rest of the code must ensure that those nodes are never chosen.
	 */
	for (i = 0; i < node_cnt; i++)
		node_array[i].node_rank = NO_VAL;

	for (node = inv->f->node_head; node; node = node->next) {
		struct node_record *node_ptr;
		char tmp[50];

		/* This will ignore interactive nodes when iterating through
		 * the apbasil inventory.  If we don't do this, SLURM is
		 * unable to resolve the ID to a nidXXX name since it's not in
		 * the slurm.conf file.  (Chris North)
		 */
		if (node->role == BNR_INTER)
			continue;

		node_ptr = _find_node_by_basil_id(node->node_id);
		if (node_ptr == NULL) {
			error("nid%05u (%s node in state %s) not in slurm.conf",
			      node->node_id, nam_noderole[node->role],
			      nam_nodestate[node->state]);
			bad_node = 1;
		} else if ((slurmctld_conf.fast_schedule != 2)
			   && (node->cpu_count != node_ptr->config_ptr->cpus)) {
			fatal("slurm.conf: node %s has %u cpus "
			      "but configured as CPUs=%u in your slurm.conf",
			      node_ptr->name, node->cpu_count,
			      node_ptr->config_ptr->cpus);
		} else if ((slurmctld_conf.fast_schedule != 2)
			   && (node->mem_size
			       != node_ptr->config_ptr->real_memory)) {
			fatal("slurm.conf: node %s has RealMemory=%u "
			      "but configured as RealMemory=%u in your "
			      "slurm.conf",
			      node_ptr->name, node->mem_size,
			      node_ptr->config_ptr->real_memory);
		} else {
			node_ptr->node_rank = inv->nodes_total - rank_count++;
			/*
			 * Convention: since we are using SLURM in
			 *             frontend-mode, we use
			 *             NodeHostName as follows.
			 *
			 * NodeHostName:  c#-#c#s#n# using the  NID convention
			 *                <cabinet>-<row><chassis><slot><node>
			 * - each cabinet can accommodate 3 chassis (c1..c3)
			 * - each chassis has 8 slots               (s0..s7)
			 * - each slot contains 2 or 4 nodes        (n0..n3)
			 *   o either 2 service nodes (n0/n3)
			 *   o or 4 compute nodes     (n0..n3)
			 *   o or 2 gemini chips      (g0/g1 serving n0..n3)
			 *
			 * Example: c0-0c1s0n1
			 *          - c0- = cabinet 0
			 *          - 0   = row     0
			 *          - c1  = chassis 1
			 *          - s0  = slot    0
			 *          - n1  = node    1
			 */
			xfree(node_ptr->node_hostname);
			node_ptr->node_hostname = xstrdup(node->name);
		}

		sprintf(tmp, "nid%05u", node->node_id);
		hostlist_push_host(hl, tmp);
	}
	free_inv(inv);
	if (bad_node) {
		hostlist_sort(hl);
		char *name = hostlist_ranged_string_xmalloc(hl);
		info("It appears your slurm.conf nodelist doesn't "
		     "match the alps system.  Here are the nodes alps knows "
		     "about\n%s", name);
	}
	hostlist_destroy(hl);
	node_rank_inv = 0;

	return SLURM_SUCCESS;
}

/**
 * basil_inventory - Periodic node-state query via ALPS XML-RPC.
 * This should be run immediately before each scheduling cycle.
 * Returns non-SLURM_SUCCESS if
 * - INVENTORY method failed (error)
 * - no nodes are available (no point in scheduling)
 * - orphaned ALPS reservation exists (wait until ALPS resynchronizes)
 */
extern int basil_inventory(void)
{
	enum basil_version version = get_basil_version();
	struct basil_inventory *inv;
	struct basil_node *node;
	struct basil_rsvn *rsvn;
	int slurm_alps_mismatch = 0;
	int rc = SLURM_SUCCESS;
	int rel_rc;
	time_t now = time(NULL);
	static time_t slurm_alps_mismatch_time = (time_t) 0;
	static bool logged_sync_timeout = false;
	static time_t last_inv_run = 0;

	if ((now - last_inv_run) < inv_interval)
		return SLURM_SUCCESS;

	last_inv_run = now;

	inv = get_full_inventory(version);
	if (inv == NULL) {
		error("BASIL %s INVENTORY failed", bv_names_long[version]);
		return SLURM_ERROR;
	}

	debug("BASIL %s INVENTORY: %d/%d batch nodes available",
	      bv_names_long[version], inv->batch_avail, inv->batch_total);

	/* Avoid checking for inv->batch_avail here since if we are
	   gang scheduling returning an error for a full system is
	   probably the wrong thing to do. (the schedule() function
	   in the slurmctld will never run ;)).
	*/
	if (!inv->f->node_head || !inv->batch_total)
		rc = ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;

	for (node = inv->f->node_head; node; node = node->next) {
		int node_inx;
		struct node_record *node_ptr;
		char *reason = NULL;

		/* This will ignore interactive nodes when iterating through
		 * the apbasil inventory.  If we don't do this, SLURM is
		 * unable to resolve the ID to a nidXXX name since it's not in
		 * the slurm.conf file.  (Chris North)
		 */
		if (node->role == BNR_INTER)
			continue;

		node_ptr = _find_node_by_basil_id(node->node_id);
		if (node_ptr == NULL) {
			error("nid%05u (%s node in state %s) not in slurm.conf",
			      node->node_id, nam_noderole[node->role],
			      nam_nodestate[node->state]);
			continue;
		}
		node_inx = node_ptr - node_record_table_ptr;

		if (node_is_allocated(node) && !IS_NODE_ALLOCATED(node_ptr)) {
			/*
			 * ALPS still hangs on to the node while SLURM considers
			 * it already unallocated. Possible causes are partition
			 * cleanup taking too long (can be 10sec ... minutes),
			 * and orphaned ALPS reservations (caught below).
			 *
			 * The converse case (SLURM hanging on to the node while
			 * ALPS has already freed it) happens frequently during
			 * job completion: select_g_job_fini() is called before
			 * make_node_comp(). Rely on SLURM logic for this case.
			 */
			slurm_alps_mismatch++;
		}

		if (node->state == BNS_DOWN) {
			reason = "ALPS marked it DOWN";
		} else if (node->state == BNS_UNAVAIL) {
			reason = "node is UNAVAILABLE";
		} else if (node->state == BNS_ROUTE) {
			reason = "node does ROUTING";
		} else if (node->state == BNS_SUSPECT) {
			reason = "entered SUSPECT mode";
		} else if (node->state == BNS_ADMINDOWN) {
			reason = "node is ADMINDOWN";
		} else if (node->state != BNS_UP) {
			reason = "state not UP";
		} else if (node->role != BNR_BATCH) {
			reason = "mode not BATCH";
		} else if (node->arch != BNA_XT) {
			reason = "arch not XT/XE";
		}

		/* Base state entirely derives from ALPS */
		if (reason) {
			if (node_ptr->down_time == 0)
				node_ptr->down_time = now;
			if (IS_NODE_DOWN(node_ptr)) {
				/* node still down */
			} else if ((slurmctld_conf.slurmd_timeout == 0) ||
				   ((now - node_ptr->down_time) <
				    slurmctld_conf.slurmd_timeout)) {
				node_ptr->node_state |= NODE_STATE_NO_RESPOND;
				bit_clear(avail_node_bitmap, node_inx);
			} else {
				xfree(node_ptr->reason);
				info("MARKING %s DOWN (%s)",
				     node_ptr->name, reason);
				/* set_node_down also kills any running jobs */
				set_node_down_ptr(node_ptr, reason);
			}
		} else if (IS_NODE_DOWN(node_ptr)) {
			xfree(node_ptr->reason);
			node_ptr->down_time = 0;
			info("MARKING %s UP", node_ptr->name);

			/* Reset state, make_node_idle figures out the rest */
			node_ptr->node_state &= NODE_STATE_FLAGS;
			node_ptr->node_state &= (~NODE_STATE_NO_RESPOND);
			node_ptr->node_state |= NODE_STATE_UNKNOWN;

			make_node_idle(node_ptr, NULL);
			if (!IS_NODE_DRAIN(node_ptr) &&
			    !IS_NODE_FAIL(node_ptr)) {
				xfree(node_ptr->reason);
				node_ptr->reason_time = 0;
				node_ptr->reason_uid = NO_VAL;
				clusteracct_storage_g_node_up(
					acct_db_conn, node_ptr, now);
			}
		} else if (IS_NODE_NO_RESPOND(node_ptr)) {
			node_ptr->node_state &= (~NODE_STATE_NO_RESPOND);
			if (!IS_NODE_DRAIN(node_ptr) &&
			    !IS_NODE_FAIL(node_ptr)) {
				bit_set(avail_node_bitmap, node_inx);
			}
		}
	}

	if (slurm_alps_mismatch)
		debug("ALPS: %d node(s) still held", slurm_alps_mismatch);

	/*
	 * Check that each ALPS reservation corresponds to a SLURM job.
	 * Purge orphaned reservations, which may result from stale or
	 * messed up system state, or are indicative of ALPS problems
	 * (stuck in pending cancel calls).
	 */
	for (rsvn = inv->f->rsvn_head; rsvn; rsvn = rsvn->next) {
		ListIterator job_iter = list_iterator_create(job_list);
		struct job_record *job_ptr;
		uint32_t resv_id;

		while ((job_ptr = (struct job_record *)list_next(job_iter))) {
			if (_get_select_jobinfo(job_ptr->select_jobinfo->data,
						SELECT_JOBDATA_RESV_ID,
						&resv_id) == SLURM_SUCCESS
			    && resv_id == rsvn->rsvn_id)
				break;
		}
		list_iterator_destroy(job_iter);

		/*
		 * Changed to ignore reservations for "UNKNOWN" batch
		 * ids (e.g. the interactive region) (Chris North)
		 */

		if ((job_ptr == NULL) && (strcmp(rsvn->batch_id, "UNKNOWN"))) {
			error("orphaned ALPS reservation %u, trying to remove",
			      rsvn->rsvn_id);
			rel_rc = basil_safe_release(rsvn->rsvn_id, inv);
			if (rel_rc) {
				error("ALPS reservation %u removal FAILED: %s",
				      rsvn->rsvn_id, basil_strerror(rel_rc));
			} else {
				debug("ALPS reservation %u removed",
				      rsvn->rsvn_id);
			}
			slurm_alps_mismatch = true;
		}
	}
	free_inv(inv);

	if (slurm_alps_mismatch) {
		/* If SLURM and ALPS state are not in synchronization,
		 * do not schedule any more jobs until waiting at least
		 * SyncTimeout seconds. */
		if (slurm_alps_mismatch_time == 0) {
			slurm_alps_mismatch_time = now;
		} else if (cray_conf->sync_timeout == 0) {
			/* Wait indefinitely */
		} else if (difftime(now, slurm_alps_mismatch_time) <
			   cray_conf->sync_timeout) {
			return ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
		} else if (!logged_sync_timeout) {
			error("Could not synchronize SLURM with ALPS for %u "
			      "seconds, proceeding with job scheduling",
			      cray_conf->sync_timeout);
			logged_sync_timeout = true;
		}
	} else {
		slurm_alps_mismatch_time = 0;
		logged_sync_timeout = false;
	}
	return rc;
}

/** Base-36 encoding of @coord */
static char _enc_coord(uint8_t coord)
{
	return coord + (coord < 10 ? '0' : 'A' - 10);
}

/**
 * basil_geometry - Check node attributes, resolve (X,Y,Z) coordinates.
 *
 * Checks both SDB database and ALPS inventory for consistency. The inventory
 * part is identical to basil_inventory(), with the difference of being called
 * before valid bitmaps exist, from select_g_node_init().
 * Its dependencies are:
 * - it needs reset_job_bitmaps() in order to rebuild node_bitmap fields,
 * - it relies on _sync_nodes_to_jobs() to
 *   o kill active jobs on nodes now marked DOWN,
 *   o reset node state to ALLOCATED if it has been marked IDLE here (which is
 *     an error case, since there is no longer an ALPS reservation for the job,
 *     this is caught by the subsequent basil_inventory()).
 */
extern int basil_geometry(struct node_record *node_ptr_array, int node_cnt)
{
	struct node_record *node_ptr, *end = node_ptr_array + node_cnt;
	enum basil_version version = get_basil_version();
	struct basil_inventory *inv;

	/* General mySQL */
	MYSQL		*handle;
	MYSQL_STMT	*stmt = NULL;
	/* Input parameters */
	unsigned int	node_id;
	/*
	 * Use a left outer join here since the attributes table may not be
	 * populated for a given nodeid (e.g. when the node has been disabled
	 * on the SMW via 'xtcli disable').
	 * The processor table has more authoritative information, if a nodeid
	 * is not listed there, it does not exist.
	 */
	const char query[] =	"SELECT x_coord, y_coord, z_coord, "
		"processor_type FROM processor WHERE processor_id = ? ";
	const int	PARAM_COUNT = 1;	/* node id */
	MYSQL_BIND	params[PARAM_COUNT];

	int		x_coord, y_coord, z_coord;
	char		proc_type[BASIL_STRING_SHORT];
	MYSQL_BIND	bind_cols[COLUMN_COUNT];
	my_bool		is_null[COLUMN_COUNT];
	my_bool		is_error[COLUMN_COUNT];
	int		is_gemini, i;
	time_t		now = time(NULL);

	memset(params, 0, sizeof(params));
	params[0].buffer_type = MYSQL_TYPE_LONG;
	params[0].is_unsigned = true;
	params[0].is_null     = (my_bool *)0;
	params[0].buffer      = (char *)&node_id;

	memset(bind_cols, 0, sizeof(bind_cols));
	for (i = 0; i < COLUMN_COUNT; i ++) {
		bind_cols[i].is_null = &is_null[i];
		bind_cols[i].error   = &is_error[i];

		if (i == COL_TYPE) {
			bind_cols[i].buffer_type   = MYSQL_TYPE_STRING;
			bind_cols[i].buffer_length = sizeof(proc_type);
			bind_cols[i].buffer	   = proc_type;
		} else {
			bind_cols[i].buffer_type   = MYSQL_TYPE_LONG;
			bind_cols[i].is_unsigned   = (i >= COL_TYPE);
		}
	}
	bind_cols[COL_X].buffer	     = (char *)&x_coord;
	bind_cols[COL_Y].buffer	     = (char *)&y_coord;
	bind_cols[COL_Z].buffer	     = (char *)&z_coord;

	inv = get_full_inventory(version);
	if (inv == NULL)
		fatal("failed to get initial BASIL inventory");

	info("BASIL %s initial INVENTORY: %d/%d batch nodes available",
	      bv_names_long[version], inv->batch_avail, inv->batch_total);

	handle = cray_connect_sdb();
	if (handle == NULL)
		fatal("can not connect to XTAdmin database on the SDB");

	is_gemini = cray_is_gemini_system(handle);
	if (is_gemini < 0)
		fatal("can not determine Cray XT/XE system type");

	stmt = prepare_stmt(handle, query, params, PARAM_COUNT,
			    bind_cols, COLUMN_COUNT);
	if (stmt == NULL)
		fatal("can not prepare statement to resolve Cray coordinates");

	for (node_ptr = node_record_table_ptr; node_ptr < end; node_ptr++) {
		struct basil_node *node;
		char *reason = NULL;

		if ((node_ptr->name == NULL) ||
		    (sscanf(node_ptr->name, "nid%05u", &node_id) != 1)) {
			error("can not read basil_node_id from %s",
				node_ptr->name);
			continue;
		}

		if (exec_stmt(stmt, query, bind_cols, COLUMN_COUNT) < 0)
			fatal("can not resolve %s coordinates", node_ptr->name);

		if (fetch_stmt(stmt) == 0) {
#if _DEBUG
			info("proc_type:%s xyz:%u:%u:%u",
			     proc_type, x_coord, y_coord, z_coord
#endif
			if (strcmp(proc_type, "compute") != 0) {
				/*
				 * Switching a compute node to be a service node
				 * can not happen at runtime: requires a reboot.
				 */
				fatal("Node '%s' is a %s node. "
				      "Only compute nodes can appear in slurm.conf.",
					node_ptr->name, proc_type);
			} else if (is_null[COL_X] || is_null[COL_Y]
				   || is_null[COL_Z]) {
				/*
				 * Similar case to the one above, observed when
				 * a blade has been removed. Node will not
				 * likely show up in ALPS.
				 */
				x_coord = y_coord = z_coord = 0;
				reason = "unknown coordinates - hardware failure?";
			}

		} else if (is_gemini) {
			fatal("Non-existing Gemini node '%s' in slurm.conf",
			      node_ptr->name);
		} else {
			fatal("Non-existing SeaStar node '%s' in slurm.conf",
			      node_ptr->name);
		}

		if (!is_gemini) {
			/*
			 * SeaStar: each node has unique coordinates
			 */
			if (node_ptr->arch == NULL)
				node_ptr->arch = xstrdup("XT");
		} else {
			/*
			 * Gemini: each 2 nodes share the same network
			 * interface (i.e., nodes 0/1 and 2/3 each have
			 * the same coordinates).
			 */
			if (node_ptr->arch == NULL)
				node_ptr->arch = xstrdup("XE");
		}

		/*
		 * Convention: since we are using SLURM in frontend-mode,
		 *             we use NodeAddr as follows.
		 *
		 * NodeAddr:      <X><Y><Z> coordinates in base-36 encoding
		 */
		xfree(node_ptr->comm_name);
		node_ptr->comm_name = xstrdup_printf("%c%c%c",
						     _enc_coord(x_coord),
						     _enc_coord(y_coord),
						     _enc_coord(z_coord));
		dim_size[0] = MAX(dim_size[0], (x_coord - 1));
		dim_size[1] = MAX(dim_size[1], (y_coord - 1));
		dim_size[2] = MAX(dim_size[2], (z_coord - 1));
#if _DEBUG
		info("%s  %s  %s reason=%s", node_ptr->name,
		     node_ptr->node_hostname, node_ptr->comm_name,
		     reason);
#endif
		/*
		 * Check the current state reported by ALPS inventory, unless it
		 * is already evident that the node has some other problem.
		 */
		if (reason == NULL) {
			for (node = inv->f->node_head; node; node = node->next)
				if (node->node_id == node_id)
					break;
			if (node == NULL) {
				reason = "not visible to ALPS - check hardware";
			} else if (node->state == BNS_DOWN) {
				reason = "ALPS marked it DOWN";
			} else if (node->state == BNS_UNAVAIL) {
				reason = "node is UNAVAILABLE";
			} else if (node->state == BNS_ROUTE) {
				reason = "node does ROUTING";
			} else if (node->state == BNS_SUSPECT) {
				reason = "entered SUSPECT mode";
			} else if (node->state == BNS_ADMINDOWN) {
				reason = "node is ADMINDOWN";
			} else if (node->state != BNS_UP) {
				reason = "state not UP";
			} else if (node->role != BNR_BATCH) {
				reason = "mode not BATCH";
			} else if (node->arch != BNA_XT) {
				reason = "arch not XT/XE";
			}
		}

		/* Base state entirely derives from ALPS
		 * NOTE: The node bitmaps are not defined when this code is
		 * initially executed. */
		node_ptr->node_state &= NODE_STATE_FLAGS;
		if (reason) {
			if (node_ptr->down_time == 0)
				node_ptr->down_time = now;
			if (IS_NODE_DOWN(node_ptr)) {
				/* node still down */
				debug("Initial DOWN node %s - %s",
					node_ptr->name, node_ptr->reason);
			} else if (slurmctld_conf.slurmd_timeout &&
				   ((now - node_ptr->down_time) <
				    slurmctld_conf.slurmd_timeout)) {
				node_ptr->node_state |= NODE_STATE_NO_RESPOND;
			} else {
				info("Initial DOWN node %s - %s",
				     node_ptr->name, reason);
				node_ptr->reason = xstrdup(reason);
				/* Node state flags preserved above */
				node_ptr->node_state |= NODE_STATE_DOWN;
				clusteracct_storage_g_node_down(acct_db_conn,
								node_ptr,
								now, NULL,
								slurm_get_slurm_user_id());
			}
		} else {
			bool node_up_flag = IS_NODE_DOWN(node_ptr) &&
					    !IS_NODE_DRAIN(node_ptr) &&
					    !IS_NODE_FAIL(node_ptr);
			node_ptr->down_time = 0;
			if (node_is_allocated(node))
				node_ptr->node_state |= NODE_STATE_ALLOCATED;
			else
				node_ptr->node_state |= NODE_STATE_IDLE;
			node_ptr->node_state &= (~NODE_STATE_NO_RESPOND);
			xfree(node_ptr->reason);
			if (node_up_flag) {
				info("ALPS returned node %s to service",
				     node_ptr->name);
				clusteracct_storage_g_node_up(acct_db_conn,
							      node_ptr, now);
			}
		}

		free_stmt_result(stmt);
	}

	if (stmt_close(stmt))
		error("error closing statement: %s", mysql_stmt_error(stmt));
	cray_close_sdb(handle);
	free_inv(inv);

	return SLURM_SUCCESS;
}

struct basil_accel_param* build_accel_param(struct job_record* job_ptr)
{
	int gpu_mem_req;
	struct basil_accel_param* head,* bap_ptr;

	gpu_mem_req = gres_plugin_get_job_value_by_type(job_ptr->gres_list,
							"gpu_mem");

	if (gpu_mem_req == NO_VAL)
		gpu_mem_req = 0;

	if (!job_ptr) {
		info("The job_ptr is NULL; nothing to do!");
		return NULL;
	} else if (!job_ptr->details) {
		info("The job_ptr->details is NULL; nothing to do!");
		return NULL;
	}

	head = xmalloc(sizeof(struct basil_accel_param));
	bap_ptr = head;
	bap_ptr->type = BA_GPU;	/* Currently BASIL only permits
				 * generic resources of type GPU. */
	bap_ptr->memory_mb = gpu_mem_req;
	bap_ptr->next = NULL;

	return head;
}


/**
 * do_basil_reserve - create a BASIL reservation.
 * IN job_ptr - pointer to job which has just been allocated resources
 * RET 0 or error code, job will abort or be requeued on failure
 */
extern int do_basil_reserve(struct job_record *job_ptr)
{
	struct nodespec *ns_head = NULL;
	/* mppmem must be at least 1 for gang scheduling to work so
	 * if you are wondering why gang scheduling isn't working you
	 * should check your slurm.conf for DefMemPerNode */
	uint32_t mppdepth, mppnppn = INFINITE, mppwidth = 0,
		mppmem = 0, node_min_mem = 0;
	uint32_t resv_id, largest_cpus = 0, min_memory = INFINITE;
	int i, first_bit, last_bit;
	long rc;
	char *user, batch_id[16];
	struct basil_accel_param* bap;
	uint16_t nppcu = 0;

	if (!job_ptr->job_resrcs || job_ptr->job_resrcs->nhosts == 0)
		return SLURM_SUCCESS;

	debug3("job #%u: %u nodes = %s, cpus=%u" , job_ptr->job_id,
		job_ptr->job_resrcs->nhosts,
		job_ptr->job_resrcs->nodes,
		job_ptr->job_resrcs->ncpus
	);

	if (job_ptr->job_resrcs->node_bitmap == NULL) {
		error("job %u node_bitmap not set", job_ptr->job_id);
		return SLURM_SUCCESS;
	}

	first_bit = bit_ffs(job_ptr->job_resrcs->node_bitmap);
	last_bit  = bit_fls(job_ptr->job_resrcs->node_bitmap);
	if (first_bit == -1 || last_bit == -1)
		return SLURM_SUCCESS;		/* no nodes allocated */

	if (cray_conf->sub_alloc) {
		mppdepth = MAX(1, job_ptr->details->cpus_per_task);
		if (!job_ptr->details->ntasks_per_node
		    && job_ptr->details->num_tasks) {
			mppnppn = (job_ptr->details->num_tasks +
				   job_ptr->job_resrcs->nhosts - 1) /
				job_ptr->job_resrcs->nhosts;
		} else
			mppnppn  = job_ptr->details->ntasks_per_node;
	} else {
		/* always be 1 */
		mppdepth = 1;
	}

	/* mppmem */
	if (job_ptr->details->pn_min_memory & MEM_PER_CPU) {
		/* Only honour --mem-per-cpu if --ntasks has been given */
		if (job_ptr->details->num_tasks)
			mppmem = job_ptr->details->pn_min_memory & ~MEM_PER_CPU;
	} else if (job_ptr->details->pn_min_memory) {
		node_min_mem = job_ptr->details->pn_min_memory;
	}

	if (slurmctld_conf.select_type_param & CR_ONE_TASK_PER_CORE) {
		if (job_ptr->details && job_ptr->details->mc_ptr &&
		    (job_ptr->details->mc_ptr->ntasks_per_core == 0xffff)) {
			nppcu = 1;
			debug("No explicit ntasks-per-core has been set, "
			      "using nppcu=1.");
		}
	}

	if (job_ptr->details && job_ptr->details->mc_ptr &&
	    (job_ptr->details->mc_ptr->ntasks_per_core != 0xffff)) {
		nppcu = job_ptr->details->mc_ptr->ntasks_per_core;
	}

	for (i = first_bit; i <= last_bit; i++) {
		struct node_record *node_ptr = node_record_table_ptr + i;
		uint32_t node_cpus, node_mem;
		uint16_t threads = 1;
		uint32_t basil_node_id;

		if (!bit_test(job_ptr->job_resrcs->node_bitmap, i))
			continue;

		if (!node_ptr->name || node_ptr->name[0] == '\0')
			continue;	/* bad node */

		if (sscanf(node_ptr->name, "nid%05u", &basil_node_id) != 1)
			fatal("can not read basil_node_id from %s",
			      node_ptr->name);

		if (ns_add_node(&ns_head, basil_node_id, false) != 0) {
			error("can not add node %s (nid%05u)", node_ptr->name,
			      basil_node_id);
			free_nodespec(ns_head);
			return SLURM_ERROR;
		}

		if (slurmctld_conf.fast_schedule) {
			node_cpus = node_ptr->config_ptr->cpus;
			threads = node_ptr->config_ptr->threads;
			node_mem  = node_ptr->config_ptr->real_memory;
		} else {
			node_cpus = node_ptr->cpus;
			threads = node_ptr->threads;
			node_mem  = node_ptr->real_memory;
		}

		if (cray_conf->sub_alloc) {
			if (node_min_mem) {
				int32_t tmp_mppmem;

				/* If the job has requested memory use it (if
				   lesser) for calculations.
				*/
				tmp_mppmem = MIN(node_mem, node_min_mem);
				/*
				 * ALPS 'Processing Elements per Node'
				 * value (aprun -N), which in slurm is
				 * --ntasks-per-node and 'mppnppn' in PBS: if
				 * --ntasks is specified, default to the
				 * number of cores per node (also the
				 * default for 'aprun -N').  On a
				 * heterogeneous system the nodes
				 * aren't always the same so keep
				 * track of the lowest mppmem and use
				 * it as the level for all nodes
				 * (mppmem is 0 when coming in).
				 */
				tmp_mppmem /= mppnppn ? mppnppn : node_cpus;

				/* Minimum memory per processing
				 * element should be 1, since 0 means
				 * give all the memory to the job. */
				if (tmp_mppmem <= 0)
					tmp_mppmem = 1;

				if (mppmem)
					mppmem = MIN(mppmem, tmp_mppmem);
				else
					mppmem = tmp_mppmem;
			}
		} else {
			node_cpus = adjust_cpus_nppcu(
				nppcu, threads, node_cpus);

			/* On a reservation we can only run one job per node
			   on a cray so allocate all the cpus on each node
			   regardless of the request.
			*/
			mppwidth += node_cpus;

			/* We want mppnppn to be the smallest number of cpus
			   per node and allocate that on each of the nodes
			   reguardless of the request.
			*/
			mppnppn = MIN(mppnppn, node_cpus);

			if (node_min_mem) {
				/* Keep track of the largest cpu count and Min
				   memory if we need to split up the memory
				   per cpu.
				*/
				largest_cpus = MAX(largest_cpus, node_cpus);
				min_memory = MIN(min_memory, node_mem);
			}
		}
	}

	if (!cray_conf->sub_alloc && node_min_mem) {
		/*
		 * ALPS 'Processing Elements per Node' value (aprun -N),
		 * which in slurm is --ntasks-per-node and 'mppnppn' in
		 * PBS: if --ntasks is specified, default to the number
		 * of cores per node (also the default for 'aprun -N').
		 * On a heterogeneous system the nodes aren't
		 * always the same so keep track of the lowest
		 * mppmem and use it as the level for all
		 * nodes (mppmem is 0 when coming in).
		 */
		mppmem = MIN(min_memory, node_min_mem) / largest_cpus;

		/* Minimum memory per processing element should be 1,
		 * since 0 means give all the memory to the job. */
		if (mppmem <= 0)
			mppmem = 1;
	}

	if (cray_conf->sub_alloc) {
		int sock_core_inx = 0, sock_core_rep_cnt = 0;
		mppwidth = 0;
		/* mppwidth */
		for (i = 0; i < job_ptr->job_resrcs->nhosts; i++) {
			uint16_t hwthreads_per_core = 1;
			uint32_t node_tasks =
				job_ptr->job_resrcs->cpus[i] / mppdepth;

			if ((job_ptr->job_resrcs->
			     sockets_per_node[sock_core_inx] > 0) &&
			    (job_ptr->job_resrcs->
			     cores_per_socket[sock_core_inx] > 0)) {
				hwthreads_per_core =
					job_ptr->job_resrcs->cpus[i] /
					job_ptr->job_resrcs->
					sockets_per_node[sock_core_inx] /
					job_ptr->job_resrcs->
					cores_per_socket[sock_core_inx];
			}
			if ((++sock_core_rep_cnt) > job_ptr->job_resrcs->
			    sock_core_rep_count[sock_core_inx]) {
				/* move to the next node */
				sock_core_inx++;
				sock_core_rep_cnt = 0;
			}
			if (nppcu)
				node_tasks =
					node_tasks * nppcu / hwthreads_per_core;

			if (mppnppn && mppnppn < node_tasks)
				node_tasks = mppnppn;
			mppwidth += node_tasks;
		}
	}

	snprintf(batch_id, sizeof(batch_id), "%u", job_ptr->job_id);
	user = uid_to_string(job_ptr->user_id);

	if (job_ptr->gres_list)
		bap = build_accel_param(job_ptr);
	else
		bap = NULL;

	rc   = basil_reserve(user, batch_id, mppwidth, mppdepth, mppnppn,
			     mppmem, (uint32_t)nppcu, ns_head, bap);
	xfree(user);
	if (rc <= 0) {
		/* errno value will be resolved by select_g_job_begin() */
		errno = is_transient_error(rc) ? EAGAIN : ECONNABORTED;
		return SLURM_ERROR;
	}

	resv_id	= rc;
	if (_set_select_jobinfo(job_ptr->select_jobinfo->data,
			SELECT_JOBDATA_RESV_ID, &resv_id) != SLURM_SUCCESS) {
		/*
		 * This is a fatal error since it means we will not be able to
		 * confirm the reservation; no step will be able to run in it.
		 */
		error("job %u: can not set resId %u", job_ptr->job_id, resv_id);
		basil_release(resv_id);
		return SLURM_ERROR;
	}
	if (mppmem)
		job_ptr->details->pn_min_memory = mppmem | MEM_PER_CPU;

	info("ALPS RESERVATION #%u, JobId %u: BASIL -n %d -N %d -d %d -m %d",
	     resv_id, job_ptr->job_id, mppwidth, mppnppn, mppdepth, mppmem);

	return SLURM_SUCCESS;
}

/**
 * do_basil_confirm - confirm an existing BASIL reservation.
 * This requires the alloc_sid to equal the session ID (getsid()) of the process
 * executing the aprun/mpirun commands
 * Returns: SLURM_SUCCESS if ok, READY_JOB_ERROR/FATAL on transient/fatal error.
 */
extern int do_basil_confirm(struct job_record *job_ptr)
{
	uint8_t  confirmed;
	uint32_t resv_id;
	uint64_t pagg_id;

	if (_get_select_jobinfo(job_ptr->select_jobinfo->data,
			SELECT_JOBDATA_CONFIRMED, &confirmed)
			!= SLURM_SUCCESS) {
		error("can not read confirmed for JobId=%u", job_ptr->job_id);
	} else if (confirmed != 0) {
		debug2("ALPS reservation for JobId %u previously confirmed",
		       job_ptr->job_id);
		return SLURM_SUCCESS;
	}

	if (_get_select_jobinfo(job_ptr->select_jobinfo->data,
			SELECT_JOBDATA_RESV_ID, &resv_id) != SLURM_SUCCESS) {
		error("can not read resId for JobId=%u", job_ptr->job_id);
	} else if (resv_id == 0) {
		/* On Cray XT/XE, a reservation ID of 0 is always invalid. */
		error("JobId=%u has invalid (ZERO) resId", job_ptr->job_id);
	} else if (_get_select_jobinfo(job_ptr->select_jobinfo->data,
			SELECT_JOBDATA_PAGG_ID, &pagg_id) != SLURM_SUCCESS) {
		error("can not read pagg ID for JobId=%u", job_ptr->job_id);
	} else {
		int rc;

		if (pagg_id == 0) {
#ifdef HAVE_REAL_CRAY
			/* This fallback case is for interactive jobs only */
			error("JobId %u has no pagg ID, falling back to SID",
				job_ptr->job_id);
#endif
			pagg_id = job_ptr->alloc_sid;
		}

		rc = basil_confirm(resv_id, job_ptr->job_id, pagg_id);
		if (rc == 0) {
			debug2("confirmed ALPS resId %u for JobId %u, pagg "
			       "%"PRIu64"", resv_id, job_ptr->job_id, pagg_id);
			confirmed = 1;
			_set_select_jobinfo(job_ptr->select_jobinfo->data,
				SELECT_JOBDATA_CONFIRMED, &confirmed);
			return SLURM_SUCCESS;
		} else if (rc == -BE_NO_RESID) {
			/*
			 * If ALPS can not find the reservation ID we are trying
			 * to confirm, it may be that the job has already been
			 * canceled, or that the reservation has timed out after
			 * waiting for the confirmation.
			 * It is more likely that this error occurs on a per-job
			 * basis, hence in this case do not drain frontend node.
			 */
			error("JobId %u has invalid ALPS resId %u - job "
			      "already canceled?", job_ptr->job_id, resv_id);
			return SLURM_SUCCESS;
		} else if (is_transient_error(rc)) {
			debug("confirming ALPS resId %u of JobId %u FAILED: %s",
			      resv_id, job_ptr->job_id, basil_strerror(rc));
			return READY_JOB_ERROR;
		} else {
			error("confirming ALPS resId %u of JobId %u FAILED: %s",
			      resv_id, job_ptr->job_id, basil_strerror(rc));
		}
	}
	return READY_JOB_FATAL;
}

/**
 * do_basil_signal  -  pass job signal on to any APIDs
 * IN job_ptr - job to be signalled
 * IN signal  - signal(7) number
 * Only signal job if an ALPS reservation exists (non-0 reservation ID).
 */
extern int do_basil_signal(struct job_record *job_ptr, int signal)
{
	uint32_t resv_id;

	if (_get_select_jobinfo(job_ptr->select_jobinfo->data,
			SELECT_JOBDATA_RESV_ID, &resv_id) != SLURM_SUCCESS) {
		error("can not read resId for JobId=%u", job_ptr->job_id);
	} else if (resv_id != 0) {
		int rc = basil_signal_apids(resv_id, signal, NULL);

		if (rc)
			error("could not signal APIDs of resId %u: %s", resv_id,
				basil_strerror(rc));
	}
	return SLURM_SUCCESS;
}

void *_sig_basil(void *args)
{
	args_sig_basil_t *args_sig_basil = (args_sig_basil_t *) args;
	int rc;

	sleep(args_sig_basil->delay);
	rc = basil_signal_apids(args_sig_basil->resv_id,
				args_sig_basil->signal, NULL);
	if (rc) {
		error("could not signal APIDs of resId %u: %s",
		      args_sig_basil->resv_id, basil_strerror(rc));
	}
	xfree(args);
	return NULL;
}

/**
 * queue_basil_signal  -  queue job signal on to any APIDs
 * IN job_ptr - job to be signalled
 * IN signal  - signal(7) number
 * IN delay   - how long to delay the signal, in seconds
 * Only signal job if an ALPS reservation exists (non-0 reservation ID).
 */
extern void queue_basil_signal(struct job_record *job_ptr, int signal,
			       uint16_t delay)
{
	args_sig_basil_t *args_sig_basil;
	pthread_attr_t attr_sig_basil;
	pthread_t thread_sig_basil;
	uint32_t resv_id;

	if (_get_select_jobinfo(job_ptr->select_jobinfo->data,
			SELECT_JOBDATA_RESV_ID, &resv_id) != SLURM_SUCCESS) {
		error("can not read resId for JobId=%u", job_ptr->job_id);
		return;
	}
	if (resv_id == 0)
		return;
	if ((delay == 0) || (delay == (uint16_t) NO_VAL)) {
		/* Send the signal now */
		int rc = basil_signal_apids(resv_id, signal, NULL);

		if (rc)
			error("could not signal APIDs of resId %u: %s", resv_id,
			      basil_strerror(rc));
		return;
	}

	/* Create a thread to send the signal later */
	slurm_attr_init(&attr_sig_basil);
	if (pthread_attr_setdetachstate(&attr_sig_basil,
					PTHREAD_CREATE_DETACHED)) {
		error("pthread_attr_setdetachstate error %m");
		slurm_attr_destroy(&attr_sig_basil);
		return;
	}
	args_sig_basil = xmalloc(sizeof(args_sig_basil_t));
	args_sig_basil->resv_id = resv_id;
	args_sig_basil->signal  = signal;
	args_sig_basil->delay   = delay;
	if (pthread_create(&thread_sig_basil, &attr_sig_basil,
			_sig_basil, (void *) args_sig_basil)) {
		error("pthread_create error %m");
		slurm_attr_destroy(&attr_sig_basil);
		xfree(args_sig_basil);
		return;
	}
	slurm_attr_destroy(&attr_sig_basil);
}

/**
 * do_basil_release - release an (unconfirmed) BASIL reservation
 * IN job_ptr - pointer to job which has just been deallocated resources
 * RET see below
 */
extern int do_basil_release(struct job_record *job_ptr)
{
	uint32_t resv_id;

	if (_get_select_jobinfo(job_ptr->select_jobinfo->data,
			SELECT_JOBDATA_RESV_ID, &resv_id) != SLURM_SUCCESS) {
		error("can not read resId for JobId=%u", job_ptr->job_id);
	} else if (resv_id && basil_release(resv_id) == 0) {
		/* The resv_id is non-zero only if the job is or was running. */
		debug("released ALPS resId %u for JobId %u",
		      resv_id, job_ptr->job_id);
	}
	/*
	 * Error handling: we only print out the errors (basil_release does this
	 * internally), but do not signal error to select_g_job_fini(). Calling
	 * contexts of this function (deallocate_nodes, batch_finish) only print
	 * additional error text: no further action is taken at this stage.
	 */
	return SLURM_SUCCESS;
}

/**
 * do_basil_switch - suspend/resume BASIL reservation
 * IN job_ptr - pointer to job which has just been deallocated resources
 * IN suspend - to suspend or not to suspend
 * RET see below
 */
extern int do_basil_switch(struct job_record *job_ptr, bool suspend)
{
	uint32_t resv_id;

	if (_get_select_jobinfo(job_ptr->select_jobinfo->data,
			SELECT_JOBDATA_RESV_ID, &resv_id) != SLURM_SUCCESS) {
		error("can not read resId for JobId=%u", job_ptr->job_id);
	} else if (resv_id && basil_switch(resv_id, suspend) == 0) {
		/* The resv_id is non-zero only if the job is or was running. */
		debug("%s ALPS resId %u for JobId %u",
		      suspend ? "Suspended" : "Resumed",
		      resv_id, job_ptr->job_id);
	}
	return SLURM_SUCCESS;
}

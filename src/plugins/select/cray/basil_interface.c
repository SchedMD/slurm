/*
 * Interface between lower-level ALPS XML-RPC functions and SLURM.
 *
 * Copyright (c) 2010-11 Centro Svizzero di Calcolo Scientifico (CSCS)
 * Licensed under GPLv2.
 */
#include "basil_interface.h"
#include "basil_alps.h"

/*
 * Following routines are from src/plugins/select/bluegene/plugin/jobinfo.c
 */
static int _set_select_jobinfo(select_jobinfo_t *jobinfo,
			       enum select_jobdata_type data_type, void *data)
{
	uint32_t *uint32 = (uint32_t *) data;

	if (jobinfo == NULL) {
		error("cray/set_select_jobinfo: jobinfo not set");
		return SLURM_ERROR;
	}
	if (jobinfo->magic != JOBINFO_MAGIC) {
		error("cray/set_select_jobinfo: jobinfo magic bad");
		return SLURM_ERROR;
	}

	switch (data_type) {
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
	uint32_t *uint32 = (uint32_t *) data;

	if (jobinfo == NULL) {
		error("cray/get_select_jobinfo: jobinfo not set");
		return SLURM_ERROR;
	}
	if (jobinfo->magic != JOBINFO_MAGIC) {
		error("cray/get_select_jobinfo: jobinfo magic bad");
		return SLURM_ERROR;
	}

	switch (data_type) {
	case SELECT_JOBDATA_RESV_ID:
		*uint32 = jobinfo->reservation_id;
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

	inv = get_full_inventory(version);
	if (inv == NULL)
		/* FIXME: should retry here if the condition is transient */
		fatal("failed to get BASIL %s ranking", bv_names_long[version]);
	else if (!inv->batch_total)
		fatal("system has no usable batch compute nodes");

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

		node_ptr = _find_node_by_basil_id(node->node_id);
		if (node_ptr == NULL)
			error("nid%05u (%s node in state %s) not in slurm.conf",
			      node->node_id, nam_noderole[node->role],
			      nam_nodestate[node->state]);
		 else
			node_ptr->node_rank = inv->nodes_total - rank_count++;
	}
	free_inv(inv);

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
	int rc = SLURM_SUCCESS;

	inv = get_full_inventory(version);
	if (inv == NULL) {
		error("BASIL %s INVENTORY failed", bv_names_long[version]);
		return SLURM_ERROR;
	}

	debug("BASIL %s INVENTORY: %d/%d batch nodes available",
	      bv_names_long[version], inv->batch_avail, inv->batch_total);

	if (!inv->f->node_head || !inv->batch_avail || !inv->batch_total)
		rc = ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;

	for (node = inv->f->node_head; node; node = node->next) {
		struct node_record *node_ptr;
		char *reason = NULL;

		node_ptr = _find_node_by_basil_id(node->node_id);
		if (node_ptr == NULL) {
			error("nid%05u (%s node in state %s) not in slurm.conf",
			      node->node_id, nam_noderole[node->role],
			      nam_nodestate[node->state]);
			continue;
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

		if (reason) {
			xfree(node_ptr->reason);

			if (IS_NODE_DOWN(node_ptr)) {
				node_ptr->reason = xstrdup(reason);
			} else {
				debug("MARKING %s DOWN (%s)",
				      node_ptr->name, reason);
				set_node_down(node_ptr->name, reason);
			}
		} else if (IS_NODE_DOWN(node_ptr)) {
			xfree(node_ptr->reason);

			/* Reset state, make_node_idle figures out the rest */
			node_ptr->node_state &= NODE_STATE_FLAGS;
			node_ptr->node_state |= NODE_STATE_UNKNOWN;

			make_node_idle(node_ptr, NULL);
		}
	}

	/*
	 * Check that each ALPS reservation corresponds to a SLURM job.
	 * Purge orphaned reservations, which may result from stale or
	 * messed up system state, or are indicative of ALPS problems
	 * (stuck in pending cancel calls).
	 * Don't return an error code here, to encourage scheduling
	 * even while some of the resources have not yet been freed.
	 */
	for (rsvn = inv->f->rsvn_head; rsvn; rsvn = rsvn->next) {
		ListIterator job_iter = list_iterator_create(job_list);
		struct job_record *job_ptr;
		uint32_t resv_id;

		if (job_iter == NULL)
			fatal("list_iterator_create: malloc failure");

		while ((job_ptr = (struct job_record *)list_next(job_iter))) {

			if (_get_select_jobinfo(job_ptr->select_jobinfo->data,
						SELECT_JOBDATA_RESV_ID,
						&resv_id) == SLURM_SUCCESS
			    && resv_id == rsvn->rsvn_id)
				break;
		}
		list_iterator_destroy(job_iter);

		if (job_ptr == NULL) {
			error("orphaned ALPS reservation %u, trying to remove",
			      rsvn->rsvn_id);
			basil_release(rsvn->rsvn_id);
			/*
			 * ALPS will take some time, do not schedule now.
			 */
			rc = ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
		}
	}
	free_inv(inv);
	return rc;
}

/**
 * basil_get_initial_state - basil_inventory() variant for initialisation
 * The logic is identical, with the difference that this is called at the
 * start, before any bitmaps are set, and serves to check the node state
 * correctly: whether nodes are allocated or have been marked down.
 * Return: SLURM_SUCCESS if ok, non-zero on error.
 */
static int basil_get_initial_state(void)
{
	enum basil_version version = get_basil_version();
	struct basil_inventory *inv;
	struct basil_node *node;

	inv = get_full_inventory(version);
	if (inv == NULL) {
		error("BASIL %s INVENTORY failed", bv_names_long[version]);
		return SLURM_ERROR;
	}

	debug("BASIL %s INITIAL INVENTORY: %d/%d batch nodes available",
	      bv_names_long[version], inv->batch_avail, inv->batch_total);

	for (node = inv->f->node_head; node; node = node->next) {
		struct node_record *node_ptr;
		char *reason = NULL;

		node_ptr = _find_node_by_basil_id(node->node_id);
		if (node_ptr == NULL)
			continue;

		/* Base state entirely depends on ALPS */
		node_ptr->node_state &= NODE_STATE_FLAGS;
		xfree(node_ptr->reason);

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

		if (reason) {
			debug("Initial DOWN node %s - %s", node_ptr->name, reason);
			node_ptr->reason = xstrdup(reason);
			node_ptr->node_state |= NODE_STATE_DOWN;
		} else {
			/*
			 * Since this node is listed in the most recent ALPS
			 * inventory, it can not possibly be allocated to a job
			 */
			node_ptr->node_state |= NODE_STATE_IDLE;
		}
	}
	free_inv(inv);
	return SLURM_SUCCESS;
}

/**
 * basil_geometry - Verify node attributes, resolve (X,Y,Z) coordinates.
 */
extern int basil_geometry(struct node_record *node_ptr_array, int node_cnt)
{
	struct node_record *node_ptr, *end = node_ptr_array + node_cnt;

	/* General */
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
	const char query[] =	"SELECT x_coord, y_coord, z_coord, cage, cpu, "
				"	LOG2(coremask+1), availmem, "
				"       processor_type  "
				"FROM  processor LEFT JOIN attributes "
				"ON    processor_id = nodeid "
				"WHERE processor_id = ? ";
	const int	PARAM_COUNT = 1;	/* node id */
	MYSQL_BIND	params[PARAM_COUNT];
	/* Output parameters */
	enum query_columns {
			/* integer data */
			COL_X,		/* X coordinate		*/
			COL_Y,		/* Y coordinate		*/
			COL_Z,		/* Z coordinate		*/
			COL_CAGE,	/* cage number (0..2)		*/
			COL_CPU,	/* node number (0..2)		*/
			COL_CORES,	/* number of cores per node	*/
			COL_MEMORY,	/* rounded-down memory in MB	*/
			/* string data */
			COL_TYPE,	/* {service, compute }		*/
			COLUMN_COUNT	/* sentinel */
	};
	int		x_coord, y_coord, z_coord;
	int		cage, cpu;
	unsigned int	node_cpus, node_mem;
	char		proc_type[BASIL_STRING_SHORT];
	MYSQL_BIND	bind_cols[COLUMN_COUNT];
	my_bool		is_null[COLUMN_COUNT];
	my_bool		is_error[COLUMN_COUNT];
	int		is_gemini, i;

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
			bind_cols[i].is_unsigned   = (i >= COL_CORES);
		}
	}
	bind_cols[COL_X].buffer	     = (char *)&x_coord;
	bind_cols[COL_Y].buffer	     = (char *)&y_coord;
	bind_cols[COL_Z].buffer	     = (char *)&z_coord;
	bind_cols[COL_CAGE].buffer   = (char *)&cage;
	bind_cols[COL_CPU].buffer    = (char *)&cpu;
	bind_cols[COL_CORES].buffer  = (char *)&node_cpus;
	bind_cols[COL_MEMORY].buffer = (char *)&node_mem;

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

		if (sscanf(node_ptr->name, "nid%05u", &node_id) != 1) {
			error("can not read basil_node_id from %s", node_ptr->name);
			continue;
		}
		if (exec_stmt(stmt, query, bind_cols, COLUMN_COUNT) < 0)
			fatal("can not resolve %s coordinates", node_ptr->name);

		if (mysql_stmt_fetch(stmt) == 0) {

			if (strcmp(proc_type, "compute") != 0) {
				/*
				 * Switching a compute node to be a service node
				 * can not happen at runtime: requires a reboot.
				 */
				fatal("Node '%s' is a %s node. "
				      "Only compute nodes can appear in slurm.conf.",
					node_ptr->name, proc_type);
			} else if (is_null[COL_CORES] || is_null[COL_MEMORY]) {
				/*
				 * This can happen if a node has been disabled
				 * on the SMW (using 'xtcli disable <nid>'). The
				 * node will still be listed in the 'processor'
				 * table, but have no 'attributes' entry (NULL
				 * values for CPUs/memory). Also, the node will
				 * be invisible to ALPS, which is why we need to
				 * set it down here already.
				 */
				node_ptr->node_state = NODE_STATE_DOWN;
				xfree(node_ptr->reason);
				node_ptr->reason = xstrdup("node data unknown -"
							   " disabled on SMW?");
				error("%s: %s", node_ptr->name, node_ptr->reason);
			} else if (node_cpus < node_ptr->config_ptr->cpus) {
				/*
				 * FIXME: Might reconsider this policy.
				 *
				 * FastSchedule is ignored here, it requires the
				 * slurm.conf to be consistent with hardware.
				 *
				 * Assumption is that CPU/Memory do not change
				 * at runtime (Cray has no hot-swappable parts).
				 *
				 * Hence checking it in basil_inventory() would
				 * mean a lot of runtime overhead.
				 */
				fatal("slurm.conf: node %s has only Procs=%d",
					node_ptr->name, node_cpus);
			} else if (node_mem < node_ptr->config_ptr->real_memory) {
				fatal("slurm.conf: node %s has RealMemory=%d",
					node_ptr->name, node_mem);
			}

		} else if (is_gemini) {
			/*
			 * XE: node IDs are consecutive, hence not being
			 * able to resolve the node ID is a (fatal) error.
			 */
			fatal("Non-existing Gemini node '%s' in slurm.conf",
			      node_ptr->name);
		} else {
			/*
			 * XT: node IDs are not consecutive. We don't want those
			 * holes to appear in slurm.conf - configuration error.
			 */
			fatal("Non-existing SeaStar node '%s' in slurm.conf",
			      node_ptr->name);
		}

		if (!is_gemini) {
				/*
				 * SeaStar: (X,Y,Z) resolve directly
				 */
				if (node_ptr->arch == NULL)
					node_ptr->arch = xstrdup("XT");
		} else {
				/*
				 * Gemini: each 2 nodes share the same network
				 * interface (i.e,. nodes 0/1 and 2/3 each have
				 * the same coordinates). Use cage and cpu to
				 * create corresponding "virtual" Y coordinate.
				 */
				y_coord = 4 * cage + cpu;

				if (node_ptr->arch == NULL)
					node_ptr->arch = xstrdup("XE");
		}

		mysql_stmt_free_result(stmt);
	}

	if (mysql_stmt_close(stmt))
		error("error closing statement: %s", mysql_stmt_error(stmt));
	mysql_close(handle);

	return basil_get_initial_state();
}

/**
 * do_basil_reserve - create a BASIL reservation.
 * IN job_ptr - pointer to job which has just been allocated resources
 * RET 0 or error code, job will abort or be requeued on failure
 */
extern int do_basil_reserve(struct job_record *job_ptr)
{
	struct nodespec *ns_head = NULL;
	uint16_t mppwidth = 0, mppdepth, mppnppn;
	uint32_t mppmem = 0, node_min_mem = 0;
	uint32_t resv_id;
	int i, first_bit, last_bit;
	hostlist_t hl;
	long rc;
	char *user, batch_id[16];

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

	mppdepth = MAX(1, job_ptr->details->cpus_per_task);
	mppnppn  = job_ptr->details->ntasks_per_node;

	/* mppmem */
	if (job_ptr->details->pn_min_memory & MEM_PER_CPU) {
		/* Only honour --mem-per-cpu if --ntasks has been given */
		if (job_ptr->details->num_tasks)
			mppmem = job_ptr->details->pn_min_memory & ~MEM_PER_CPU;
	} else if (job_ptr->details->pn_min_memory) {
		node_min_mem = job_ptr->details->pn_min_memory;
	}

	hl = hostlist_create("");
	if (hl == NULL)
		fatal("hostlist_create: malloc error");

	for (i = first_bit; i <= last_bit; i++) {
		struct node_record *node_ptr = node_record_table_ptr + i;
		uint32_t basil_node_id;

		if (!bit_test(job_ptr->job_resrcs->node_bitmap, i))
			continue;

		if (!node_ptr->name)
			continue;
		if (sscanf(node_ptr->name, "nid%05u", &basil_node_id) != 1)
			fatal("can not read basil_node_id from %s", node_ptr->name);

		if (ns_add_node(&ns_head, basil_node_id) != 0) {
			error("can not add node %s (nid%05u)", node_ptr->name, basil_node_id);
			free_nodespec(ns_head);
			return SLURM_ERROR;
		}

		if (node_min_mem) {
			uint32_t node_cpus, node_mem;

			if (slurmctld_conf.fast_schedule) {
				node_cpus = node_ptr->config_ptr->cpus;
				node_mem  = node_ptr->config_ptr->real_memory;
			} else {
				node_cpus = node_ptr->cpus;
				node_mem  = node_ptr->real_memory;
			}
			/*
			 * ALPS 'Processing Elements per Node' value (aprun -N),
			 * which in slurm is --ntasks-per-node and 'mppnppn' in
			 * PBS: if --ntasks is specified, default to the number
			 * of cores per node (also the default for 'aprun -N').
			 */
			node_mem /= mppnppn ? mppnppn : node_cpus;

			mppmem = node_min_mem = MIN(node_mem, node_min_mem);
		}
	}

	/* mppwidth */
	for (i = 0; i < job_ptr->job_resrcs->nhosts; i++) {
		uint16_t node_tasks = job_ptr->job_resrcs->cpus[i] / mppdepth;

		if (mppnppn && mppnppn < node_tasks)
			node_tasks = mppnppn;
		mppwidth += node_tasks;
	}

	snprintf(batch_id, sizeof(batch_id), "%u", job_ptr->job_id);
	user = uid_to_string(job_ptr->user_id);
	rc   = basil_reserve(user, batch_id, mppwidth,
			     mppdepth, mppnppn, mppmem, ns_head);
	xfree(user);
	if (rc <= 0) {
		/* errno value will be resolved by select_g_job_begin() */
		errno = is_transient_error(rc) ? EAGAIN : ECONNABORTED;
		return SLURM_ERROR;
	}

	resv_id	= rc;
	_set_select_jobinfo(job_ptr->select_jobinfo->data,
			    SELECT_JOBDATA_RESV_ID, &resv_id);

	info("ALPS RESERVATION #%u, JobId %u: BASIL -n %d -N %d -d %d -m %d",
	     resv_id, job_ptr->job_id, mppwidth, mppnppn, mppdepth, mppmem);

	return SLURM_SUCCESS;
}

/**
 * do_basil_confirm - confirm an existing BASIL reservation.
 * This requires the alloc_sid to equal the session ID (getsid()) of the process
 * executing the aprun/mpirun commands
 */
extern int do_basil_confirm(struct job_record *job_ptr)
{
	uint32_t resv_id;

	if (_get_select_jobinfo(job_ptr->select_jobinfo->data,
		       SELECT_JOBDATA_RESV_ID, &resv_id) != SLURM_SUCCESS)
		return SLURM_ERROR;

	debug2("confirming ALPS resId %u for JobId %u with pagg %u", resv_id,
		job_ptr->job_id, job_ptr->alloc_sid);

	return basil_confirm(resv_id, job_ptr->job_id, job_ptr->alloc_sid);
}

/**
 * do_basil_release - release an (unconfirmed) BASIL reservation
 * IN job_ptr - pointer to job which has just been deallocated resources
 * RET 0 or error code
 */
extern int do_basil_release(struct job_record *job_ptr)
{
	uint32_t resv_id;

	if (_get_select_jobinfo(job_ptr->select_jobinfo->data,
		       SELECT_JOBDATA_RESV_ID, &resv_id) != SLURM_SUCCESS)
		return SLURM_ERROR;

	if (basil_release(resv_id) < 0)
		return SLURM_ERROR;

	debug("released ALPS resId %u for JobId %u", resv_id, job_ptr->job_id);

	return SLURM_SUCCESS;
}

/*
 * Access to ALPS QUERY methods
 *
 * Copyright (c) 2009-2011 Centro Svizzero di Calcolo Scientifico (CSCS)
 * Licensed under the GPLv2.
 */
#include "../basil_alps.h"
#include "parser_internal.h"

/**
 * _get_alps_engine  -  run QUERY of type ENGINE
 * This uses the convention of returning the Engine.version attribute via 'msg'.
 * Returns pointer to @buf, NULL on error.
 */
static const char *_get_alps_engine(char *buf, size_t buflen)
{
	struct basil_parse_data bp = {0};

	if (cray_conf->alps_engine) {
		strncpy(buf, cray_conf->alps_engine, buflen);
		return buf;
	}

	/* For this query use Basil 1.0 as lowest common denominator */
	bp.version = BV_1_0;
	bp.method  = BM_engine;

	if (basil_request(&bp) < 0)
		return NULL;
	strncpy(buf, bp.msg, buflen);
	return buf;
}

/** Return true if @seg has at least a processor or a memory allocation. */
static bool _segment_is_allocated(const struct basil_segment *seg)
{
	struct basil_node_processor *proc;
	struct basil_node_memory *mem;

	for (proc = seg->proc_head; proc; proc = proc->next)
		if (proc->rsvn_id)
			return true;
	for (mem = seg->mem_head; mem; mem = mem->next)
		if (mem->a_head != NULL)
			return true;
	return false;
}

/**
 * get_basil_version  -  Detect highest BASIL version supported by ALPS.
 *
 * This uses the following correspondence table to find the highest supported
 * BASIL version. Failing that, it falls back to Basil 1.0 as last resort.
 *
 * +------------+---------------+------+----------------+----------------------+
 * | CLE release| Engine.version| ALPS | Basil Protocol |       Remarks        |
 * +------------+---------------+------+----------------+----------------------+
 * |  <= 2.2.48B|         1.1.0 |  1.1 |   1.0, 1.1     | see below            |
 * |  >= 2.2.67 |         1.2.0 |  1.2 |   1.0, 1.1     | last CLE 2.2 update  |
 * |     3.0    |         1.3.0 |  3.0 |   1.0, 1.1     | Cray ticket #762417  |
 * |     3.1    |         3.1.0 |  3.1 |   1.0, 1.1     | Cray ticket #762035  |
 * |     4.0    |         4.0.0 |  4.0 |   1.0,1.1,1.2  | starts GPU support   |
 * +------------+---------------+------+----------------+----------------------+
 *
 * The 'ALPS' column shows the name of the ALPS engine; the 'Basil Protocol'
 * column shows the supported versions for the BasilRequest.protocol attribute.
 *
 * No CLE 2 versions were released between 2.2.48B and 2.2.67; the Basil 1.2
 * variant that came with the latter release behaved identically to Basil 1.1.
 *
 * Starting from Basil 3.1, there is also a 'basil_support' attribute to query
 * the supported 'Basil Protocol' list.
 */
extern enum basil_version get_basil_version(void)
{
	char engine_version[BASIL_STRING_LONG];
	static enum basil_version bv = BV_MAX;

	if (bv != BV_MAX)
		return bv;

	if (_get_alps_engine(engine_version, sizeof(engine_version)) == NULL)
		fatal("can not determine ALPS Engine version");
	else if (strncmp(engine_version, "latest", 6) == 0) {
		bv = BV_5_2_3;
	} else if (strncmp(engine_version, "5.2", 3) == 0) {
		int macro = atoi(engine_version+4);
		if (macro >= 3) /* means 5.2.44+ */
			bv = BV_5_2_3;
		else
			bv = BV_5_2;
	} else if (strncmp(engine_version, "5.1", 3) == 0)
		bv = BV_5_1;
	else if (strncmp(engine_version, "5.0", 3) == 0)
		bv = BV_5_0;
	else if (strncmp(engine_version, "4.2.0", 5) == 0)
		bv = BV_4_1;
	else if (strncmp(engine_version, "4.1.0", 5) == 0)
		bv = BV_4_1;
	else if (strncmp(engine_version, "4.0", 3) == 0)
		bv = BV_4_0;
	else if (strncmp(engine_version, "3.1.0", 5) == 0)
		bv = BV_3_1;
	else if (strncmp(engine_version, "1.3.0", 5) == 0)
		/*
		 * Cray Bug#762417 - strictly speaking, we should be
		 * returning BV_3_0 here. Alps Engine Version 1.3.0
		 * is reserved for the Cozla release (CLE 3.0), which
		 * however was only a short time on the market.
		 */
		bv = BV_3_1;
	else if (strncmp(engine_version, "1.2.0", 5) == 0)
		bv = BV_1_2;
	else if (strncmp(engine_version, "1.1", 3) == 0)
		bv = BV_1_1;
	else
		fatal("unsupported ALPS Engine version '%s', please edit "
		      "src/plugins/select/cray/libalps/do_query.c "
		      "for this version",
		      engine_version);

	if (bv == BV_5_2_3) {
		/* Starting in 5.2.UP03 (5.2.44) things changed, so
		   make it that way */
		basil_5_2_elements[BT_MEMARRAY].depth = 9;
		basil_5_2_elements[BT_MEMORY].depth = 10;
		basil_5_2_elements[BT_MEMALLOC].depth = 11;
	}

	return bv;
}

/** Perform a detailed inventory */
extern struct basil_inventory *get_full_inventory(enum basil_version version)
{
	struct basil_parse_data bp = {0};

	bp.version   = version;
	bp.method    = BM_inventory;
	bp.mdata.inv = xmalloc(sizeof(*bp.mdata.inv));

	if (bp.mdata.inv) {
		bp.mdata.inv->f = xmalloc(sizeof(struct basil_full_inventory));
		if (bp.mdata.inv->f == NULL) {
			xfree(bp.mdata.inv);
			bp.mdata.inv = NULL;
		}
	}

	if (bp.mdata.inv == NULL)
		return NULL;

	if (basil_request(&bp) < 0) {
		free_inv(bp.mdata.inv);
		return NULL;
	}

	return bp.mdata.inv;
}

/*
 *	Informations extracted from INVENTORY
 */

/** Return true if @node has at least a processor or a memory allocation. */
extern bool node_is_allocated(const struct basil_node *node)
{
	struct basil_segment *seg;

	for (seg = node->seg_head; seg; seg = seg->next)
		if (_segment_is_allocated(seg))
			return true;
	return false;
}

/** Search @inv for a particular reservation identified by @rsvn_id */
extern const struct basil_rsvn *basil_rsvn_by_id(
	const struct basil_inventory *inv, uint32_t rsvn_id)
{
	const struct basil_rsvn *rsvn;

	assert(inv && inv->f);
	for (rsvn = inv->f->rsvn_head; rsvn; rsvn = rsvn->next)
		if (rsvn->rsvn_id == rsvn_id)
			break;
	return rsvn;
}

/**
 * basil_get_rsvn_aprun_apids  -  get list of aprun APIDs for @rsvn_id
 * Returns 0-terminated array, which caller must free.
 * WARNING: if the aprun application uses fewer nodes than are reserved by
 *          @rsvn_id, additional information is required to confirm whether
 *          that particular node is indeed in use by the given apid.
 */
extern uint64_t *basil_get_rsvn_aprun_apids(const struct basil_inventory *inv,
					    uint32_t rsvn_id)
{
	const struct basil_rsvn	*rsvn = basil_rsvn_by_id(inv, rsvn_id);
	const struct basil_rsvn_app *app;
	uint64_t *apids = NULL;
	int n = 1;	/* 0-terminated array */

	if (rsvn == NULL)
		return NULL;

	for (app = rsvn->app_head; app; app = app->next)
		/*
		 * There are two types of basil_rsvn_app applications:
		 * - the first application has a 'timestamp' of 0, a 'cmd' of
		 *   "BASIL" - this is used to store the reservation parameters;
		 * - all other applications have a non-0 timestamp and refer to
		 *   actual aprun job steps (whose APIDs we are interested in).
		 */
		if (app->timestamp) {
			apids = xrealloc(apids, (n + 1) * sizeof(*apids));
			if (apids == NULL)
				fatal("failed to allocate Apid entry");
			apids[n-1] = app->apid;
			apids[n++] = 0;
		}
	return apids;
}

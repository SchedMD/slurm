/*
 * Access to ALPS QUERY methods
 *
 * Copyright (c) 2009-2011 Centro Svizzero di Calcolo Scientifico (CSCS)
 * Licensed under the GPLv2.
 */
#include "../basil_alps.h"

/**
 * _get_alps_engine  -  run QUERY of type ENGINE
 * This uses the convention of returning the Engine.version attribute via 'msg'.
 * Returns static result, NULL on error.
 */
static const char *_get_alps_engine(void)
{
	struct basil_parse_data bp = {0};
	static char alps_engine[64];

	/* For this query use Basil 1.0 as lowest common denominator */
	bp.version = BV_1_0;
	bp.method  = BM_engine;

	if (basil_request(&bp) < 0)
		return NULL;
	strncpy(alps_engine, bp.msg, sizeof(alps_engine));
	return alps_engine;
}

/**
 * get_basil_version  -  Detect highest BASIL version supported by ALPS.
 *
 * This uses the following correspondence table to find the highest supported
 * ALPS version. Failing that, it falls back to Basil 1.0 as last resort.
 *
 * +------------+---------------+------+----------------+----------------------+
 * | CLE release| Engine.version| ALPS | Basil Protocol |       Remarks        |
 * +------------+---------------+------+----------------+----------------------+
 * |  <= 2.2.48B|         1.1.0 |  1.1 |   1.0, 1.1     | see below            |
 * |  >= 2.2.67 |         1.2.0 |  1.2 |   1.0, 1.1     | last CLE 2.2 update  |
 * |     3.0    |         1.3.0 |  3.0 |   1.0, 1.1     | Cray ticket #762417  |
 * |     3.1    |         3.1.0 |  3.1 |   1.0, 1.1     | Cray ticket #762035  |
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
 *
 * However, we can not presuppose this version and thus use Basil 1.0 as lowest
 * common denominator to obtain the name of the ALPS engine.
 */
enum basil_version get_basil_version(void)
{
	const char *engine_version = _get_alps_engine();

	if (engine_version == NULL)
		error("can not determine ALPS Engine.version");
	else if (strncmp(engine_version, "3.1.0", 5) == 0)
		return BV_3_1;
	else if (strncmp(engine_version, "1.3.0", 5) == 0)
		/*
		 * Cray Bug#762417 - strictly speaking, we should be
		 * returning BV_3_0 here. Alps Engine Version 1.3.0
		 * is reserved for the Cozla release (CLE 3.0), which
		 * however was only a short time on the market.
		 */
		return BV_3_1;
	else if (strncmp(engine_version, "1.2.0", 5) == 0)
		return BV_1_2;
	else if (strncmp(engine_version, "1.1", 3) == 0)
		return BV_1_1;
	else
		error("falling back to BASIL 1.0");
	return BV_1_0;
}

static struct basil_inventory *alloc_inv(bool do_full_inventory)
{
	struct basil_inventory *inv = calloc(1, sizeof(*inv));

	if (inv != NULL && do_full_inventory) {
		inv->f = calloc(1, sizeof(struct basil_full_inventory));
		if (inv->f == NULL) {
			free(inv);
			inv = NULL;
		}
	}
	return inv;
}

/**
 * get_inventory  -  generic INVENTORY request
 * Caller must free result structure by calling free_inv().
 */
static struct basil_inventory *get_inventory(enum basil_version version,
					     bool do_full_inventory)
{
	struct basil_parse_data bp = {0};

	bp.version   = version;
	bp.method    = BM_inventory;
	bp.mdata.inv = alloc_inv(do_full_inventory);

	if (bp.mdata.inv == NULL)
		return NULL;

	if (basil_request(&bp) < 0) {
		free_inv(bp.mdata.inv);
		return NULL;
	}

	return bp.mdata.inv;
}

/** Perform a detailed inventory */
struct basil_inventory *get_full_inventory(enum basil_version version)
{
	return get_inventory(version, true);
}

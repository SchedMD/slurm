/*
 * Implements the Basil RELEASE method for partition reservations.
 *
 * Copyright (c) 2009-2011 Centro Svizzero di Calcolo Scientifico (CSCS)
 * Licensed under the GPLv2.
 */
#include "../basil_alps.h"

static int rsvn_release(struct basil_reservation *res)
{
	struct basil_parse_data bp = {0};

	bp.method    = BM_release;
	bp.mdata.res = res;
	bp.version   = get_basil_version();
	/* NOTE - for simplicity we could use BV_1_0 here */

	return basil_request(&bp);
}

/**
 * basil_release - release an (un)confirmed reservation
 * @rsvn_id:    the reservation id
 * Returns 0 if ok, a negative %basil_error otherwise.
 */
int basil_release(uint32_t rsvn_id)
{
	struct basil_reservation rsvn = {0};

	rsvn.rsvn_id = rsvn_id;
	return rsvn_release(&rsvn);
}

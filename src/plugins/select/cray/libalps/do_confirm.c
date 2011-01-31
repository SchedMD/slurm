/*
 * Implements the Basil CONFIRM method for partition reservations.
 *
 * Copyright (c) 2009-2011 Centro Svizzero di Calcolo Scientifico (CSCS)
 * Licensed under the GPLv2.
 */
#include "../basil_alps.h"

static int rsvn_confirm(struct basil_reservation *res, uint64_t pagg_id)
{
	struct basil_parse_data bp = {0};

	bp.method    = BM_confirm;
	res->pagg_id = pagg_id;
	bp.mdata.res = res;
	bp.version   = BV_1_0;
	/*
	 * Rule:
	 * - if *res->batch_id == '\0' we are not using Basil 1.0
	 * - else we use Basil 1.0 to set the 'job_name'
	 */
	if (*res->batch_id == '\0')
		bp.version = get_basil_version();

	return basil_request(&bp);
}

/**
 * basil_confirm - confirm an existing reservation
 * @rsvn_id:    the reservation id
 * @job_id:	job ID or -1 (see note below)
 * @pagg_id:	SID or CSA PAGG ID of the shell process executing the job script
 * Returns 0 if ok, a negative %basil_error otherwise.
 *
 * NOTE: @job_id is only meaningful for confirmation of Basil 1.0 jobs.
 *       Basil 1.1 jobs can register the batch_id when creating the reservation.
 */
int basil_confirm(uint32_t rsvn_id, int job_id, uint64_t pagg_id)
{
	struct basil_reservation rsvn = {0};

	rsvn.rsvn_id = rsvn_id;
	if (job_id >= 0)
		snprintf(rsvn.batch_id, sizeof(rsvn.batch_id), "%u", job_id);
	return rsvn_confirm(&rsvn, pagg_id);
}

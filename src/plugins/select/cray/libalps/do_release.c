/*
 * Implements the Basil RELEASE method for partition reservations.
 *
 * Copyright (c) 2009-2011 Centro Svizzero di Calcolo Scientifico (CSCS)
 * Licensed under the GPLv2.
 */
#include "../basil_alps.h"

/* Location of Cray apkill executable (supported on XT/XE CNL) */
static const char apkill[] = HAVE_ALPS_DIR "/bin/apkill";

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

/**
 * basil_safe_release  -  release a reservation after performing sanity checks
 * @rsvn_id:	reservation ID of reservation to release
 * @inv:	recent Basil Inventory, or NULL to generate internally
 * Returns 0 if ok, a negative %basil_error otherwise.
 */
int basil_safe_release(int32_t rsvn_id, struct basil_inventory *inv)
{
	struct basil_inventory *new_inv = inv;
	int i, rc = BE_NONE;

	if (inv == NULL)
		new_inv = get_full_inventory(get_basil_version());

	if (new_inv == NULL) {
		error("can not obtain a BASIL inventory to check APIDs");
		rc = BE_INTERNAL | BE_TRANSIENT;
	} else if (access(apkill, X_OK) < 0) {
		error("FATAL: can not execute the apkill command '%s'", apkill);
		rc = BE_SYSTEM;
	} else {
		/*
		 * Before issuing the Basil RELEASE command, check if there are
		 * still any live application IDs (APIDs) associated with the
		 * reservation. If yes, trying to release the reservation will
		 * not succeed, ALPS will hold on to it until all applications
		 * have terminated, i.e. the RELEASE will be without effect. To
		 * avoid such failure, try to force-terminate the APIDs using
		 * the Cray apkill(1) binary. This should normally only occur if
		 * job steps have not terminated cleanly, e.g. a crashed salloc
		 * session.
		 */
		uint64_t *apids = basil_get_rsvn_aprun_apids(new_inv, rsvn_id);
		char cmd[512];

		if (apids)
			for (i = 0; apids[i]; i++) {
				error("apkill live apid %llu of ALPS resId %u",
					 (unsigned long long)apids[i], rsvn_id);
				snprintf(cmd, sizeof(cmd), "%s %llu", apkill,
					 (unsigned long long)apids[i]);
				if (system(cmd) < 0)
					error("system(%s) failed", cmd);
			}
		free(apids);
		rc = basil_release(rsvn_id);
	}
	if (inv == NULL)
		free_inv(new_inv);
	return rc ? -rc : 0;
}

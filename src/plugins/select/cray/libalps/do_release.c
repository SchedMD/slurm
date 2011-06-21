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

/**
 * basil_signal_apids  -  send a signal to all APIDs of a given ALPS reservation
 * @rsvn_id:	reservation ID to target
 * @signal:	signal number
 * @inv:	recent Basil Inventory, or NULL to generate internally
 * Returns 0 if ok, a negative %basil_error otherwise.
 */
int basil_signal_apids(int32_t rsvn_id, int signal, struct basil_inventory *inv)
{
	struct basil_inventory *new_inv = inv;
	uint64_t *apid, *apids;
	char cmd[512];

	if (access(cray_conf->apkill, X_OK) < 0) {
		error("FATAL: can not execute the apkill command '%s'",
		      cray_conf->apkill);
		return -BE_SYSTEM;
	}

	if (inv == NULL)
		new_inv = get_full_inventory(get_basil_version());
	if (new_inv == NULL) {
		error("can not obtain a BASIL inventory to get APID list");
		return -(BE_INTERNAL | BE_TRANSIENT);
	}

	apids = basil_get_rsvn_aprun_apids(new_inv, rsvn_id);
	if (apids) {
		for (apid = apids; *apid; apid++) {
			debug2("ALPS resId %u, running apkill -%d %llu",
				rsvn_id, signal, (unsigned long long)*apid);
			snprintf(cmd, sizeof(cmd), "%s -%d %llu",
				 cray_conf->apkill, signal,
				 (unsigned long long)*apid);
			if (system(cmd) < 0)
				error("system(%s) failed", cmd);
		}
		xfree(apids);
	}
	if (inv == NULL)
		free_inv(new_inv);
	return BE_NONE;
}

/**
 * basil_safe_release  -  release reservation after signalling job steps
 * @rsvn_id:	reservation to release
 * @inv:	recent Basil Inventory, or NULL to generate internally
 * Returns 0 if ok, a negative %basil_error otherwise.
 */
int basil_safe_release(int32_t rsvn_id, struct basil_inventory *inv)
{
	int rc = basil_release(rsvn_id);
	/*
	 * If there are still any live application IDs (APIDs) associated with
	 * @rsvn_id, the RELEASE command will be without effect, since ALPS
	 * holds on to a reservation until all of its application IDs have
	 * disappeared.
	 * On normal termination, ALPS should clean up the APIDs by itself. In
	 * order to clean up orphaned reservations, try to terminate the APIDs
	 * manually using apkill(1). If this step fails, fall back to releasing
	 * the reservation normally and hope that ALPS resolves the situation.
	 * To prevent that any subsequent aprun lines get started while the
	 * apkill of the current one is still in progress, do the RELEASE first.
	 */
	basil_signal_apids(rsvn_id, SIGKILL, inv);
	return rc;
}

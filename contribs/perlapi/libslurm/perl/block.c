/*
 * node.c - convert data between node related messages and perl HVs
 */

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>
#include "ppport.h"

#include <slurm/slurm.h>
#include "slurm-perl.h"


/*
 * convert block_info_t to perl HV
 */
int
block_info_to_hv(block_info_t *block_info, HV *hv)
{
	if(block_info->bg_block_id)
		STORE_FIELD(hv, block_info, bg_block_id, charp);
	if(block_info->blrtsimage)
		STORE_FIELD(hv, block_info, blrtsimage, charp);
	if (block_info->bp_inx) {
		int j;
		AV* av = newAV();
		for(j = 0; ; j += 2) {
			if(block_info->bp_inx[j] == -1)
				break;
			av_store(av, j, newSVuv(block_info->bp_inx[j]));
			av_store(av, j+1, newSVuv(block_info->bp_inx[j+1]));
		}
		hv_store_sv(hv, "bp_inx", newRV_noinc((SV*)av));
	}
	STORE_FIELD(hv, block_info, conn_type, uint16_t);
	if(block_info->ionodes)
		STORE_FIELD(hv, block_info, ionodes, charp);
	if (block_info->ionode_inx) {
		int j;
		AV* av = newAV();
		for(j = 0; ; j += 2) {
			if(block_info->ionode_inx[j] == -1)
				break;
			av_store(av, j, newSVuv(block_info->ionode_inx[j]));
			av_store(av, j+1, newSVuv(block_info->ionode_inx[j+1]));
		}
		hv_store_sv(hv, "ionode_inx", newRV_noinc((SV*)av));
	}
	STORE_FIELD(hv, block_info, job_running, uint32_t);
	if(block_info->linuximage)
		STORE_FIELD(hv, block_info, linuximage, charp);
	if(block_info->mloaderimage)
		STORE_FIELD(hv, block_info, mloaderimage, charp);
	if(block_info->nodes)
		STORE_FIELD(hv, block_info, nodes, charp);
	STORE_FIELD(hv, block_info, node_cnt, uint32_t);
	STORE_FIELD(hv, block_info, node_use, uint16_t);
	if (block_info->owner_name)
		STORE_FIELD(hv, block_info, owner_name, charp);
	if(block_info->ramdiskimage)
		STORE_FIELD(hv, block_info, ramdiskimage, charp);
	if(block_info->reason)
		STORE_FIELD(hv, block_info, reason, charp);
	STORE_FIELD(hv, block_info, state, uint16_t);
	return 0;
}

/*
 * convert perl HV to block_info_t
 */
int
hv_to_block_info(HV *hv, block_info_t *block_info)
{
	SV **svp;
	AV *av;
	int i, n;

	memset(block_info, 0, sizeof(block_info_t));

	FETCH_FIELD(hv, block_info, bg_block_id, charp, FALSE);
	FETCH_FIELD(hv, block_info, blrtsimage, charp, FALSE);
	svp = hv_fetch(hv, "bp_inx", 6, FALSE);
	if (svp && SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		av = (AV*)SvRV(*svp);
		n = av_len(av) + 2; /* for trailing -1 */
		block_info->bp_inx = xmalloc(n * sizeof(int));
		for (i = 0 ; i < n-1; i += 2) {
			block_info->bp_inx[i] = (int)SvIV(*(av_fetch(av, i, FALSE)));
			block_info->bp_inx[i+1] = (int)SvIV(*(av_fetch(av, i+1 ,FALSE)));
		}
		block_info->bp_inx[n-1] = -1;
	} else {
		/* nothing to do */
	}
	FETCH_FIELD(hv, block_info, conn_type, uint16_t, TRUE);
	FETCH_FIELD(hv, block_info, ionodes, charp, FALSE);
	svp = hv_fetch(hv, "ionode_inx", 10, FALSE);
	if (svp && SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		av = (AV*)SvRV(*svp);
		n = av_len(av) + 2; /* for trailing -1 */
		block_info->ionode_inx = xmalloc(n * sizeof(int));
		for (i = 0 ; i < n-1; i += 2) {
			block_info->ionode_inx[i] = (int)SvIV(*(av_fetch(av, i, FALSE)));
			block_info->ionode_inx[i+1] = (int)SvIV(*(av_fetch(av, i+1 ,FALSE)));
		}
		block_info->ionode_inx[n-1] = -1;
	} else {
		/* nothing to do */
	}
	FETCH_FIELD(hv, block_info, job_running, uint32_t, TRUE);
	FETCH_FIELD(hv, block_info, linuximage, charp, FALSE);
	FETCH_FIELD(hv, block_info, mloaderimage, charp, FALSE);
	FETCH_FIELD(hv, block_info, nodes, charp, FALSE);
	FETCH_FIELD(hv, block_info, node_cnt, uint32_t, TRUE);
	FETCH_FIELD(hv, block_info, node_use, uint16_t, TRUE);
	FETCH_FIELD(hv, block_info, owner_name, charp, FALSE);
	FETCH_FIELD(hv, block_info, ramdiskimage, charp, FALSE);
	FETCH_FIELD(hv, block_info, reason, charp, FALSE);
	FETCH_FIELD(hv, block_info, state, uint16_t, TRUE);
	return 0;
}

/*
 * convert block_info_msg_t to perl HV
 */
int
block_info_msg_to_hv(block_info_msg_t *block_info_msg, HV *hv)
{
	int i;
	HV *hv_info;
	AV *av;

	STORE_FIELD(hv, block_info_msg, last_update, time_t);
	/* record_count implied in node_array */
	av = newAV();
	for(i = 0; i < block_info_msg->record_count; i ++) {
		hv_info =newHV();
		if (block_info_to_hv(block_info_msg->block_array + i,
				     hv_info) < 0) {
			SvREFCNT_dec((SV*)hv_info);
			SvREFCNT_dec((SV*)av);
			return -1;
		}
		av_store(av, i, newRV_noinc((SV*)hv_info));
	}
	hv_store_sv(hv, "block_array", newRV_noinc((SV*)av));
	return 0;
}

/*
 * convert perl HV to block_info_msg_t
 */
int
hv_to_block_info_msg(HV *hv, block_info_msg_t *block_info_msg)
{
	SV **svp;
	AV *av;
	int i, n;

	memset(block_info_msg, 0, sizeof(block_info_msg_t));

	FETCH_FIELD(hv, block_info_msg, last_update, time_t, TRUE);

	svp = hv_fetch(hv, "block_array", 11, FALSE);
	if (! (svp && SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV)) {
		Perl_warn (aTHX_ "block_array is not an array reference in HV for block_info_msg_t");
		return -1;
	}

	av = (AV*)SvRV(*svp);
	n = av_len(av) + 1;
	block_info_msg->record_count = n;

	block_info_msg->block_array = xmalloc(n * sizeof(block_info_t));
	for (i = 0; i < n; i ++) {
		svp = av_fetch(av, i, FALSE);
		if (! (svp && SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVHV)) {
			Perl_warn (aTHX_ "element %d in node_array is not valid", i);
			return -1;
		}
		if (hv_to_block_info((HV*)SvRV(*svp), &block_info_msg->block_array[i]) < 0) {
			Perl_warn (aTHX_ "failed to convert element %d in block_array", i);
			return -1;
		}
	}
	return 0;
}

/*
 * convert perl HV to update_block_msg_t
 */
int
hv_to_update_block_msg(HV *hv, update_block_msg_t *update_msg)
{
	SV **svp;
	AV *av;
	int i, n;

	slurm_init_update_block_msg(update_msg);

	FETCH_FIELD(hv, update_msg, bg_block_id, charp, FALSE);
	FETCH_FIELD(hv, update_msg, blrtsimage, charp, FALSE);
	svp = hv_fetch(hv, "bp_inx", 6, FALSE);
	if (svp && SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		av = (AV*)SvRV(*svp);
		n = av_len(av) + 2; /* for trailing -1 */
		update_msg->bp_inx = xmalloc(n * sizeof(int));
		for (i = 0 ; i < n-1; i += 2) {
			update_msg->bp_inx[i] = (int)SvIV(*(av_fetch(av, i, FALSE)));
			update_msg->bp_inx[i+1] = (int)SvIV(*(av_fetch(av, i+1 ,FALSE)));
		}
		update_msg->bp_inx[n-1] = -1;
	} else {
		/* nothing to do */
	}
	FETCH_FIELD(hv, update_msg, conn_type, uint16_t, FALSE);
	FETCH_FIELD(hv, update_msg, ionodes, charp, FALSE);
	svp = hv_fetch(hv, "ionode_inx", 10, FALSE);
	if (svp && SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		av = (AV*)SvRV(*svp);
		n = av_len(av) + 2; /* for trailing -1 */
		update_msg->ionode_inx = xmalloc(n * sizeof(int));
		for (i = 0 ; i < n-1; i += 2) {
			update_msg->ionode_inx[i] = (int)SvIV(*(av_fetch(av, i, FALSE)));
			update_msg->ionode_inx[i+1] = (int)SvIV(*(av_fetch(av, i+1 ,FALSE)));
		}
		update_msg->ionode_inx[n-1] = -1;
	} else {
		/* nothing to do */
	}
	FETCH_FIELD(hv, update_msg, job_running, uint32_t, FALSE);
	FETCH_FIELD(hv, update_msg, linuximage, charp, FALSE);
	FETCH_FIELD(hv, update_msg, mloaderimage, charp, FALSE);
	FETCH_FIELD(hv, update_msg, nodes, charp, FALSE);
	FETCH_FIELD(hv, update_msg, node_cnt, uint32_t, FALSE);
	FETCH_FIELD(hv, update_msg, node_use, uint16_t, FALSE);
	FETCH_FIELD(hv, update_msg, owner_name, charp, FALSE);
	FETCH_FIELD(hv, update_msg, ramdiskimage, charp, FALSE);
	FETCH_FIELD(hv, update_msg, reason, charp, FALSE);
	FETCH_FIELD(hv, update_msg, state, uint16_t, FALSE);
	return 0;
}

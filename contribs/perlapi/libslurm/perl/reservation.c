/*
 * reservation.c - convert data between reservation related messages
 *                 and perl HVs
 */

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>
#include "ppport.h"

#include <slurm/slurm.h>
#include "slurm-perl.h"

/*
 * convert reserve_info_t to perl HV
 */
int
reserve_info_to_hv(reserve_info_t *reserve_info, HV *hv)
{
	if (reserve_info->accounts)
		STORE_FIELD(hv, reserve_info, accounts, charp);
	STORE_FIELD(hv, reserve_info, end_time, time_t);
	if (reserve_info->features)
		STORE_FIELD(hv, reserve_info, features, charp);
	STORE_FIELD(hv, reserve_info, flags, uint32_t);
	if (reserve_info->licenses)
		STORE_FIELD(hv, reserve_info, licenses, charp);
	if (reserve_info->name)
		STORE_FIELD(hv, reserve_info, name, charp);
	STORE_FIELD(hv, reserve_info, node_cnt, uint32_t);
	if (reserve_info->node_list)
		STORE_FIELD(hv, reserve_info, node_list, charp);

	/* no store for int pointers yet */
	if (reserve_info->node_inx) {
		int j;
		AV *av = newAV();
		for(j = 0; ; j += 2) {
			if(reserve_info->node_inx[j] == -1)
				break;
			av_store(av, j, newSVuv(reserve_info->node_inx[j]));
			av_store(av, j+1,
				 newSVuv(reserve_info->node_inx[j+1]));
		}
		hv_store_sv(hv, "node_inx", newRV_noinc((SV*)av));
	}
	if (reserve_info->partition)
		STORE_FIELD(hv, reserve_info, partition, charp);
	STORE_FIELD(hv, reserve_info, start_time, time_t);
	if (reserve_info->users)
		STORE_FIELD(hv, reserve_info, users, charp);

	return 0;
}

/*
 * convert perl HV to reserve_info_t
 */
int
hv_to_reserve_info(HV *hv, reserve_info_t *resv_info)
{
	SV **svp;
	AV *av;
	int i, n;

	memset(resv_info, 0, sizeof(reserve_info_t));

	FETCH_FIELD(hv, resv_info, accounts, charp, FALSE);
	FETCH_FIELD(hv, resv_info, end_time, time_t, TRUE);
	FETCH_FIELD(hv, resv_info, features, charp, FALSE);
	FETCH_FIELD(hv, resv_info, flags, uint32_t, TRUE);
	FETCH_FIELD(hv, resv_info, licenses, charp, FALSE);
	FETCH_FIELD(hv, resv_info, name, charp, TRUE);
	FETCH_FIELD(hv, resv_info, node_cnt, uint32_t, TRUE);
	svp = hv_fetch(hv, "node_inx", 8, FALSE);
	if (svp && SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		av = (AV*)SvRV(*svp);
		n = av_len(av) + 2; /* for trailing -1 */
		resv_info->node_inx = xmalloc(n * sizeof(int));
		for (i = 0 ; i < n-1; i += 2) {
			resv_info->node_inx[i] = (int)SvIV(*(av_fetch(av, i ,FALSE)));
			resv_info->node_inx[i+1] = (int)SvIV(*(av_fetch(av, i+1 ,FALSE)));
		}
		resv_info->node_inx[n-1] = -1;
	} else {
		/* nothing to do */
	}
	FETCH_FIELD(hv, resv_info, node_list, charp, FALSE);
	FETCH_FIELD(hv, resv_info, partition, charp, FALSE);
	FETCH_FIELD(hv, resv_info, start_time, time_t, TRUE);
	FETCH_FIELD(hv, resv_info, users, charp, FALSE);
	return 0;
}

/*
 * convert reserve_info_msg_t to perl HV
 */
int
reserve_info_msg_to_hv(reserve_info_msg_t *reserve_info_msg, HV *hv)
{
	int i;
	HV *hv_info;
	AV *av;

	STORE_FIELD(hv, reserve_info_msg, last_update, time_t);
	/* record_count implied in reservation_array */
	av = newAV();
	for(i = 0; i < reserve_info_msg->record_count; i ++) {
		hv_info = newHV();
		if (reserve_info_to_hv(reserve_info_msg->reservation_array + i,
				       hv_info)
		    < 0) {
			SvREFCNT_dec(hv_info);
			SvREFCNT_dec(av);
			return -1;
		}
		av_store(av, i, newRV_noinc((SV*)hv_info));
	}
	hv_store_sv(hv, "reservation_array", newRV_noinc((SV*)av));
	return 0;
}

/* 
 * convert perl HV to reserve_info_msg_t
 */
int
hv_to_reserve_info_msg(HV *hv, reserve_info_msg_t *resv_info_msg)
{
	SV **svp;
	AV *av;
	int i, n;

	memset(resv_info_msg, 0, sizeof(reserve_info_msg_t));

	FETCH_FIELD(hv, resv_info_msg, last_update, time_t, TRUE);

	svp = hv_fetch(hv, "reservation_array", 17, FALSE);
	if (! (svp && SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV)) {
		Perl_warn (aTHX_ "reservation_array is not an array reference in HV for reservation_info_msg_t");
		return -1;
	}

	av = (AV*)SvRV(*svp);
	n = av_len(av) + 1;
	resv_info_msg->record_count = n;

	resv_info_msg->reservation_array = xmalloc(n * sizeof(reserve_info_t));
	for (i = 0; i < n; i ++) {
		svp = av_fetch(av, i, FALSE);
		if (! (svp && SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVHV)) {
			Perl_warn (aTHX_ "element %d in reservation_array is not valid", i);
			return -1;
		}
		if (hv_to_reserve_info((HV*)SvRV(*svp), &resv_info_msg->reservation_array[i]) < 0) {
			Perl_warn (aTHX_ "failed to convert element %d in reservation_array", i);
			return -1;
		}
	}
	return 0;
}

/*
 * convert perl HV to resv_desc_msg_t.
 */
int
hv_to_update_reservation_msg(HV *hv, resv_desc_msg_t *resv_msg)
{
	slurm_init_resv_desc_msg(resv_msg);

 	FETCH_FIELD(hv, resv_msg, accounts, charp, FALSE);
	FETCH_FIELD(hv, resv_msg, duration, uint32_t, FALSE);
	FETCH_FIELD(hv, resv_msg, end_time, time_t, FALSE);
	FETCH_FIELD(hv, resv_msg, features, charp, FALSE);
	FETCH_FIELD(hv, resv_msg, flags, uint32_t, FALSE);
	FETCH_FIELD(hv, resv_msg, licenses, charp, FALSE);
	FETCH_FIELD(hv, resv_msg, name, charp, FALSE);
	FETCH_PTR_FIELD(hv, resv_msg, node_cnt, "SLURM::uint32_t", FALSE);
	FETCH_FIELD(hv, resv_msg, node_list, charp, FALSE);
	FETCH_FIELD(hv, resv_msg, partition, charp, FALSE);
	FETCH_FIELD(hv, resv_msg, start_time, time_t, FALSE);
	FETCH_FIELD(hv, resv_msg, users, charp, FALSE);

	return 0;
}

/*
 * convert perl HV to reservation_name_msg_t.
 */
int
hv_to_delete_reservation_msg(HV *hv, reservation_name_msg_t *resv_name)
{
	resv_name->name = NULL;

	FETCH_FIELD(hv, resv_name, name, charp, FALSE);

	return 0;
}

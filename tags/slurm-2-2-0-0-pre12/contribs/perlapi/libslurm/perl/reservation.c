/*
 * reservation.c - convert data between reservation related messages
 *                 and perl HVs
 */

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>

#include <slurm/slurm.h>
#include "slurm-perl.h"

/*
 * convert reserve_info_t to perl HV
 */
int
reserve_info_to_hv(reserve_info_t* reserve_info, HV* hv)
{
	if (reserve_info->accounts)
		STORE_FIELD(hv, reserve_info, accounts, charp);
	if (reserve_info->end_time)
		STORE_FIELD(hv, reserve_info, end_time, time_t);
	if (reserve_info->features)
		STORE_FIELD(hv, reserve_info, features, charp);
	if (reserve_info->flags)
		STORE_FIELD(hv, reserve_info, flags, uint16_t);
	if (reserve_info->licenses)
		STORE_FIELD(hv, reserve_info, licenses, charp);
	if (reserve_info->name)
		STORE_FIELD(hv, reserve_info, name, charp);
	if (reserve_info->node_cnt)
		STORE_FIELD(hv, reserve_info, node_cnt, uint32_t);
	if (reserve_info->node_list)
		STORE_FIELD(hv, reserve_info, node_list, charp);

	/* no store for int pointers yet */
	if (reserve_info->node_inx) {
		int j;
		AV* avp = newAV();
		for(j = 0; ; j += 2) {
			if(reserve_info->node_inx[j] == -1)
				break;
			av_store(avp, j, newSVuv(reserve_info->node_inx[j]));
			av_store(avp, j+1,
				 newSVuv(reserve_info->node_inx[j+1]));
		}
		hv_store_sv(hv, "node_inx", newRV_noinc((SV*)avp));
	}
	if (reserve_info->partition)
		STORE_FIELD(hv, reserve_info, partition, charp);
	if (reserve_info->start_time)
		STORE_FIELD(hv, reserve_info, start_time, time_t);

	return 0;
}

/*
 * convert partition_info_msg_t to perl HV
 */
int
reserve_info_msg_to_hv(reserve_info_msg_t* reserve_info_msg, HV* hv)
{
	int i;
	HV* hvp;
	AV *avp;

	STORE_FIELD(hv, reserve_info_msg, last_update, time_t);
	/* record_count implied in reservation_array */
	avp = newAV();
	for(i = 0; i < reserve_info_msg->record_count; i ++) {
		hvp = newHV();
		if (reserve_info_to_hv(reserve_info_msg->reservation_array + i,
				       hvp)
		    < 0) {
			SvREFCNT_dec(hvp);
			SvREFCNT_dec(avp);
			return -1;
		}
		av_store(avp, i, newRV_noinc((SV*)hvp));
	}
	hv_store_sv(hv, "reservation_array", newRV_noinc((SV*)avp));
	return 0;
}

/*
 * convert perl HV to resv_desc_msg_t.
 */
int
hv_to_update_reservation_msg(HV* hv, resv_desc_msg_t* resv_msg)
{
 	resv_msg->accounts = NULL;
	resv_msg->duration = (uint32_t) NO_VAL;
	resv_msg->end_time = (time_t) NO_VAL;
	resv_msg->features = NULL;
	resv_msg->flags = (uint16_t) NO_VAL;
	resv_msg->licenses = NULL;
	resv_msg->name = NULL;
	resv_msg->node_cnt = (uint32_t) NO_VAL;
	resv_msg->node_list = NULL;
	resv_msg->partition = NULL;
	resv_msg->start_time = (time_t) NO_VAL;
	resv_msg->users = NULL;

 	FETCH_FIELD(hv, resv_msg, accounts, charp, FALSE);
	FETCH_FIELD(hv, resv_msg, duration, uint32_t, FALSE);
	FETCH_FIELD(hv, resv_msg, end_time, time_t, FALSE);
	FETCH_FIELD(hv, resv_msg, features, charp, FALSE);
	FETCH_FIELD(hv, resv_msg, flags, uint16_t, FALSE);
	FETCH_FIELD(hv, resv_msg, licenses, charp, FALSE);
	FETCH_FIELD(hv, resv_msg, name, charp, FALSE);
	FETCH_FIELD(hv, resv_msg, node_cnt, uint32_t, FALSE);
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
hv_to_delete_reservation_msg(HV* hv, reservation_name_msg_t* resv_name)
{
	resv_name->name = NULL;

	FETCH_FIELD(hv, resv_name, name, charp, FALSE);

	return 0;
}

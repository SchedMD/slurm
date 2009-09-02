/*
 * trigger.c - convert data between trigger related messages and perl HVs
 */

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>

#include <slurm/slurm.h>
#include "msg.h"

/*
 * convert trigger_info_t to perl HV 
 */
int
trigger_info_to_hv(trigger_info_t* trigger_info, HV* hv)
{
	STORE_FIELD(hv, trigger_info, trig_id, uint32_t);
	STORE_FIELD(hv, trigger_info, res_type, uint16_t);
	if(trigger_info->res_id)
		STORE_FIELD(hv, trigger_info, res_id, charp);
	STORE_FIELD(hv, trigger_info, trig_type, uint16_t);
	STORE_FIELD(hv, trigger_info, offset, uint16_t);
	STORE_FIELD(hv, trigger_info, user_id, uint32_t);
	if(trigger_info->program)
		STORE_FIELD(hv, trigger_info, program, charp);
	return 0;
}
/*
 * convert _msg_t to perl HV 
 */
int
trigger_info_msg_to_hv(trigger_info_msg_t* trigger_info_msg, HV* hv)
{
	int i;
	HV* hvp;
	AV* avp;

	/* record_count implied in node_array */
	avp = newAV();
	for(i = 0; i < trigger_info_msg->record_count; i ++) {
		hvp =newHV();
		if (trigger_info_to_hv(trigger_info_msg->trigger_array + i, hvp) < 0) {
			SvREFCNT_dec((SV*)hvp);
			SvREFCNT_dec((SV*)avp);
			return -1;
		}
		av_store(avp, i, newRV_noinc((SV*)hvp));
	}
	hv_store_sv(hv, "trigger_array", newRV_noinc((SV*)avp));
	return 0;
}

/*
 * convert perl HV to trigger_info_t 
 */
int
hv_to_trigger_info(HV* hv, trigger_info_t *trigger_info)
{
	
	FETCH_FIELD(hv, trigger_info, trig_id, uint32_t, FALSE);
	FETCH_FIELD(hv, trigger_info, res_type, uint8_t, FALSE);
	FETCH_FIELD(hv, trigger_info, res_id, charp, FALSE);
	FETCH_FIELD(hv, trigger_info, trig_type, uint16_t, FALSE);
	FETCH_FIELD(hv, trigger_info, offset, uint16_t, FALSE);
	FETCH_FIELD(hv, trigger_info, user_id, uint32_t, TRUE);
	FETCH_FIELD(hv, trigger_info, program, charp, TRUE);
	return 0;
}

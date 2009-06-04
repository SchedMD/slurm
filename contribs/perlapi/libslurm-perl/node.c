/*
 * node.c - convert data between node related messages and perl HVs
 */

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>

#include <slurm/slurm.h>
#include "msg.h"

/*
 * convert node_info_t to perl HV 
 */
int
node_info_to_hv(node_info_t* node_info, HV* hv)
{
	if(node_info->arch)
		STORE_FIELD(hv, node_info, arch, charp);
	STORE_FIELD(hv, node_info, cores, uint16_t);
	STORE_FIELD(hv, node_info, cpus, uint16_t);
	if(node_info->features)
		STORE_FIELD(hv, node_info, features, charp);
	if (node_info->name)
		STORE_FIELD(hv, node_info, name, charp);
	else {
		Perl_warn (aTHX_ "node name missing in node_info_t");
		return -1;
	}
	STORE_FIELD(hv, node_info, node_state, uint16_t);
	if(node_info->os)
		STORE_FIELD(hv, node_info, os, charp);
	STORE_FIELD(hv, node_info, real_memory, uint32_t);
	if(node_info->reason)
		STORE_FIELD(hv, node_info, reason, charp);
	STORE_FIELD(hv, node_info, sockets, uint16_t);
	STORE_FIELD(hv, node_info, threads, uint16_t);
	STORE_FIELD(hv, node_info, tmp_disk, uint32_t);

	/* TODO: select_nodeinfo */

	STORE_FIELD(hv, node_info, weight, uint32_t);
	return 0;
}
/*
 * convert node_info_msg_t to perl HV 
 */
int
node_info_msg_to_hv(node_info_msg_t* node_info_msg, HV* hv)
{
	int i;
	HV* hvp;
	AV* avp;

	STORE_FIELD(hv, node_info_msg, last_update, time_t);
	STORE_FIELD(hv, node_info_msg, node_scaling, uint16_t);
	/* record_count implied in node_array */
	avp = newAV();
	for(i = 0; i < node_info_msg->record_count; i ++) {
		hvp =newHV();
		if (node_info_to_hv(node_info_msg->node_array + i, hvp) < 0) {
			SvREFCNT_dec((SV*)hvp);
			SvREFCNT_dec((SV*)avp);
			return -1;
		}
		av_store(avp, i, newRV_noinc((SV*)hvp));
	}
	hv_store_sv(hv, "node_array", newRV_noinc((SV*)avp));
	return 0;
}

/*
 * convert perl HV to update_node_msg_t 
 */
int
hv_to_update_node_msg(HV* hv, update_node_msg_t *update_msg)
{
 	update_msg->node_names = NULL;
	update_msg->features = NULL;
	update_msg->reason = NULL;
	update_msg->node_state = (uint16_t) NO_VAL;
	update_msg->weight = (uint32_t) NO_VAL;

	FETCH_FIELD(hv, update_msg, node_names, charp, TRUE);
	FETCH_FIELD(hv, update_msg, node_state, uint16_t, FALSE);
	FETCH_FIELD(hv, update_msg, reason, charp, FALSE);
	FETCH_FIELD(hv, update_msg, features, charp, FALSE);
	FETCH_FIELD(hv, update_msg, weight, uint32_t, FALSE);
	return 0;
}


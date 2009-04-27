/*
 * partition.c - convert data between partition related messages and perl HVs
 */

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>

#include <slurm/slurm.h>
#include "msg.h"

/*
 * convert partition_info_t to perl HV
 */
int
part_info_to_hv(partition_info_t* part_info, HV* hv)
{
	if (part_info->allow_alloc_nodes)
		STORE_FIELD(hv, part_info, allow_alloc_nodes, charp);
	if (part_info->allow_groups)
		STORE_FIELD(hv, part_info, allow_groups, charp);
	STORE_FIELD(hv, part_info, default_part, uint16_t);
	STORE_FIELD(hv, part_info, default_time, uint32_t);
	STORE_FIELD(hv, part_info, disable_root_jobs, uint16_t);
	STORE_FIELD(hv, part_info, hidden, uint16_t);
	STORE_FIELD(hv, part_info, max_nodes, uint32_t);
	STORE_FIELD(hv, part_info, max_share, uint16_t);
	STORE_FIELD(hv, part_info, max_time, uint32_t);
	STORE_FIELD(hv, part_info, min_nodes, uint32_t);
	if (part_info->name)
		STORE_FIELD(hv, part_info, name, charp);
	else {
		Perl_warn(aTHX_ "partition name missing in partition_info_t");
		return -1;
	}
	/* no store for int pointers yet */
	if (part_info->node_inx) {
		int j;
		AV* avp = newAV();
		for(j = 0; ; j += 2) {
			if(part_info->node_inx[j] == -1)
				break;
			av_store(avp, j, newSVuv(part_info->node_inx[j]));
			av_store(avp, j+1, newSVuv(part_info->node_inx[j+1]));
		}
		hv_store_sv(hv, "node_inx", newRV_noinc((SV*)avp));
	}
	STORE_FIELD(hv, part_info, node_scaling, uint16_t);
	if (part_info->nodes)
		STORE_FIELD(hv, part_info, nodes, charp);
	STORE_FIELD(hv, part_info, priority, uint16_t);
	STORE_FIELD(hv, part_info, root_only, uint16_t);
	STORE_FIELD(hv, part_info, state_up, uint16_t);
	STORE_FIELD(hv, part_info, total_cpus, uint32_t);
	STORE_FIELD(hv, part_info, total_nodes, uint32_t);

	return 0;
}

/*
 * convert partition_info_msg_t to perl HV
 */
int
partition_info_msg_to_hv(partition_info_msg_t* part_info_msg, HV* hv)
{
	int i;
	HV* hvp;
	AV *avp;

	STORE_FIELD(hv, part_info_msg, last_update, time_t);
	/* record_count implied in partition_array */
	avp = newAV();
	for(i = 0; i < part_info_msg->record_count; i ++) {
		hvp = newHV();
		if (part_info_to_hv(part_info_msg->partition_array + i, hvp)
		    < 0) {
			SvREFCNT_dec(hvp);
			SvREFCNT_dec(avp);
			return -1;
		}
		av_store(avp, i, newRV_noinc((SV*)hvp));
	}
	hv_store_sv(hv, "partition_array", newRV_noinc((SV*)avp));
	return 0;
}

int
hv_to_update_part_msg(HV* hv, update_part_msg_t* part_msg)
{
	slurm_init_part_desc_msg(part_msg);
	
	FETCH_FIELD(hv, part_msg, allow_alloc_nodes, charp, FALSE);
	FETCH_FIELD(hv, part_msg, allow_groups, charp, FALSE);
	FETCH_FIELD(hv, part_msg, default_part, uint16_t, FALSE);
	FETCH_FIELD(hv, part_msg, default_time, uint32_t, FALSE);
	FETCH_FIELD(hv, part_msg, disable_root_jobs, uint16_t, FALSE);
	FETCH_FIELD(hv, part_msg, hidden, uint16_t, FALSE);
	FETCH_FIELD(hv, part_msg, max_nodes, uint32_t, FALSE);
	FETCH_FIELD(hv, part_msg, max_share, uint16_t, FALSE);
	FETCH_FIELD(hv, part_msg, max_time, uint32_t, FALSE);
	FETCH_FIELD(hv, part_msg, min_nodes, uint32_t, FALSE);
	FETCH_FIELD(hv, part_msg, name, charp, TRUE);
	/*not used node_inx */
	FETCH_FIELD(hv, part_msg, node_scaling, uint16_t, FALSE);
	FETCH_FIELD(hv, part_msg, nodes, charp, FALSE);
	FETCH_FIELD(hv, part_msg, priority, uint16_t, FALSE);
	FETCH_FIELD(hv, part_msg, root_only, uint16_t, FALSE);
	FETCH_FIELD(hv, part_msg, state_up, uint16_t, FALSE);
	FETCH_FIELD(hv, part_msg, total_cpus, uint32_t, FALSE);
	FETCH_FIELD(hv, part_msg, total_nodes, uint32_t, FALSE);
	return 0;
}

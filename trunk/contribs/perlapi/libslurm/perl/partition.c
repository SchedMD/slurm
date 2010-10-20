/*
 * partition.c - convert data between partition related messages and perl HVs
 */

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>
#include "ppport.h"

#include <slurm/slurm.h>
#include "slurm-perl.h"

/*
 * convert partition_info_t to perl HV
 */
int
partition_info_to_hv(partition_info_t *part_info, HV *hv)
{
	if (part_info->allow_alloc_nodes)
		STORE_FIELD(hv, part_info, allow_alloc_nodes, charp);
	if (part_info->allow_groups)
		STORE_FIELD(hv, part_info, allow_groups, charp);
	if (part_info->alternate)
		STORE_FIELD(hv, part_info, alternate, charp);
	STORE_FIELD(hv, part_info, default_time, uint32_t);
	STORE_FIELD(hv, part_info, flags, uint16_t);
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
		AV* av = newAV();
		for(j = 0; ; j += 2) {
			if(part_info->node_inx[j] == -1)
				break;
			av_store(av, j, newSVuv(part_info->node_inx[j]));
			av_store(av, j+1, newSVuv(part_info->node_inx[j+1]));
		}
		hv_store_sv(hv, "node_inx", newRV_noinc((SV*)av));
	}

	if (part_info->nodes)
		STORE_FIELD(hv, part_info, nodes, charp);
	STORE_FIELD(hv, part_info, preempt_mode, uint16_t);
	STORE_FIELD(hv, part_info, priority, uint16_t);
	STORE_FIELD(hv, part_info, state_up, uint16_t);
	STORE_FIELD(hv, part_info, total_cpus, uint32_t);
	STORE_FIELD(hv, part_info, total_nodes, uint32_t);

	return 0;
}

/* 
 * convert perl HV to partition_info_t
 */
int
hv_to_partition_info(HV *hv, partition_info_t *part_info)
{
	SV **svp;
	AV *av;
	int i, n;

	memset(part_info, 0, sizeof(partition_info_t));

	FETCH_FIELD(hv, part_info, allow_alloc_nodes, charp, FALSE);
	FETCH_FIELD(hv, part_info, allow_groups, charp, FALSE);
	FETCH_FIELD(hv, part_info, alternate, charp, FALSE);
	FETCH_FIELD(hv, part_info, default_time, uint32_t, TRUE);
	FETCH_FIELD(hv, part_info, flags, uint16_t, TRUE);
	FETCH_FIELD(hv, part_info, max_nodes, uint32_t, TRUE);
	FETCH_FIELD(hv, part_info, max_share, uint16_t, TRUE);
	FETCH_FIELD(hv, part_info, max_time, uint32_t, TRUE);
	FETCH_FIELD(hv, part_info, min_nodes, uint32_t, TRUE);
	FETCH_FIELD(hv, part_info, name, charp, TRUE);
	FETCH_FIELD(hv, part_info, name, charp, TRUE);
	svp = hv_fetch(hv, "node_inx", 8, FALSE);
	if (svp && SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		av = (AV*)SvRV(*svp);
		n = av_len(av) + 2; /* for trailing -1 */
		part_info->node_inx = xmalloc(n * sizeof(int));
		for (i = 0 ; i < n-1; i += 2) {
			part_info->node_inx[i] = (int)SvIV(*(av_fetch(av, i, FALSE)));
			part_info->node_inx[i+1] = (int)SvIV(*(av_fetch(av, i+1 ,FALSE)));
		}
		part_info->node_inx[n-1] = -1;
	} else {
		/* nothing to do */
	}
	FETCH_FIELD(hv, part_info, nodes, charp, FALSE);
	FETCH_FIELD(hv, part_info, preempt_mode, uint16_t, TRUE);
	FETCH_FIELD(hv, part_info, priority, uint16_t, TRUE);
	FETCH_FIELD(hv, part_info, state_up, uint16_t, TRUE);
	FETCH_FIELD(hv, part_info, total_cpus, uint32_t, TRUE);
	FETCH_FIELD(hv, part_info, total_nodes, uint32_t, TRUE);
	return 0;
}

/*
 * convert partition_info_msg_t to perl HV
 */
int
partition_info_msg_to_hv(partition_info_msg_t *part_info_msg, HV *hv)
{
	int i;
	HV *hv_info;
	AV *av;

	STORE_FIELD(hv, part_info_msg, last_update, time_t);
	/* record_count implied in partition_array */
	av = newAV();
	for(i = 0; i < part_info_msg->record_count; i ++) {
		hv_info = newHV();
		if (partition_info_to_hv(part_info_msg->partition_array + i, hv_info)
		    < 0) {
			SvREFCNT_dec(hv_info);
			SvREFCNT_dec(av);
			return -1;
		}
		av_store(av, i, newRV_noinc((SV*)hv_info));
	}
	hv_store_sv(hv, "partition_array", newRV_noinc((SV*)av));
	return 0;
}

/* 
 * convert perl HV to partition_info_msg_t
 */
int
hv_to_partition_info_msg(HV *hv, partition_info_msg_t *part_info_msg)
{
	SV **svp;
	AV *av;
	int i, n;

	memset(part_info_msg, 0, sizeof(partition_info_msg_t));

	FETCH_FIELD(hv, part_info_msg, last_update, time_t, TRUE);
	svp = hv_fetch(hv, "partition_array", 15, TRUE);
	if (! (svp && SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV)) {
		Perl_warn (aTHX_ "partition_array is not an array reference in HV for partition_info_msg_t");
		return -1;
	}

	av = (AV*)SvRV(*svp);
	n = av_len(av) + 1;
	part_info_msg->record_count = n;

	part_info_msg->partition_array = xmalloc(n * sizeof(partition_info_t));
	for (i = 0; i < n; i ++) {
		svp = av_fetch(av, i, FALSE);
		if (! (svp && SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVHV)) {
			Perl_warn (aTHX_ "element %d in partition_array is not valid", i);
			return -1;
		}
		if (hv_to_partition_info((HV*)SvRV(*svp), &part_info_msg->partition_array[i]) < 0) {
			Perl_warn (aTHX_ "failed to convert element %d in partition_array", i);
			return -1;
		}
	}
	return 0;
}

/*
 * convert perl HV to update_part_msg_t
 */
int
hv_to_update_part_msg(HV *hv, update_part_msg_t *part_msg)
{
	slurm_init_part_desc_msg(part_msg);

	FETCH_FIELD(hv, part_msg, allow_alloc_nodes, charp, FALSE);
	FETCH_FIELD(hv, part_msg, allow_groups, charp, FALSE);
	FETCH_FIELD(hv, part_msg, default_time, uint32_t, FALSE);
	FETCH_FIELD(hv, part_msg, flags, uint16_t, FALSE);
	FETCH_FIELD(hv, part_msg, max_nodes, uint32_t, FALSE);
	FETCH_FIELD(hv, part_msg, max_share, uint16_t, FALSE);
	FETCH_FIELD(hv, part_msg, max_time, uint32_t, FALSE);
	FETCH_FIELD(hv, part_msg, min_nodes, uint32_t, FALSE);
	FETCH_FIELD(hv, part_msg, name, charp, TRUE);
	/*not used node_inx */
	FETCH_FIELD(hv, part_msg, nodes, charp, FALSE);
	FETCH_FIELD(hv, part_msg, priority, uint16_t, FALSE);
	FETCH_FIELD(hv, part_msg, state_up, uint16_t, FALSE);
	FETCH_FIELD(hv, part_msg, total_cpus, uint32_t, FALSE);
	FETCH_FIELD(hv, part_msg, total_nodes, uint32_t, FALSE);
	return 0;
}

/*
 * convert perl HV to delete_part_msg_t
 */
int
hv_to_delete_part_msg(HV *hv, delete_part_msg_t *delete_msg)
{
	FETCH_FIELD(hv, delete_msg, name, charp, TRUE);
	return 0;
}

/*
 * node.c - convert data between node related messages and perl HVs
 */

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>
#include "ppport.h"

#include <slurm/slurm.h>
#include "slurm-perl.h"

#ifdef HAVE_BG
/* These are just helper functions from slurm proper that don't get
 * exported regularly.  Copied from src/common/slurm_protocol_defs.h.
 */
#define IS_NODE_ALLOCATED(_X)		\
	((_X->node_state & NODE_STATE_BASE) == NODE_STATE_ALLOCATED)
#define IS_NODE_COMPLETING(_X)	\
	(_X->node_state & NODE_STATE_COMPLETING)
#endif

/*
 * convert node_info_t to perl HV
 */
int
node_info_to_hv(node_info_t *node_info, HV *hv)
{
	uint16_t alloc_cpus = 0;
	if(node_info->arch)
		STORE_FIELD(hv, node_info, arch, charp);
	STORE_FIELD(hv, node_info, boot_time, time_t);
	STORE_FIELD(hv, node_info, cores, uint16_t);
	STORE_FIELD(hv, node_info, cpu_load, uint32_t);
	STORE_FIELD(hv, node_info, cpus, uint16_t);
	if (node_info->features)
		STORE_FIELD(hv, node_info, features, charp);
	if (node_info->features_act)
		STORE_FIELD(hv, node_info, features_act, charp);
	if (node_info->gres)
		STORE_FIELD(hv, node_info, gres, charp);
	if (node_info->name)
		STORE_FIELD(hv, node_info, name, charp);
	else {
		Perl_warn (aTHX_ "node name missing in node_info_t");
		return -1;
	}
	STORE_FIELD(hv, node_info, node_state, uint32_t);
	if(node_info->os)
		STORE_FIELD(hv, node_info, os, charp);
	STORE_FIELD(hv, node_info, real_memory, uint64_t);
	if(node_info->reason)
		STORE_FIELD(hv, node_info, reason, charp);
	STORE_FIELD(hv, node_info, reason_time, time_t);
	STORE_FIELD(hv, node_info, reason_uid, uint32_t);
	STORE_FIELD(hv, node_info, slurmd_start_time, time_t);
	STORE_FIELD(hv, node_info, boards, uint16_t);
	STORE_FIELD(hv, node_info, sockets, uint16_t);
	STORE_FIELD(hv, node_info, threads, uint16_t);
	STORE_FIELD(hv, node_info, tmp_disk, uint32_t);

	slurm_get_select_nodeinfo(node_info->select_nodeinfo,
				  SELECT_NODEDATA_SUBCNT,
				  NODE_STATE_ALLOCATED,
				  &alloc_cpus);

	hv_store_uint16_t(hv, "alloc_cpus", alloc_cpus);

	STORE_PTR_FIELD(hv, node_info, select_nodeinfo, "Slurm::dynamic_plugin_data_t");

	STORE_FIELD(hv, node_info, weight, uint32_t);
	return 0;
}

/*
 * convert perl HV to node_info_t
 */
int
hv_to_node_info(HV *hv, node_info_t *node_info)
{
	SV **svp;

	memset(node_info, 0, sizeof(node_info_t));

	/*
	 * slurmcld will pack hidden nodes with a NULL name.
	 * node_info_msg_to_hv() will create an empty has for these records.
	 * If name is not set just return.
	 */
	if (!(svp = hv_fetch(hv, "name", 4, FALSE)))
		return 0;

	FETCH_FIELD(hv, node_info, arch, charp, FALSE);
	FETCH_FIELD(hv, node_info, boot_time, time_t, TRUE);
	FETCH_FIELD(hv, node_info, cores, uint16_t, TRUE);
	FETCH_FIELD(hv, node_info, cpu_load, uint32_t, TRUE);
	FETCH_FIELD(hv, node_info, cpus, uint16_t, TRUE);
	FETCH_FIELD(hv, node_info, features, charp, FALSE);
	FETCH_FIELD(hv, node_info, features_act, charp, FALSE);
	FETCH_FIELD(hv, node_info, gres, charp, FALSE);
	FETCH_FIELD(hv, node_info, name, charp, TRUE);
	FETCH_FIELD(hv, node_info, node_state, uint32_t, TRUE);
	FETCH_FIELD(hv, node_info, os, charp, FALSE);
	FETCH_FIELD(hv, node_info, real_memory, uint64_t, TRUE);
	FETCH_FIELD(hv, node_info, reason, charp, FALSE);
	FETCH_FIELD(hv, node_info, reason_time, time_t, TRUE);
	FETCH_FIELD(hv, node_info, reason_uid, uint32_t, TRUE);
	FETCH_FIELD(hv, node_info, slurmd_start_time, time_t, TRUE);
	FETCH_FIELD(hv, node_info, boards, uint16_t, TRUE);
	FETCH_FIELD(hv, node_info, sockets, uint16_t, TRUE);
	FETCH_FIELD(hv, node_info, threads, uint16_t, TRUE);
	FETCH_FIELD(hv, node_info, tmp_disk, uint32_t, TRUE);
	FETCH_FIELD(hv, node_info, weight, uint32_t, TRUE);
	FETCH_PTR_FIELD(hv, node_info, select_nodeinfo, "Slurm::dynamic_plugin_data_t", TRUE);
	return 0;
}

/*
 * convert node_info_msg_t to perl HV
 */
int
node_info_msg_to_hv(node_info_msg_t *node_info_msg, HV *hv)
{
	int i;
	HV *hv_info;
	AV *av;

	STORE_FIELD(hv, node_info_msg, last_update, time_t);
	/*
	 * node_info_msg->node_array will have node_records with NULL names for
	 * nodes that are hidden. They are put in the array to preserve the
	 * node_index which will match up with a partiton's node_inx[]. Add
	 * empty hashes for nodes that have NULL names -- hidden nodes.
	 */
	av = newAV();
	for(i = 0; i < node_info_msg->record_count; i ++) {
		hv_info =newHV();
		if (node_info_msg->node_array[i].name &&
		    node_info_to_hv(node_info_msg->node_array + i,
				    hv_info) < 0) {
			SvREFCNT_dec((SV*)hv_info);
			SvREFCNT_dec((SV*)av);
			return -1;
		}
		av_store(av, i, newRV_noinc((SV*)hv_info));
	}
	hv_store_sv(hv, "node_array", newRV_noinc((SV*)av));
	return 0;
}

/*
 * convert perl HV to node_info_msg_t
 */
int
hv_to_node_info_msg(HV *hv, node_info_msg_t *node_info_msg)
{
	SV **svp;
	AV *av;
	int i, n;

	memset(node_info_msg, 0, sizeof(node_info_msg_t));

	FETCH_FIELD(hv, node_info_msg, last_update, time_t, TRUE);

	svp = hv_fetch(hv, "node_array", 10, FALSE);
	if (! (svp && SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV)) {
		Perl_warn (aTHX_ "node_array is not an array reference in HV for node_info_msg_t");
		return -1;
	}

	av = (AV*)SvRV(*svp);
	n = av_len(av) + 1;
	node_info_msg->record_count = n;

	node_info_msg->node_array = xmalloc(n * sizeof(node_info_t));
	for (i = 0; i < n; i ++) {
		svp = av_fetch(av, i, FALSE);
		if (! (svp && SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVHV)) {
			Perl_warn (aTHX_ "element %d in node_array is not valid", i);
			return -1;
		}
		if (hv_to_node_info((HV*)SvRV(*svp), &node_info_msg->node_array[i]) < 0) {
			Perl_warn (aTHX_ "failed to convert element %d in node_array", i);
			return -1;
		}
	}
	return 0;
}

/*
 * convert perl HV to update_node_msg_t
 */
int
hv_to_update_node_msg(HV *hv, update_node_msg_t *update_msg)
{
	slurm_init_update_node_msg(update_msg);

	FETCH_FIELD(hv, update_msg, node_addr, charp, FALSE);
	FETCH_FIELD(hv, update_msg, node_hostname, charp, FALSE);
	FETCH_FIELD(hv, update_msg, node_names, charp, TRUE);
	FETCH_FIELD(hv, update_msg, node_state, uint32_t, FALSE);
	FETCH_FIELD(hv, update_msg, reason, charp, FALSE);
	FETCH_FIELD(hv, update_msg, resume_after, uint32_t, FALSE);
	FETCH_FIELD(hv, update_msg, features, charp, FALSE);
	FETCH_FIELD(hv, update_msg, features_act, charp, FALSE);
	FETCH_FIELD(hv, update_msg, weight, uint32_t, FALSE);
	return 0;
}

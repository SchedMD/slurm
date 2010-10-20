/*
 * topo.c - convert data between topology related messages and perl HVs
 */

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>
#include "ppport.h"

#include <slurm/slurm.h>
#include "slurm-perl.h"

/*
 * convert topo_info_t to perl HV 
 */
int
topo_info_to_hv(topo_info_t *topo_info, HV *hv)
{
	STORE_FIELD(hv, topo_info, level, uint16_t);
	STORE_FIELD(hv, topo_info, link_speed, uint32_t);
	if(topo_info->name)
		STORE_FIELD(hv, topo_info, name, charp);
	if(topo_info->nodes)
		STORE_FIELD(hv, topo_info, nodes, charp);
	if(topo_info->switches)
		STORE_FIELD(hv, topo_info, switches, charp);
	return 0;
}

/*
 * convert perl HV to topo_info_t 
 */
int
hv_to_topo_info(HV *hv, topo_info_t *topo_info)
{
	memset(topo_info, 0, sizeof(topo_info_t));

	FETCH_FIELD(hv, topo_info, level, uint16_t, TRUE);
	FETCH_FIELD(hv, topo_info, link_speed, uint32_t, TRUE);
	FETCH_FIELD(hv, topo_info, name, charp, FALSE);
	FETCH_FIELD(hv, topo_info, nodes, charp, TRUE);
	FETCH_FIELD(hv, topo_info, switches, charp, TRUE);
	return 0;
}

/*
 * convert topo_info_response_msg_t to perl HV 
 */
int
topo_info_response_msg_to_hv(topo_info_response_msg_t *topo_info_msg, HV *hv)
{
	int i;
	HV* hv_info;
	AV* av;

	/* record_count implied in node_array */
	av = newAV();
	for(i = 0; i < topo_info_msg->record_count; i ++) {
		hv_info =newHV();
		if (topo_info_to_hv(topo_info_msg->topo_array + i, hv_info) < 0) {
			SvREFCNT_dec((SV*)hv_info);
			SvREFCNT_dec((SV*)av);
			return -1;
		}
		av_store(av, i, newRV_noinc((SV*)hv_info));
	}
	hv_store_sv(hv, "topo_array", newRV_noinc((SV*)av));
	return 0;
}

/* 
 * convert perl HV to topo_info_response_msg_t
 */
int
hv_to_topo_info_response_msg(HV *hv, topo_info_response_msg_t *topo_info_msg)
{
	SV **svp;
	AV *av;
	int i, n;

	memset(topo_info_msg, 0, sizeof(topo_info_response_msg_t));

	svp = hv_fetch(hv, "topo_array", 10, FALSE);
	if (! (svp && SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV)) {
		Perl_warn (aTHX_ "topo_array is not an array refrence in HV for topo_info_response_msg_t");
		return -1;
	}

	av = (AV*)SvRV(*svp);
	n = av_len(av) + 1;
	topo_info_msg->record_count = n;

	topo_info_msg->topo_array = xmalloc(n * sizeof(topo_info_t));
	for (i = 0; i < n; i ++) {
		svp = av_fetch(av, i, FALSE);
		if (! (svp && SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVHV)) {
			Perl_warn (aTHX_ "element %d in topo_array is not valid", i);
			return -1;
		}
		if (hv_to_topo_info((HV*)SvRV(*svp), &topo_info_msg->topo_array[i]) < 0) {
			Perl_warn (aTHX_ "failed to convert element %d in topo_array", i);
			return -1;
		}
	}
	return 0;
}


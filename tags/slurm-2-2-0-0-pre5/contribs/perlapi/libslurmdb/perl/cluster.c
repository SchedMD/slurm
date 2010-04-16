/*
 * cluster.c - convert data between cluster related messages and perl HVs
 */

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>

#include <slurm/slurmdb.h>
#include "msg.h"

char *slurm_xstrdup(const char *str);

int
hv_to_cluster_cond(HV* hv, slurmdb_cluster_cond_t* cluster_cond)
{
	AV* cluster_av;
	SV** svp;
	char* cluster = NULL;
	int i, elements = 0;

	cluster_cond->classification = SLURMDB_CLASS_NONE;
	cluster_cond->cluster_list = slurm_list_create(NULL);
	cluster_cond->usage_end = 0;
	cluster_cond->usage_start = 0;
	cluster_cond->with_deleted = 1;
	cluster_cond->with_usage = 1;

	FETCH_FIELD(hv, cluster_cond, classification, uint16_t, FALSE);

	if((svp = hv_fetch(hv, "cluster_list", 12, FALSE))) {
	    if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		cluster_av = (AV*)SvRV(*svp);
		elements = av_len(cluster_av) + 1;
		if (elements > 0) {
		    for(i = 0; i < elements; i ++) {
			if((svp = av_fetch(cluster_av, i, FALSE))) {
			    cluster = slurm_xstrdup((char*)SvPV_nolen(*svp));
			    slurm_list_append(cluster_cond->cluster_list, cluster);
			} else {
			    Perl_warn(aTHX_ "error fetching cluster from cluster_list");
			    return -1;
			}
		    }
		}
	    } else {
		Perl_warn(aTHX_ "cluster_list of cluster_cond is not an array reference");
		return -1;
	    }
	}

	FETCH_FIELD(hv, cluster_cond, usage_end, time_t , FALSE);
	FETCH_FIELD(hv, cluster_cond, usage_start, time_t , FALSE);
	FETCH_FIELD(hv, cluster_cond, with_deleted, uint16_t, FALSE);
	FETCH_FIELD(hv, cluster_cond, with_usage, uint16_t, FALSE);

	return 0;
}

int
cluster_accounting_rec_to_hv(slurmdb_cluster_accounting_rec_t *ar, HV* hv)
{
	STORE_FIELD(hv, ar, alloc_secs, uint64_t);
	STORE_FIELD(hv, ar, cpu_count, uint32_t);
	STORE_FIELD(hv, ar, down_secs, uint64_t);
	STORE_FIELD(hv, ar, idle_secs, uint64_t);
	STORE_FIELD(hv, ar, over_secs, uint64_t);
	STORE_FIELD(hv, ar, pdown_secs, uint64_t);
	STORE_FIELD(hv, ar, period_start, time_t);
	STORE_FIELD(hv, ar, resv_secs, uint64_t);

	return 0;
}

int
cluster_rec_to_hv(slurmdb_cluster_rec_t *rec, HV* hv)
{
	AV * acc_av;
	HV * rh;
	ListIterator itr = slurm_list_iterator_create(rec->accounting_list);
	slurmdb_cluster_accounting_rec_t* ar = NULL;
	acc_av = (AV *)sv_2mortal((SV *)newAV());

	while ((ar = slurm_list_next(itr))) {
	    rh = (HV *)sv_2mortal((SV *)newHV());
	    cluster_accounting_rec_to_hv(ar, rh);
	    av_push(acc_av, newRV_noinc((SV *)rh));
	}
	hv_store_sv(hv, "accounting_list", newRV((SV*)acc_av));
	STORE_FIELD(hv, rec, classification, uint16_t);
	STORE_FIELD(hv, rec, control_host, charp);
	STORE_FIELD(hv, rec, control_port, uint32_t);
	STORE_FIELD(hv, rec, cpu_count, uint32_t);
	STORE_FIELD(hv, rec, name, charp);
	STORE_FIELD(hv, rec, nodes, charp);
	/* slurmdb_association_rec_t *root_assoc; */
	STORE_FIELD(hv, rec, rpc_version, uint16_t);

	return 0;
}


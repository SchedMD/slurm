/*
 * cluster.c - convert data between cluster related messages and perl HVs
 */

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>

#include <slurm/slurmdb.h>
#include "slurmdb-perl.h"

extern char *slurm_xstrdup(const char *str);
extern int slurmdb_report_set_start_end_time(time_t *start, time_t *end);

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
		for(i = 0; i < elements; i ++) {
		    if((svp = av_fetch(cluster_av, i, FALSE))) {
			cluster = slurm_xstrdup((char*)SvPV_nolen(*svp));
			slurm_list_append(cluster_cond->cluster_list, cluster);
		    } else {
			Perl_warn(aTHX_ "error fetching cluster from cluster_list");
			return -1;
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
hv_to_assoc_cond(HV* hv, slurmdb_association_cond_t* assoc_cond)
{
	AV* element_av;
	SV** svp;
	char* str = NULL;
	int i, elements = 0;
	time_t start_time = 0;
	time_t end_time = 0;

	if ( (svp = hv_fetch (hv, "usage_start", strlen("usage_start"), FALSE)) ) {
	    start_time = (time_t) (SV2time_t(*svp));
	}
	if ( (svp = hv_fetch (hv, "usage_end", strlen("usage_end"), FALSE)) ) {
	    end_time = (time_t) (SV2time_t(*svp));
	}
	slurmdb_report_set_start_end_time(&start_time, &end_time);
	assoc_cond->usage_start = start_time;
	assoc_cond->usage_end = end_time;

	assoc_cond->with_usage = 1;
	assoc_cond->with_deleted = 0;
	assoc_cond->with_raw_qos = 0;
	assoc_cond->with_sub_accts = 0;
	assoc_cond->without_parent_info = 0;
	assoc_cond->without_parent_limits = 0;

	FETCH_FIELD(hv, assoc_cond, with_usage, uint16_t, FALSE);
	FETCH_FIELD(hv, assoc_cond, with_deleted, uint16_t, FALSE);
	FETCH_FIELD(hv, assoc_cond, with_raw_qos, uint16_t, FALSE);
	FETCH_FIELD(hv, assoc_cond, with_sub_accts, uint16_t, FALSE);
	FETCH_FIELD(hv, assoc_cond, without_parent_info, uint16_t, FALSE);
	FETCH_FIELD(hv, assoc_cond, without_parent_limits, uint16_t, FALSE);

	if((svp = hv_fetch(hv, "acct_list", strlen("acct_list"), FALSE))) {
	    if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		assoc_cond->acct_list = slurm_list_create(NULL);
		element_av = (AV*)SvRV(*svp);
		elements = av_len(element_av) + 1;
		for(i = 0; i < elements; i ++) {
		    if((svp = av_fetch(element_av, i, FALSE))) {
			str = slurm_xstrdup((char*)SvPV_nolen(*svp));
			slurm_list_append(assoc_cond->acct_list, str);
		    } else {
			Perl_warn(aTHX_ "error fetching acct from acct_list");
			return -1;
		    }
		}
	    } else {
		Perl_warn(aTHX_ "acct_list of association_cond is not an array reference");
		return -1;
	    }
	}

	if((svp = hv_fetch(hv, "cluster_list", strlen("cluster_list"), FALSE))) {
	    if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		assoc_cond->cluster_list = slurm_list_create(NULL);
		element_av = (AV*)SvRV(*svp);
		elements = av_len(element_av) + 1;
		for(i = 0; i < elements; i ++) {
		    if((svp = av_fetch(element_av, i, FALSE))) {
			str = slurm_xstrdup((char*)SvPV_nolen(*svp));
			slurm_list_append(assoc_cond->cluster_list, str);
		    } else {
			Perl_warn(aTHX_ "error fetching cluster from cluster_list");
			return -1;
		    }
		}
	    } else {
		Perl_warn(aTHX_ "cluster_list of association_cond is not an array reference");
		return -1;
	    }
	}

	if((svp = hv_fetch(hv, "fairshare_list", strlen("fairshare_list"), FALSE))) {
	    if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		assoc_cond->fairshare_list = slurm_list_create(NULL);
		element_av = (AV*)SvRV(*svp);
		elements = av_len(element_av) + 1;
		for(i = 0; i < elements; i ++) {
		    if((svp = av_fetch(element_av, i, FALSE))) {
			str = slurm_xstrdup((char*)SvPV_nolen(*svp));
			slurm_list_append(assoc_cond->fairshare_list, str);
		    } else {
			Perl_warn(aTHX_ "error fetching fairshare from fairshare_list");
			return -1;
		    }
		}
	    } else {
		Perl_warn(aTHX_ "fairshare_list of association_cond is not an array reference");
		return -1;
	    }
	}

	if((svp = hv_fetch(hv, "grp_cpu_mins_list", strlen("grp_cpu_mins_list"), FALSE))) {
	    if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		assoc_cond->grp_cpu_mins_list = slurm_list_create(NULL);
		element_av = (AV*)SvRV(*svp);
		elements = av_len(element_av) + 1;
		for(i = 0; i < elements; i ++) {
		    if((svp = av_fetch(element_av, i, FALSE))) {
			str = slurm_xstrdup((char*)SvPV_nolen(*svp));
			slurm_list_append(assoc_cond->grp_cpu_mins_list, str);
		    } else {
			Perl_warn(aTHX_ "error fetching grp_cpu_mins from grp_cpu_mins_list");
			return -1;
		    }
		}
	    } else {
		Perl_warn(aTHX_ "grp_cpu_mins_list of association_cond is not an array reference");
		return -1;
	    }
	}

	if((svp = hv_fetch(hv, "grp_cpus_list", strlen("grp_cpus_list"), FALSE))) {
	    if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		assoc_cond->grp_cpus_list = slurm_list_create(NULL);
		element_av = (AV*)SvRV(*svp);
		elements = av_len(element_av) + 1;
		for(i = 0; i < elements; i ++) {
		    if((svp = av_fetch(element_av, i, FALSE))) {
			str = slurm_xstrdup((char*)SvPV_nolen(*svp));
			slurm_list_append(assoc_cond->grp_cpus_list, str);
		    } else {
			Perl_warn(aTHX_ "error fetching grp_cpus from grp_cpus_list");
			return -1;
		    }
		}
	    } else {
		Perl_warn(aTHX_ "grp_cpus_list of association_cond is not an array reference");
		return -1;
	    }
	}

	if((svp = hv_fetch(hv, "grp_jobs_list", strlen("grp_jobs_list"), FALSE))) {
	    if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		assoc_cond->grp_jobs_list = slurm_list_create(NULL);
		element_av = (AV*)SvRV(*svp);
		elements = av_len(element_av) + 1;
		for(i = 0; i < elements; i ++) {
		    if((svp = av_fetch(element_av, i, FALSE))) {
			str = slurm_xstrdup((char*)SvPV_nolen(*svp));
			slurm_list_append(assoc_cond->grp_jobs_list, str);
		    } else {
			Perl_warn(aTHX_ "error fetching grp_jobs from grp_jobs_list");
			return -1;
		    }
		}
	    } else {
		Perl_warn(aTHX_ "grp_jobs_list of association_cond is not an array reference");
		return -1;
	    }
	}

	if((svp = hv_fetch(hv, "grp_nodes_list", strlen("grp_nodes_list"), FALSE))) {
	    if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		assoc_cond->grp_nodes_list = slurm_list_create(NULL);
		element_av = (AV*)SvRV(*svp);
		elements = av_len(element_av) + 1;
		for(i = 0; i < elements; i ++) {
		    if((svp = av_fetch(element_av, i, FALSE))) {
			str = slurm_xstrdup((char*)SvPV_nolen(*svp));
			slurm_list_append(assoc_cond->grp_nodes_list, str);
		    } else {
			Perl_warn(aTHX_ "error fetching grp_nodes from grp_nodes_list");
			return -1;
		    }
		}
	    } else {
		Perl_warn(aTHX_ "grp_nodes_list of association_cond is not an array reference");
		return -1;
	    }
	}

	if((svp = hv_fetch(hv, "grp_submit_jobs_list", strlen("grp_submit_jobs_list"), FALSE))) {
	    if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		assoc_cond->grp_submit_jobs_list = slurm_list_create(NULL);
		element_av = (AV*)SvRV(*svp);
		elements = av_len(element_av) + 1;
		for(i = 0; i < elements; i ++) {
		    if((svp = av_fetch(element_av, i, FALSE))) {
			str = slurm_xstrdup((char*)SvPV_nolen(*svp));
			slurm_list_append(assoc_cond->grp_submit_jobs_list, str);
		    } else {
			Perl_warn(aTHX_ "error fetching grp_submit_jobs from grp_submit_jobs_list");
			return -1;
		    }
		}
	    } else {
		Perl_warn(aTHX_ "grp_submit_jobs_list of association_cond is not an array reference");
		return -1;
	    }
	}

	if((svp = hv_fetch(hv, "grp_wall_list", strlen("grp_wall_list"), FALSE))) {
	    if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		assoc_cond->grp_wall_list = slurm_list_create(NULL);
		element_av = (AV*)SvRV(*svp);
		elements = av_len(element_av) + 1;
		for(i = 0; i < elements; i ++) {
		    if((svp = av_fetch(element_av, i, FALSE))) {
			str = slurm_xstrdup((char*)SvPV_nolen(*svp));
			slurm_list_append(assoc_cond->grp_wall_list, str);
		    } else {
			Perl_warn(aTHX_ "error fetching grp_wall from grp_wall_list");
			return -1;
		    }
		}
	    } else {
		Perl_warn(aTHX_ "grp_wall_list of association_cond is not an array reference");
		return -1;
	    }
	}

	if((svp = hv_fetch(hv, "id_list", strlen("id_list"), FALSE))) {
	    if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		assoc_cond->id_list = slurm_list_create(NULL);
		element_av = (AV*)SvRV(*svp);
		elements = av_len(element_av) + 1;
		for(i = 0; i < elements; i ++) {
		    if((svp = av_fetch(element_av, i, FALSE))) {
			str = slurm_xstrdup((char*)SvPV_nolen(*svp));
			slurm_list_append(assoc_cond->id_list, str);
		    } else {
			Perl_warn(aTHX_ "error fetching id from id_list");
			return -1;
		    }
		}
	    } else {
		Perl_warn(aTHX_ "id_list of association_cond is not an array reference");
		return -1;
	    }
	}

	if((svp = hv_fetch(hv, "max_cpu_mins_pj_list", strlen("max_cpu_mins_pj_list"), FALSE))) {
	    if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		assoc_cond->max_cpu_mins_pj_list = slurm_list_create(NULL);
		element_av = (AV*)SvRV(*svp);
		elements = av_len(element_av) + 1;
		for(i = 0; i < elements; i ++) {
		    if((svp = av_fetch(element_av, i, FALSE))) {
			str = slurm_xstrdup((char*)SvPV_nolen(*svp));
			slurm_list_append(assoc_cond->max_cpu_mins_pj_list, str);
		    } else {
			Perl_warn(aTHX_ "error fetching max_cpu_mins_pj from max_cpu_mins_pj_list");
			return -1;
		    }
		}
	    } else {
		Perl_warn(aTHX_ "max_cpu_mins_pj_list of association_cond is not an array reference");
		return -1;
	    }
	}

	if((svp = hv_fetch(hv, "max_cpus_pj_list", strlen("max_cpus_pj_list"), FALSE))) {
	    if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		assoc_cond->max_cpus_pj_list = slurm_list_create(NULL);
		element_av = (AV*)SvRV(*svp);
		elements = av_len(element_av) + 1;
		for(i = 0; i < elements; i ++) {
		    if((svp = av_fetch(element_av, i, FALSE))) {
			str = slurm_xstrdup((char*)SvPV_nolen(*svp));
			slurm_list_append(assoc_cond->max_cpus_pj_list, str);
		    } else {
			Perl_warn(aTHX_ "error fetching max_cpus_pj from max_cpus_pj_list");
			return -1;
		    }
		}
	    } else {
		Perl_warn(aTHX_ "max_cpus_pj_list of association_cond is not an array reference");
		return -1;
	    }
	}

	if((svp = hv_fetch(hv, "max_jobs_list", strlen("max_jobs_list"), FALSE))) {
	    if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		assoc_cond->max_jobs_list = slurm_list_create(NULL);
		element_av = (AV*)SvRV(*svp);
		elements = av_len(element_av) + 1;
		for(i = 0; i < elements; i ++) {
		    if((svp = av_fetch(element_av, i, FALSE))) {
			str = slurm_xstrdup((char*)SvPV_nolen(*svp));
			slurm_list_append(assoc_cond->max_jobs_list, str);
		    } else {
			Perl_warn(aTHX_ "error fetching max_jobs from max_jobs_list");
			return -1;
		    }
		}
	    } else {
		Perl_warn(aTHX_ "max_jobs_list of association_cond is not an array reference");
		return -1;
	    }
	}

	if((svp = hv_fetch(hv, "max_nodes_pj_list", strlen("max_nodes_pj_list"), FALSE))) {
	    if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		assoc_cond->max_nodes_pj_list = slurm_list_create(NULL);
		element_av = (AV*)SvRV(*svp);
		elements = av_len(element_av) + 1;
		for(i = 0; i < elements; i ++) {
		    if((svp = av_fetch(element_av, i, FALSE))) {
			str = slurm_xstrdup((char*)SvPV_nolen(*svp));
			slurm_list_append(assoc_cond->max_nodes_pj_list, str);
		    } else {
			Perl_warn(aTHX_ "error fetching max_nodes_pj from max_nodes_pj_list");
			return -1;
		    }
		}
	    } else {
		Perl_warn(aTHX_ "max_nodes_pj_list of association_cond is not an array reference");
		return -1;
	    }
	}

	if((svp = hv_fetch(hv, "max_submit_jobs_list", strlen("max_submit_jobs_list"), FALSE))) {
	    if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		assoc_cond->max_submit_jobs_list = slurm_list_create(NULL);
		element_av = (AV*)SvRV(*svp);
		elements = av_len(element_av) + 1;
		for(i = 0; i < elements; i ++) {
		    if((svp = av_fetch(element_av, i, FALSE))) {
			str = slurm_xstrdup((char*)SvPV_nolen(*svp));
			slurm_list_append(assoc_cond->max_submit_jobs_list, str);
		    } else {
			Perl_warn(aTHX_ "error fetching max_submit_jobs from max_submit_jobs_list");
			return -1;
		    }
		}
	    } else {
		Perl_warn(aTHX_ "max_submit_jobs_list of association_cond is not an array reference");
		return -1;
	    }
	}

	if((svp = hv_fetch(hv, "max_wall_pj_list", strlen("max_wall_pj_list"), FALSE))) {
	    if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		assoc_cond->max_wall_pj_list = slurm_list_create(NULL);
		element_av = (AV*)SvRV(*svp);
		elements = av_len(element_av) + 1;
		for(i = 0; i < elements; i ++) {
		    if((svp = av_fetch(element_av, i, FALSE))) {
			str = slurm_xstrdup((char*)SvPV_nolen(*svp));
			slurm_list_append(assoc_cond->max_wall_pj_list, str);
		    } else {
			Perl_warn(aTHX_ "error fetching max_wall_pj from max_wall_pj_list");
			return -1;
		    }
		}
	    } else {
		Perl_warn(aTHX_ "max_wall_pj_list of association_cond is not an array reference");
		return -1;
	    }
	}

	if((svp = hv_fetch(hv, "partition_list", strlen("partition_list"), FALSE))) {
	    if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		assoc_cond->partition_list = slurm_list_create(NULL);
		element_av = (AV*)SvRV(*svp);
		elements = av_len(element_av) + 1;
		for(i = 0; i < elements; i ++) {
		    if((svp = av_fetch(element_av, i, FALSE))) {
			str = slurm_xstrdup((char*)SvPV_nolen(*svp));
			slurm_list_append(assoc_cond->partition_list, str);
		    } else {
			Perl_warn(aTHX_ "error fetching partition from partition_list");
			return -1;
		    }
		}
	    } else {
		Perl_warn(aTHX_ "partition_list of association_cond is not an array reference");
		return -1;
	    }
	}

	if((svp = hv_fetch(hv, "parent_acct_list", strlen("parent_acct_list"), FALSE))) {
	    if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		assoc_cond->parent_acct_list = slurm_list_create(NULL);
		element_av = (AV*)SvRV(*svp);
		elements = av_len(element_av) + 1;
		for(i = 0; i < elements; i ++) {
		    if((svp = av_fetch(element_av, i, FALSE))) {
			str = slurm_xstrdup((char*)SvPV_nolen(*svp));
			slurm_list_append(assoc_cond->parent_acct_list, str);
		    } else {
			Perl_warn(aTHX_ "error fetching parent_acct from parent_acct_list");
			return -1;
		    }
		}
	    } else {
		Perl_warn(aTHX_ "parent_acct_list of association_cond is not an array reference");
		return -1;
	    }
	}

	if((svp = hv_fetch(hv, "qos_list", strlen("qos_list"), FALSE))) {
	    if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		assoc_cond->qos_list = slurm_list_create(NULL);
		element_av = (AV*)SvRV(*svp);
		elements = av_len(element_av) + 1;
		for(i = 0; i < elements; i ++) {
		    if((svp = av_fetch(element_av, i, FALSE))) {
			str = slurm_xstrdup((char*)SvPV_nolen(*svp));
			slurm_list_append(assoc_cond->qos_list, str);
		    } else {
			Perl_warn(aTHX_ "error fetching qos from qos_list");
			return -1;
		    }
		}
	    } else {
		Perl_warn(aTHX_ "qos_list of association_cond is not an array reference");
		return -1;
	    }
	}

	if((svp = hv_fetch(hv, "user_list", strlen("user_list"), FALSE))) {
	    if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) {
		assoc_cond->user_list = slurm_list_create(NULL);
		element_av = (AV*)SvRV(*svp);
		elements = av_len(element_av) + 1;
		for(i = 0; i < elements; i ++) {
		    if((svp = av_fetch(element_av, i, FALSE))) {
			str = slurm_xstrdup((char*)SvPV_nolen(*svp));
			slurm_list_append(assoc_cond->user_list, str);
		    } else {
			Perl_warn(aTHX_ "error fetching user from user_list");
			return -1;
		    }
		}
	    } else {
		Perl_warn(aTHX_ "user_list of association_cond is not an array reference");
		return -1;
	    }
	}

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
	AV * acc_av = (AV *)sv_2mortal((SV *)newAV());
	HV * rh;
	ListIterator itr = NULL;
	slurmdb_cluster_accounting_rec_t* ar = NULL;

	if (rec->accounting_list) {
	    itr = slurm_list_iterator_create(rec->accounting_list);
	    while ((ar = slurm_list_next(itr))) {
		rh = (HV *)sv_2mortal((SV *)newHV());
		if (cluster_accounting_rec_to_hv(ar, rh) < 0) {
		    Perl_warn(aTHX_ "Failed to convert a cluster_accounting_rec to a hv");
		    return -1;
		} else {
		    av_push(acc_av, newRV((SV *)rh));
		}
	    }
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

int
report_assoc_rec_to_hv(slurmdb_report_assoc_rec_t *ar, HV* hv)
{
	STORE_FIELD(hv, ar, acct, charp);
	STORE_FIELD(hv, ar, cluster, charp);
	STORE_FIELD(hv, ar, cpu_secs, uint64_t);
	STORE_FIELD(hv, ar, parent_acct, charp);
	STORE_FIELD(hv, ar, user, charp);

	return 0;
}

int
report_user_rec_to_hv(slurmdb_report_user_rec_t *rec, HV* hv)
{
	AV * acc_av = (AV *)sv_2mortal((SV *)newAV());
	AV * char_av = (AV *)sv_2mortal((SV *)newAV());
	HV * rh;
	char* acct;
	slurmdb_report_assoc_rec_t *ar = NULL;
	ListIterator itr = NULL;

	if (rec->acct_list) {
	    itr = slurm_list_iterator_create(rec->acct_list);
	    while ((acct = slurm_list_next(itr))) {
		av_push(char_av, newRV(newSVpv(acct, strlen(acct))));
	    }
	}
	hv_store_sv(hv, "acct_list", newRV((SV*)char_av));

	if (rec->assoc_list) {
	    itr = slurm_list_iterator_create(rec->assoc_list);
	    while ((ar = slurm_list_next(itr))) {
		rh = (HV *)sv_2mortal((SV *)newHV());
		if (report_assoc_rec_to_hv(ar, rh) < 0) {
		    Perl_warn(aTHX_ "Failed to convert a report_assoc_rec to a hv");
		    return -1;
		} else {
		    av_push(acc_av, newRV((SV *)rh));
		}
	    }
	}
	hv_store_sv(hv, "assoc_list", newRV((SV*)acc_av));

	STORE_FIELD(hv, rec, acct, charp);
	STORE_FIELD(hv, rec, cpu_secs, uint64_t);
	STORE_FIELD(hv, rec, name, charp);
	STORE_FIELD(hv, rec, uid, uid_t);

	return 0;
}

int
report_cluster_rec_to_hv(slurmdb_report_cluster_rec_t* rec, HV* hv)
{
	AV * acc_av = (AV *)sv_2mortal((SV *)newAV());
	AV * usr_av = (AV *)sv_2mortal((SV *)newAV());
	HV * rh;
	slurmdb_report_assoc_rec_t* ar = NULL;
	slurmdb_report_user_rec_t* ur = NULL;
	ListIterator itr = NULL;

	if (rec->assoc_list) {
	    itr = slurm_list_iterator_create(rec->assoc_list);
	    while ((ar = slurm_list_next(itr))) {
		rh = (HV *)sv_2mortal((SV *)newHV());
		if (report_assoc_rec_to_hv(ar, rh) < 0) {
		    Perl_warn(aTHX_ "Failed to convert a report_assoc_rec to a hv");
		    return -1;
		} else {
		    av_push(acc_av, newRV((SV *)rh));
		}
	    }
	}
	hv_store_sv(hv, "assoc_list", newRV((SV*)acc_av));

	STORE_FIELD(hv, rec, cpu_count, uint32_t);
	STORE_FIELD(hv, rec, cpu_secs, uint64_t );
	STORE_FIELD(hv, rec, name, charp);

	if (rec->user_list) {
	    itr = slurm_list_iterator_create(rec->user_list);
	    while ((ur = slurm_list_next(itr))) {
		rh = (HV *)sv_2mortal((SV *)newHV());
		if (report_user_rec_to_hv(ur, rh) < 0) {
		    Perl_warn(aTHX_ "Failed to convert a report_user_rec to a hv");
		    return -1;
		} else {
		    av_push(usr_av, newRV((SV *)rh));
		}
	    }
	}
	hv_store_sv(hv, "user_list", newRV((SV*)usr_av));

	return 0;
}

int
report_cluster_rec_list_to_av(List list, AV* av)
{
	HV * rh;
	ListIterator itr = NULL;
	slurmdb_report_cluster_rec_t* rec = NULL;

	if (list) {
	    itr = slurm_list_iterator_create(list);
	    while ((rec = slurm_list_next(itr))) {
		rh = (HV *)sv_2mortal((SV *)newHV());
		if (report_cluster_rec_to_hv(rec, rh) < 0) {
		    Perl_warn(aTHX_ "Failed to convert a report_cluster_rec to a hv");
		    return -1;
		} else {
		    av_push(av, newRV((SV *)rh));
		}
	    }
	}

	return 0;
}

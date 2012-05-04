/*
 * cluster.c - convert data between cluster related messages and perl HVs
 */

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>

#include <slurm/slurmdb.h>
#include "slurmdb-perl.h"

extern char* slurm_xstrdup(const char* str);
extern int slurmdb_report_set_start_end_time(time_t* start, time_t* end);

int
av_to_cluster_grouping_list(AV* av, List grouping_list)
{
    SV**   svp;
    char*  str = NULL;
    int    i, elements = 0;

    elements = av_len(av) + 1;
    for(i = 0; i < elements; i ++) {
	if((svp = av_fetch(av, i, FALSE))) {
	    str = slurm_xstrdup((char*)SvPV_nolen(*svp));
	    slurm_list_append(grouping_list, str);
	} else {
	    Perl_warn(aTHX_ "error fetching group from grouping list");
	    return -1;
	}
    }
    return 0;
}

int
hv_to_assoc_cond(HV* hv, slurmdb_association_cond_t* assoc_cond)
{
    AV*    element_av;
    SV**   svp;
    char*  str = NULL;
    int    i, elements = 0;
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

    FETCH_FIELD(hv, assoc_cond, with_usage,            uint16_t, FALSE);
    FETCH_FIELD(hv, assoc_cond, with_deleted,          uint16_t, FALSE);
    FETCH_FIELD(hv, assoc_cond, with_raw_qos,          uint16_t, FALSE);
    FETCH_FIELD(hv, assoc_cond, with_sub_accts,        uint16_t, FALSE);
    FETCH_FIELD(hv, assoc_cond, without_parent_info,   uint16_t, FALSE);
    FETCH_FIELD(hv, assoc_cond, without_parent_limits, uint16_t, FALSE);

    FETCH_LIST_FIELD(hv, assoc_cond, acct_list);
    FETCH_LIST_FIELD(hv, assoc_cond, cluster_list);
    FETCH_LIST_FIELD(hv, assoc_cond, def_qos_id_list);
    FETCH_LIST_FIELD(hv, assoc_cond, fairshare_list);
    FETCH_LIST_FIELD(hv, assoc_cond, grp_cpu_mins_list);
    FETCH_LIST_FIELD(hv, assoc_cond, grp_cpu_run_mins_list);
    FETCH_LIST_FIELD(hv, assoc_cond, grp_cpus_list);
    FETCH_LIST_FIELD(hv, assoc_cond, grp_jobs_list);
    FETCH_LIST_FIELD(hv, assoc_cond, grp_mem_list);
    FETCH_LIST_FIELD(hv, assoc_cond, grp_nodes_list);
    FETCH_LIST_FIELD(hv, assoc_cond, grp_submit_jobs_list);
    FETCH_LIST_FIELD(hv, assoc_cond, grp_wall_list);
    FETCH_LIST_FIELD(hv, assoc_cond, id_list);
    FETCH_LIST_FIELD(hv, assoc_cond, max_cpu_mins_pj_list);
    FETCH_LIST_FIELD(hv, assoc_cond, max_cpu_run_mins_list);
    FETCH_LIST_FIELD(hv, assoc_cond, max_cpus_pj_list);
    FETCH_LIST_FIELD(hv, assoc_cond, max_jobs_list);
    FETCH_LIST_FIELD(hv, assoc_cond, max_nodes_pj_list);
    FETCH_LIST_FIELD(hv, assoc_cond, max_submit_jobs_list);
    FETCH_LIST_FIELD(hv, assoc_cond, max_wall_pj_list);
    FETCH_LIST_FIELD(hv, assoc_cond, parent_acct_list);
    FETCH_LIST_FIELD(hv, assoc_cond, partition_list);
    FETCH_LIST_FIELD(hv, assoc_cond, qos_list);
    FETCH_LIST_FIELD(hv, assoc_cond, user_list);

    return 0;
}

int
hv_to_cluster_cond(HV* hv, slurmdb_cluster_cond_t* cluster_cond)
{
    AV*    element_av;
    char*  str = NULL;
    int    i, elements = 0;

    cluster_cond->classification = SLURMDB_CLASS_NONE;
    cluster_cond->usage_end = 0;
    cluster_cond->usage_start = 0;
    cluster_cond->with_deleted = 1;
    cluster_cond->with_usage = 1;

    FETCH_FIELD(hv, cluster_cond, classification, uint16_t, FALSE);
    FETCH_FIELD(hv, cluster_cond, flags,          uint32_t, FALSE);
    FETCH_FIELD(hv, cluster_cond, usage_end,      time_t , FALSE);
    FETCH_FIELD(hv, cluster_cond, usage_start,    time_t , FALSE);
    FETCH_FIELD(hv, cluster_cond, with_deleted,   uint16_t, FALSE);
    FETCH_FIELD(hv, cluster_cond, with_usage,     uint16_t, FALSE);

    FETCH_LIST_FIELD(hv, cluster_cond, cluster_list);
    FETCH_LIST_FIELD(hv, cluster_cond, plugin_id_select_list);
    FETCH_LIST_FIELD(hv, cluster_cond, rpc_version_list);

    return 0;
}

int
hv_to_job_cond(HV* hv, slurmdb_job_cond_t* job_cond)
{
    AV*    element_av;
    SV**   svp;
    char*  str = NULL;
    int    i, elements = 0;
    time_t start_time = 0;
    time_t end_time = 0;

    if ( (svp = hv_fetch (hv, "usage_start", strlen("usage_start"), FALSE)) ) {
	start_time = (time_t) (SV2time_t(*svp));
    }
    if ( (svp = hv_fetch (hv, "usage_end", strlen("usage_end"), FALSE)) ) {
	end_time = (time_t) (SV2time_t(*svp));
    }
    slurmdb_report_set_start_end_time(&start_time, &end_time);
    job_cond->usage_start = start_time;
    job_cond->usage_end = end_time;

    job_cond->cpus_max = 0;
    job_cond->cpus_min = 0;
    job_cond->duplicates = 0;
    job_cond->nodes_max = 0;
    job_cond->nodes_min = 0;
    job_cond->used_nodes = NULL;
    job_cond->without_steps = 0;
    job_cond->without_usage_truncation = 0;

    FETCH_FIELD(hv, job_cond, cpus_max,                 uint32_t, FALSE);
    FETCH_FIELD(hv, job_cond, cpus_min,                 uint32_t, FALSE);
    FETCH_FIELD(hv, job_cond, duplicates,               uint16_t, FALSE);
    FETCH_FIELD(hv, job_cond, exitcode,                 int32_t, FALSE);
    FETCH_FIELD(hv, job_cond, nodes_max,                uint32_t, FALSE);
    FETCH_FIELD(hv, job_cond, nodes_min,                uint32_t, FALSE);
    FETCH_FIELD(hv, job_cond, timelimit_max,            uint32_t, FALSE);
    FETCH_FIELD(hv, job_cond, timelimit_min,            uint32_t, FALSE);
    FETCH_FIELD(hv, job_cond, usage_end,                time_t, FALSE);
    FETCH_FIELD(hv, job_cond, usage_start,              time_t, FALSE);
    FETCH_FIELD(hv, job_cond, used_nodes,               charp, FALSE);
    FETCH_FIELD(hv, job_cond, without_steps,            uint16_t, FALSE);
    FETCH_FIELD(hv, job_cond, without_usage_truncation, uint16_t, FALSE);

    FETCH_LIST_FIELD(hv, job_cond, acct_list);
    FETCH_LIST_FIELD(hv, job_cond, associd_list);
    FETCH_LIST_FIELD(hv, job_cond, cluster_list);
    FETCH_LIST_FIELD(hv, job_cond, groupid_list);
    FETCH_LIST_FIELD(hv, job_cond, jobname_list);
    FETCH_LIST_FIELD(hv, job_cond, partition_list);
    FETCH_LIST_FIELD(hv, job_cond, qos_list);
    FETCH_LIST_FIELD(hv, job_cond, resv_list);
    FETCH_LIST_FIELD(hv, job_cond, resvid_list);
    FETCH_LIST_FIELD(hv, job_cond, state_list);
    FETCH_LIST_FIELD(hv, job_cond, step_list);
    FETCH_LIST_FIELD(hv, job_cond, userid_list);
    FETCH_LIST_FIELD(hv, job_cond, wckey_list);

    return 0;
}

int
hv_to_user_cond(HV* hv, slurmdb_user_cond_t* user_cond)
{
    AV*    element_av;
    SV**   svp;
    char*  str = NULL;
    int    i, elements = 0;

    user_cond->admin_level = 0;
    user_cond->with_assocs = 1;
    user_cond->with_coords = 0;
    user_cond->with_deleted = 1;
    user_cond->with_wckeys = 0;

    FETCH_FIELD(hv, user_cond, admin_level,  uint16_t, FALSE);
    FETCH_FIELD(hv, user_cond, with_assocs,  uint16_t, FALSE);
    FETCH_FIELD(hv, user_cond, with_coords,  uint16_t, FALSE);
    FETCH_FIELD(hv, user_cond, with_deleted, uint16_t, FALSE);
    FETCH_FIELD(hv, user_cond, with_wckeys,  uint16_t, FALSE);

    if ( (svp = hv_fetch (hv, "assoc_cond", strlen("assoc_cond"), FALSE)) ) {
	if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVHV) {
	    HV* element_hv = (HV*)SvRV(*svp);
	    hv_to_assoc_cond(element_hv, user_cond->assoc_cond);
	} else {
	    Perl_warn(aTHX_ "assoc_cond val is not an hash value reference");
	    return -1;
	}
    }

    FETCH_LIST_FIELD(hv, user_cond, def_acct_list);
    FETCH_LIST_FIELD(hv, user_cond, def_wckey_list);

    return 0;
}

int
report_job_grouping_to_hv(slurmdb_report_job_grouping_t* rec, HV* hv)
{
    /* FIX ME: include the job list here (is is not NULL, as
     * previously thought) */
    STORE_FIELD(hv, rec, min_size, uint32_t);
    STORE_FIELD(hv, rec, max_size, uint32_t);
    STORE_FIELD(hv, rec, count,    uint32_t);
    STORE_FIELD(hv, rec, cpu_secs, uint64_t);

    return 0;
}

int
report_acct_grouping_to_hv(slurmdb_report_acct_grouping_t* rec, HV* hv)
{
    AV* group_av = (AV*)sv_2mortal((SV*)newAV());
    HV* rh;
    slurmdb_report_job_grouping_t* jgr = NULL;
    ListIterator itr = NULL;

    STORE_FIELD(hv, rec, acct,     charp);
    STORE_FIELD(hv, rec, count,    uint32_t);
    STORE_FIELD(hv, rec, cpu_secs, uint64_t);
    STORE_FIELD(hv, rec, lft,      uint32_t);
    STORE_FIELD(hv, rec, rgt,      uint32_t);

    if (rec->groups) {
	itr = slurm_list_iterator_create(rec->groups);
	while ((jgr = slurm_list_next(itr))) {
	    rh = (HV*)sv_2mortal((SV*)newHV());
	    if (report_job_grouping_to_hv(jgr, rh) < 0) {
		Perl_warn(aTHX_ "Failed to convert a report_job_grouping to a hv");
		return -1;
	    } else {
		av_push(group_av, newRV((SV*)rh));
	    }
	}
    }
    hv_store_sv(hv, "groups", newRV((SV*)group_av));

    return 0;
}

int
report_cluster_grouping_to_hv(slurmdb_report_cluster_grouping_t* rec, HV* hv)
{
    AV* acct_av = (AV*)sv_2mortal((SV*)newAV());
    HV* rh;
    slurmdb_report_acct_grouping_t* agr = NULL;
    ListIterator itr = NULL;

    STORE_FIELD(hv, rec, cluster,  charp);
    STORE_FIELD(hv, rec, count,    uint32_t);
    STORE_FIELD(hv, rec, cpu_secs, uint64_t);

    if (rec->acct_list) {
	itr = slurm_list_iterator_create(rec->acct_list);
	while ((agr = slurm_list_next(itr))) {
	    rh = (HV*)sv_2mortal((SV*)newHV());
	    if (report_acct_grouping_to_hv(agr, rh) < 0) {
		Perl_warn(aTHX_ "Failed to convert a report_acct_grouping to a hv");
		return -1;
	    } else {
		av_push(acct_av, newRV((SV*)rh));
	    }
	}
    }
    hv_store_sv(hv, "acct_list", newRV((SV*)acct_av));

    return 0;
}

int
cluster_grouping_list_to_av(List list, AV* av)
{
    HV* rh;
    ListIterator itr = NULL;
    slurmdb_report_cluster_grouping_t* rec = NULL;

    if (list) {
	itr = slurm_list_iterator_create(list);
	while ((rec = slurm_list_next(itr))) {
	    rh = (HV*)sv_2mortal((SV*)newHV());
	    if (report_cluster_grouping_to_hv(rec, rh) < 0) {
		Perl_warn(aTHX_ "Failed to convert a report_cluster_grouping to a hv");
		return -1;
	    } else {
		av_push(av, newRV((SV*)rh));
	    }
	}
    }

    return 0;
}

int
cluster_accounting_rec_to_hv(slurmdb_cluster_accounting_rec_t* ar, HV* hv)
{
    STORE_FIELD(hv, ar, alloc_secs,   uint64_t);
    STORE_FIELD(hv, ar, cpu_count,    uint32_t);
    STORE_FIELD(hv, ar, down_secs,    uint64_t);
    STORE_FIELD(hv, ar, idle_secs,    uint64_t);
    STORE_FIELD(hv, ar, over_secs,    uint64_t);
    STORE_FIELD(hv, ar, pdown_secs,   uint64_t);
    STORE_FIELD(hv, ar, period_start, time_t);
    STORE_FIELD(hv, ar, resv_secs,    uint64_t);

    return 0;
}

int
cluster_rec_to_hv(slurmdb_cluster_rec_t* rec, HV* hv)
{
    AV* acc_av = (AV*)sv_2mortal((SV*)newAV());
    HV* rh;
    ListIterator itr = NULL;
    slurmdb_cluster_accounting_rec_t* ar = NULL;

    if (rec->accounting_list) {
	itr = slurm_list_iterator_create(rec->accounting_list);
	while ((ar = slurm_list_next(itr))) {
	    rh = (HV*)sv_2mortal((SV*)newHV());
	    if (cluster_accounting_rec_to_hv(ar, rh) < 0) {
		Perl_warn(aTHX_ "Failed to convert a cluster_accounting_rec to a hv");
		return -1;
	    } else {
		av_push(acc_av, newRV((SV*)rh));
	    }
	}
    }
    hv_store_sv(hv, "accounting_list", newRV((SV*)acc_av));

    STORE_FIELD(hv, rec, classification, uint16_t);
    STORE_FIELD(hv, rec, control_host,   charp);
    STORE_FIELD(hv, rec, control_port,   uint32_t);
    STORE_FIELD(hv, rec, cpu_count,      uint32_t);
    STORE_FIELD(hv, rec, dimensions,     uint16_t);
    STORE_FIELD(hv, rec, flags,          uint32_t);
    STORE_FIELD(hv, rec, name,           charp);
    STORE_FIELD(hv, rec, nodes,          charp);
    STORE_FIELD(hv, rec, plugin_id_select, uint32_t);
    /* slurmdb_association_rec_t* root_assoc; */
    STORE_FIELD(hv, rec, rpc_version,    uint16_t);

    return 0;
}

int
report_assoc_rec_to_hv(slurmdb_report_assoc_rec_t* ar, HV* hv)
{
    STORE_FIELD(hv, ar, acct,        charp);
    STORE_FIELD(hv, ar, cluster,     charp);
    STORE_FIELD(hv, ar, cpu_secs,    uint64_t);
    STORE_FIELD(hv, ar, parent_acct, charp);
    STORE_FIELD(hv, ar, user,        charp);

    return 0;
}

int
report_cluster_rec_to_hv(slurmdb_report_cluster_rec_t* rec, HV* hv)
{
    AV* acc_av = (AV*)sv_2mortal((SV*)newAV());
    AV* usr_av = (AV*)sv_2mortal((SV*)newAV());
    HV* rh;
    slurmdb_report_assoc_rec_t* ar = NULL;
    slurmdb_report_user_rec_t* ur = NULL;
    ListIterator itr = NULL;

    if (rec->assoc_list) {
	itr = slurm_list_iterator_create(rec->assoc_list);
	while ((ar = slurm_list_next(itr))) {
	    rh = (HV*)sv_2mortal((SV*)newHV());
	    if (report_assoc_rec_to_hv(ar, rh) < 0) {
		Perl_warn(aTHX_ "Failed to convert a report_assoc_rec to a hv");
		return -1;
	    } else {
		av_push(acc_av, newRV((SV*)rh));
	    }
	}
    }
    hv_store_sv(hv, "assoc_list", newRV((SV*)acc_av));

    STORE_FIELD(hv, rec, cpu_count, uint32_t);
    STORE_FIELD(hv, rec, cpu_secs,  uint64_t);
    STORE_FIELD(hv, rec, name,      charp);

    if (rec->user_list) {
	itr = slurm_list_iterator_create(rec->user_list);
	while ((ur = slurm_list_next(itr))) {
	    rh = (HV*)sv_2mortal((SV*)newHV());
	    if (report_user_rec_to_hv(ur, rh) < 0) {
		Perl_warn(aTHX_ "Failed to convert a report_user_rec to a hv");
		return -1;
	    } else {
		av_push(usr_av, newRV((SV*)rh));
	    }
	}
    }
    hv_store_sv(hv, "user_list", newRV((SV*)usr_av));

    return 0;
}

int
report_cluster_rec_list_to_av(List list, AV* av)
{
    HV* rh;
    ListIterator itr = NULL;
    slurmdb_report_cluster_rec_t* rec = NULL;

    if (list) {
	itr = slurm_list_iterator_create(list);
	while ((rec = slurm_list_next(itr))) {
	    rh = (HV*)sv_2mortal((SV*)newHV());
	    if (report_cluster_rec_to_hv(rec, rh) < 0) {
		Perl_warn(aTHX_ "Failed to convert a report_cluster_rec to a hv");
		return -1;
	    } else {
		av_push(av, newRV((SV*)rh));
	    }
	}
    }

    return 0;
}

int
report_user_rec_to_hv(slurmdb_report_user_rec_t* rec, HV* hv)
{
    AV*   acc_av = (AV*)sv_2mortal((SV*)newAV());
    AV*   char_av = (AV*)sv_2mortal((SV*)newAV());
    HV*   rh;
    char* acct;
    slurmdb_report_assoc_rec_t* ar = NULL;
    ListIterator itr = NULL;

    if (rec->acct_list) {
	itr = slurm_list_iterator_create(rec->acct_list);
	while ((acct = slurm_list_next(itr))) {
	    av_push(char_av, newSVpv(acct, strlen(acct)));
	}
    }
    hv_store_sv(hv, "acct_list", newRV((SV*)char_av));

    if (rec->assoc_list) {
	itr = slurm_list_iterator_create(rec->assoc_list);
	while ((ar = slurm_list_next(itr))) {
	    rh = (HV*)sv_2mortal((SV*)newHV());
	    if (report_assoc_rec_to_hv(ar, rh) < 0) {
		Perl_warn(aTHX_ "Failed to convert a report_assoc_rec to a hv");
		return -1;
	    } else {
		av_push(acc_av, newRV((SV*)rh));
	    }
	}
    }
    hv_store_sv(hv, "assoc_list", newRV((SV*)acc_av));

    STORE_FIELD(hv, rec, acct,     charp);
    STORE_FIELD(hv, rec, cpu_secs, uint64_t);
    STORE_FIELD(hv, rec, name,     charp);
    STORE_FIELD(hv, rec, uid,      uid_t);

    return 0;
}

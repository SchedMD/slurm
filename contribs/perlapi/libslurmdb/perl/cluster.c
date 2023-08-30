/*
 * cluster.c - convert data between cluster related messages and perl HVs
 */

#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>

#undef VERSION /* MakeMaker defines VERSION to some version we don't care
		* about. The true version will be defined in config.h which is
		* included indirectly below.
		*/

#include <slurm/slurmdb.h>
#include "src/common/slurm_protocol_defs.h"
#include "slurmdb-perl.h"

extern char* slurm_xstrdup(const char* str);
extern int slurmdb_report_set_start_end_time(time_t* start, time_t* end);
extern char *slurmdb_get_qos_complete_str_bitstr(List qos_list, bitstr_t *valid_qos);

int
av_to_cluster_grouping_list(AV* av, List grouping_list)
{
    SV**   svp;
    char*  str = NULL;
    int    i, elements = 0;

    elements = av_len(av) + 1;
    for (i = 0; i < elements; i ++) {
	if ((svp = av_fetch(av, i, FALSE))) {
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
hv_to_assoc_cond(HV* hv, slurmdb_assoc_cond_t* assoc_cond)
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
    FETCH_LIST_FIELD(hv, assoc_cond, id_list);
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

    if ( (svp = hv_fetch (hv, "step_list", strlen("step_list"), FALSE)) ) {
	char *jobids = (char *) (SvPV_nolen(*svp));
	if (!job_cond->step_list)
	    job_cond->step_list =
		    slurm_list_create(slurm_destroy_selected_step);
	slurm_addto_step_list(job_cond->step_list, jobids);
    }
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
    job_cond->db_flags = SLURMDB_JOB_FLAG_NOTSET;
    job_cond->flags = 0;
    job_cond->nodes_max = 0;
    job_cond->nodes_min = 0;
    job_cond->used_nodes = NULL;

    FETCH_FIELD(hv, job_cond, cpus_max,                 uint32_t, FALSE);
    FETCH_FIELD(hv, job_cond, cpus_min,                 uint32_t, FALSE);
    FETCH_FIELD(hv, job_cond, db_flags,                 uint32_t, FALSE);
    FETCH_FIELD(hv, job_cond, flags,                    uint32_t, FALSE);
    FETCH_FIELD(hv, job_cond, exitcode,                 int32_t, FALSE);
    FETCH_FIELD(hv, job_cond, nodes_max,                uint32_t, FALSE);
    FETCH_FIELD(hv, job_cond, nodes_min,                uint32_t, FALSE);
    FETCH_FIELD(hv, job_cond, timelimit_max,            uint32_t, FALSE);
    FETCH_FIELD(hv, job_cond, timelimit_min,            uint32_t, FALSE);
    FETCH_FIELD(hv, job_cond, usage_end,                time_t, FALSE);
    FETCH_FIELD(hv, job_cond, usage_start,              time_t, FALSE);
    FETCH_FIELD(hv, job_cond, used_nodes,               charp, FALSE);

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
tres_rec_to_hv(slurmdb_tres_rec_t* rec, HV* hv)
{
	STORE_FIELD(hv, rec, alloc_secs, uint64_t);
	STORE_FIELD(hv, rec, rec_count,  uint32_t);
	STORE_FIELD(hv, rec, count,      uint64_t);
	STORE_FIELD(hv, rec, id,         uint32_t);
	STORE_FIELD(hv, rec, name,       charp);
	STORE_FIELD(hv, rec, type,       charp);

	return 0;
}

int
report_job_grouping_to_hv(slurmdb_report_job_grouping_t* rec, HV* hv)
{
    AV* my_av;
    HV* rh;
    slurmdb_tres_rec_t *tres_rec = NULL;
    ListIterator itr = NULL;

    /* FIX ME: include the job list here (is is not NULL, as
     * previously thought) */
    STORE_FIELD(hv, rec, min_size, uint32_t);
    STORE_FIELD(hv, rec, max_size, uint32_t);
    STORE_FIELD(hv, rec, count,    uint32_t);

    my_av = (AV*)sv_2mortal((SV*)newAV());
    if (rec->tres_list) {
	itr = slurm_list_iterator_create(rec->tres_list);
	while ((tres_rec = slurm_list_next(itr))) {
	    rh = (HV*)sv_2mortal((SV*)newHV());
	    if (tres_rec_to_hv(tres_rec, rh) < 0) {
		Perl_warn(aTHX_ "Failed to convert a tres_rec to a hv");
		slurm_list_iterator_destroy(itr);
		return -1;
	    } else {
		av_push(my_av, newRV((SV*)rh));
	    }
	}
	slurm_list_iterator_destroy(itr);
    }
    hv_store_sv(hv, "tres_list", newRV((SV*)my_av));

    return 0;
}

int
report_acct_grouping_to_hv(slurmdb_report_acct_grouping_t* rec, HV* hv)
{
    AV* my_av;
    HV* rh;
    slurmdb_report_job_grouping_t* jgr = NULL;
    slurmdb_tres_rec_t *tres_rec = NULL;
    ListIterator itr = NULL;

    STORE_FIELD(hv, rec, acct,     charp);
    STORE_FIELD(hv, rec, count,    uint32_t);
    STORE_FIELD(hv, rec, lft,      uint32_t);
    STORE_FIELD(hv, rec, rgt,      uint32_t);

    my_av = (AV*)sv_2mortal((SV*)newAV());
    if (rec->groups) {
	itr = slurm_list_iterator_create(rec->groups);
	while ((jgr = slurm_list_next(itr))) {
	    rh = (HV*)sv_2mortal((SV*)newHV());
	    if (report_job_grouping_to_hv(jgr, rh) < 0) {
		Perl_warn(aTHX_ "Failed to convert a report_job_grouping to a hv");
		slurm_list_iterator_destroy(itr);
		return -1;
	    } else {
		av_push(my_av, newRV((SV*)rh));
	    }
	}
	slurm_list_iterator_destroy(itr);
    }
    hv_store_sv(hv, "groups", newRV((SV*)my_av));

    my_av = (AV*)sv_2mortal((SV*)newAV());
    if (rec->tres_list) {
	itr = slurm_list_iterator_create(rec->tres_list);
	while ((tres_rec = slurm_list_next(itr))) {
	    rh = (HV*)sv_2mortal((SV*)newHV());
	    if (tres_rec_to_hv(tres_rec, rh) < 0) {
		Perl_warn(aTHX_ "Failed to convert a tres_rec to a hv");
		slurm_list_iterator_destroy(itr);
		return -1;
	    } else {
		av_push(my_av, newRV((SV*)rh));
	    }
	}
	slurm_list_iterator_destroy(itr);
    }
    hv_store_sv(hv, "tres_list", newRV((SV*)my_av));

    return 0;
}

int
report_cluster_grouping_to_hv(slurmdb_report_cluster_grouping_t* rec, HV* hv)
{
    AV* my_av;
    HV* rh;
    slurmdb_report_acct_grouping_t* agr = NULL;
    slurmdb_tres_rec_t *tres_rec = NULL;
    ListIterator itr = NULL;

    STORE_FIELD(hv, rec, cluster,  charp);
    STORE_FIELD(hv, rec, count,    uint32_t);

    my_av = (AV*)sv_2mortal((SV*)newAV());
    if (rec->acct_list) {
	itr = slurm_list_iterator_create(rec->acct_list);
	while ((agr = slurm_list_next(itr))) {
	    rh = (HV*)sv_2mortal((SV*)newHV());
	    if (report_acct_grouping_to_hv(agr, rh) < 0) {
		Perl_warn(aTHX_ "Failed to convert a report_acct_grouping to a hv");
		slurm_list_iterator_destroy(itr);
		return -1;
	    } else {
		av_push(my_av, newRV((SV*)rh));
	    }
	}
	slurm_list_iterator_destroy(itr);
    }
    hv_store_sv(hv, "acct_list", newRV((SV*)my_av));

    my_av = (AV*)sv_2mortal((SV*)newAV());
    if (rec->tres_list) {
	itr = slurm_list_iterator_create(rec->tres_list);
	while ((tres_rec = slurm_list_next(itr))) {
	    rh = (HV*)sv_2mortal((SV*)newHV());
	    if (tres_rec_to_hv(tres_rec, rh) < 0) {
		Perl_warn(aTHX_ "Failed to convert a tres_rec to a hv");
		slurm_list_iterator_destroy(itr);
		return -1;
	    } else {
		av_push(my_av, newRV((SV*)rh));
	    }
	}
	slurm_list_iterator_destroy(itr);
    }
    hv_store_sv(hv, "tres_list", newRV((SV*)my_av));

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
		slurm_list_iterator_destroy(itr);
		return -1;
	    } else {
		av_push(av, newRV((SV*)rh));
	    }
	}
	slurm_list_iterator_destroy(itr);
    }

    return 0;
}

int
cluster_accounting_rec_to_hv(slurmdb_cluster_accounting_rec_t* ar, HV* hv)
{
    HV*   rh;

    STORE_FIELD(hv, ar, alloc_secs,   uint64_t);
    STORE_FIELD(hv, ar, down_secs,    uint64_t);
    STORE_FIELD(hv, ar, idle_secs,    uint64_t);
    STORE_FIELD(hv, ar, over_secs,    uint64_t);
    STORE_FIELD(hv, ar, pdown_secs,   uint64_t);
    STORE_FIELD(hv, ar, period_start, time_t);
    STORE_FIELD(hv, ar, plan_secs,    uint64_t);

    rh = (HV*)sv_2mortal((SV*)newHV());
    if (tres_rec_to_hv(&ar->tres_rec, rh) < 0) {
	    Perl_warn(aTHX_ "Failed to convert a tres_rec to a hv");
	    return -1;
    }
    hv_store_sv(hv, "tres_rec", newRV((SV*)rh));

    return 0;
}

int
cluster_rec_to_hv(slurmdb_cluster_rec_t* rec, HV* hv)
{
    AV* my_av;
    HV* rh;
    ListIterator itr = NULL;
    slurmdb_cluster_accounting_rec_t* ar = NULL;

    my_av = (AV*)sv_2mortal((SV*)newAV());
    if (rec->accounting_list) {
	itr = slurm_list_iterator_create(rec->accounting_list);
	while ((ar = slurm_list_next(itr))) {
	    rh = (HV*)sv_2mortal((SV*)newHV());
	    if (cluster_accounting_rec_to_hv(ar, rh) < 0) {
		Perl_warn(aTHX_ "Failed to convert a cluster_accounting_rec to a hv");
		slurm_list_iterator_destroy(itr);
		return -1;
	    } else {
		av_push(my_av, newRV((SV*)rh));
	    }
	}
	slurm_list_iterator_destroy(itr);
    }
    hv_store_sv(hv, "accounting_list", newRV((SV*)my_av));

    STORE_FIELD(hv, rec, classification, uint16_t);
    STORE_FIELD(hv, rec, control_host,   charp);
    STORE_FIELD(hv, rec, control_port,   uint32_t);
    STORE_FIELD(hv, rec, dimensions,     uint16_t);
    STORE_FIELD(hv, rec, flags,          uint32_t);
    STORE_FIELD(hv, rec, name,           charp);
    STORE_FIELD(hv, rec, nodes,          charp);
    STORE_FIELD(hv, rec, plugin_id_select, uint32_t);
    /* slurmdb_assoc_rec_t* root_assoc; */
    STORE_FIELD(hv, rec, rpc_version,    uint16_t);
    STORE_FIELD(hv, rec, tres_str,          charp);

    return 0;
}

int
report_assoc_rec_to_hv(slurmdb_report_assoc_rec_t* rec, HV* hv)
{
    AV* my_av;
    HV* rh;
    slurmdb_tres_rec_t *tres_rec = NULL;
    ListIterator itr = NULL;

    STORE_FIELD(hv, rec, acct,        charp);
    STORE_FIELD(hv, rec, cluster,     charp);
    STORE_FIELD(hv, rec, parent_acct, charp);

    my_av = (AV*)sv_2mortal((SV*)newAV());
    if (rec->tres_list) {
	itr = slurm_list_iterator_create(rec->tres_list);
	while ((tres_rec = slurm_list_next(itr))) {
	    rh = (HV*)sv_2mortal((SV*)newHV());
	    if (tres_rec_to_hv(tres_rec, rh) < 0) {
		Perl_warn(aTHX_ "Failed to convert a tres_rec to a hv");
		slurm_list_iterator_destroy(itr);
		return -1;
	    } else {
		av_push(my_av, newRV((SV*)rh));
	    }
	}
	slurm_list_iterator_destroy(itr);
    }
    hv_store_sv(hv, "tres_list", newRV((SV*)my_av));

    STORE_FIELD(hv, rec, user,        charp);

    return 0;
}

int
report_cluster_rec_to_hv(slurmdb_report_cluster_rec_t* rec, HV* hv)
{
    AV* my_av;
    HV* rh;
    slurmdb_report_assoc_rec_t* ar = NULL;
    slurmdb_report_user_rec_t* ur = NULL;
    slurmdb_tres_rec_t *tres_rec = NULL;
    ListIterator itr = NULL;

    /* FIXME: do the accounting_list (add function to parse
     * slurmdb_accounting_rec_t) */

    my_av = (AV*)sv_2mortal((SV*)newAV());
    if (rec->assoc_list) {
	itr = slurm_list_iterator_create(rec->assoc_list);
	while ((ar = slurm_list_next(itr))) {
	    rh = (HV*)sv_2mortal((SV*)newHV());
	    if (report_assoc_rec_to_hv(ar, rh) < 0) {
		Perl_warn(aTHX_ "Failed to convert a report_assoc_rec to a hv");
		slurm_list_iterator_destroy(itr);
		return -1;
	    } else {
		av_push(my_av, newRV((SV*)rh));
	    }
	}
	slurm_list_iterator_destroy(itr);
    }
    hv_store_sv(hv, "assoc_list", newRV((SV*)my_av));

    STORE_FIELD(hv, rec, name,      charp);

    my_av = (AV*)sv_2mortal((SV*)newAV());
    if (rec->tres_list) {
	itr = slurm_list_iterator_create(rec->tres_list);
	while ((tres_rec = slurm_list_next(itr))) {
	    rh = (HV*)sv_2mortal((SV*)newHV());
	    if (tres_rec_to_hv(tres_rec, rh) < 0) {
		Perl_warn(aTHX_ "Failed to convert a tres_rec to a hv");
		slurm_list_iterator_destroy(itr);
		return -1;
	    } else {
		av_push(my_av, newRV((SV*)rh));
	    }
	}
	slurm_list_iterator_destroy(itr);
    }
    hv_store_sv(hv, "tres_list", newRV((SV*)my_av));

    my_av = (AV*)sv_2mortal((SV*)newAV());
    if (rec->user_list) {
	itr = slurm_list_iterator_create(rec->user_list);
	while ((ur = slurm_list_next(itr))) {
	    rh = (HV*)sv_2mortal((SV*)newHV());
	    if (report_user_rec_to_hv(ur, rh) < 0) {
		Perl_warn(aTHX_ "Failed to convert a report_user_rec to a hv");
		slurm_list_iterator_destroy(itr);
		return -1;
	    } else {
		av_push(my_av, newRV((SV*)rh));
	    }
	}
	slurm_list_iterator_destroy(itr);
    }
    hv_store_sv(hv, "user_list", newRV((SV*)my_av));

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
		slurm_list_iterator_destroy(itr);
		return -1;
	    } else {
		av_push(av, newRV((SV*)rh));
	    }
	}
	slurm_list_iterator_destroy(itr);
    }

    return 0;
}

int
report_user_rec_to_hv(slurmdb_report_user_rec_t* rec, HV* hv)
{
    AV*   my_av;
    HV*   rh;
    char* acct;
    slurmdb_report_assoc_rec_t* ar = NULL;
    slurmdb_tres_rec_t *tres_rec = NULL;
    ListIterator itr = NULL;

    my_av = (AV*)sv_2mortal((SV*)newAV());
    if (rec->acct_list) {
	itr = slurm_list_iterator_create(rec->acct_list);
	while ((acct = slurm_list_next(itr))) {
	    av_push(my_av, newSVpv(acct, strlen(acct)));
	}
	slurm_list_iterator_destroy(itr);
    }
    hv_store_sv(hv, "acct_list", newRV((SV*)my_av));

    my_av = (AV*)sv_2mortal((SV*)newAV());
    if (rec->assoc_list) {
	itr = slurm_list_iterator_create(rec->assoc_list);
	while ((ar = slurm_list_next(itr))) {
	    rh = (HV*)sv_2mortal((SV*)newHV());
	    if (report_assoc_rec_to_hv(ar, rh) < 0) {
		Perl_warn(aTHX_ "Failed to convert a report_assoc_rec to a hv");
		slurm_list_iterator_destroy(itr);
		return -1;
	    } else {
		av_push(my_av, newRV((SV*)rh));
	    }
	}
	slurm_list_iterator_destroy(itr);
    }
    hv_store_sv(hv, "assoc_list", newRV((SV*)my_av));

    STORE_FIELD(hv, rec, acct,     charp);
    STORE_FIELD(hv, rec, name,     charp);

    my_av = (AV*)sv_2mortal((SV*)newAV());
    if (rec->tres_list) {
	itr = slurm_list_iterator_create(rec->tres_list);
	while ((tres_rec = slurm_list_next(itr))) {
	    rh = (HV*)sv_2mortal((SV*)newHV());
	    if (tres_rec_to_hv(tres_rec, rh) < 0) {
		Perl_warn(aTHX_ "Failed to convert a tres_rec to a hv");
		slurm_list_iterator_destroy(itr);
		return -1;
	    } else {
		av_push(my_av, newRV((SV*)rh));
	    }
	}
	slurm_list_iterator_destroy(itr);
    }
    hv_store_sv(hv, "tres_list", newRV((SV*)my_av));

    STORE_FIELD(hv, rec, uid,      uid_t);

    return 0;
}

int
stats_to_hv(slurmdb_stats_t *stats, HV* hv)
{
    STORE_FIELD(hv, stats, act_cpufreq,           double);
    STORE_FIELD(hv, stats, consumed_energy,       uint64_t);
    STORE_FIELD(hv, stats, tres_usage_in_ave,     charp);
    STORE_FIELD(hv, stats, tres_usage_in_max,     charp);
    STORE_FIELD(hv, stats, tres_usage_in_max_nodeid, charp);
    STORE_FIELD(hv, stats, tres_usage_in_max_taskid, charp);
    STORE_FIELD(hv, stats, tres_usage_in_min,     charp);
    STORE_FIELD(hv, stats, tres_usage_in_min_nodeid, charp);
    STORE_FIELD(hv, stats, tres_usage_in_min_taskid, charp);
    STORE_FIELD(hv, stats, tres_usage_in_tot, charp);
    STORE_FIELD(hv, stats, tres_usage_out_ave,    charp);
    STORE_FIELD(hv, stats, tres_usage_out_max,    charp);
    STORE_FIELD(hv, stats, tres_usage_out_max_nodeid, charp);
    STORE_FIELD(hv, stats, tres_usage_out_max_taskid, charp);
    STORE_FIELD(hv, stats, tres_usage_out_min,    charp);
    STORE_FIELD(hv, stats, tres_usage_out_min_nodeid, charp);
    STORE_FIELD(hv, stats, tres_usage_out_min_taskid, charp);
    STORE_FIELD(hv, stats, tres_usage_out_tot, charp);

    return 0;
}

int
step_rec_to_hv(slurmdb_step_rec_t *rec, HV* hv)
{
    HV* stats_hv = (HV*)sv_2mortal((SV*)newHV());
    HV *step_id_hv = (HV*)sv_2mortal((SV*)newHV());

    stats_to_hv(&rec->stats, stats_hv);
    hv_store_sv(hv, "stats", newRV((SV*)stats_hv));

    step_id_to_hv(&rec->step_id, step_id_hv);
    hv_store_sv(hv, "step_id", newRV((SV*)step_id_hv));

    STORE_FIELD(hv, rec, elapsed,         uint32_t);
    STORE_FIELD(hv, rec, end,             time_t);
    STORE_FIELD(hv, rec, exitcode,        int32_t);
    STORE_FIELD(hv, rec, nnodes,          uint32_t);
    STORE_FIELD(hv, rec, nodes,           charp);
    STORE_FIELD(hv, rec, ntasks,          uint32_t);
    STORE_FIELD(hv, rec, pid_str,         charp);
    STORE_FIELD(hv, rec, req_cpufreq_min, uint32_t);
    STORE_FIELD(hv, rec, req_cpufreq_max, uint32_t);
    STORE_FIELD(hv, rec, req_cpufreq_gov, uint32_t);
    STORE_FIELD(hv, rec, requid,          uint32_t);
    STORE_FIELD(hv, rec, start,           time_t);
    STORE_FIELD(hv, rec, state,           uint32_t);
    STORE_FIELD(hv, rec, stepname,        charp);
    STORE_FIELD(hv, rec, suspended,       uint32_t);
    STORE_FIELD(hv, rec, sys_cpu_sec,     uint32_t);
    STORE_FIELD(hv, rec, sys_cpu_usec,    uint32_t);
    STORE_FIELD(hv, rec, task_dist,       uint32_t);
    STORE_FIELD(hv, rec, tot_cpu_sec,     uint32_t);
    STORE_FIELD(hv, rec, tot_cpu_usec,    uint32_t);
    STORE_FIELD(hv, rec, tres_alloc_str,  charp);
    STORE_FIELD(hv, rec, user_cpu_sec,    uint32_t);
    STORE_FIELD(hv, rec, user_cpu_usec,   uint32_t);

    return 0;
}

int
job_rec_to_hv(slurmdb_job_rec_t* rec, HV* hv)
{
    slurmdb_step_rec_t *step;
    ListIterator itr = NULL;
    AV* steps_av = (AV*)sv_2mortal((SV*)newAV());
    HV* step_hv;

    if (rec->steps) {
	itr = slurm_list_iterator_create(rec->steps);
	while ((step = slurm_list_next(itr))) {
	    step_hv = (HV*)sv_2mortal((SV*)newHV());
	    step_rec_to_hv(step, step_hv);
	    av_push(steps_av, newRV((SV*)step_hv));
	}
	slurm_list_iterator_destroy(itr);
    }
    hv_store_sv(hv, "steps", newRV((SV*)steps_av));

    STORE_FIELD(hv, rec, account,         charp);
    STORE_FIELD(hv, rec, alloc_nodes,     uint32_t);
    STORE_FIELD(hv, rec, array_job_id,    uint32_t);
    STORE_FIELD(hv, rec, array_max_tasks, uint32_t);
    STORE_FIELD(hv, rec, array_task_id,   uint32_t);
    STORE_FIELD(hv, rec, array_task_str,  charp);
    STORE_FIELD(hv, rec, associd,         uint32_t);
    STORE_FIELD(hv, rec, blockid,         charp);
    STORE_FIELD(hv, rec, cluster,         charp);
    STORE_FIELD(hv, rec, derived_ec,      uint32_t);
    STORE_FIELD(hv, rec, derived_es,      charp);
    STORE_FIELD(hv, rec, elapsed,         uint32_t);
    STORE_FIELD(hv, rec, eligible,        time_t);
    STORE_FIELD(hv, rec, end,             time_t);
    STORE_FIELD(hv, rec, env,             charp);
    STORE_FIELD(hv, rec, exitcode,        uint32_t);
    STORE_FIELD(hv, rec, failed_node,     charp);
    /*STORE_FIELD(hv, rec, first_step_ptr,  void*);*/
    STORE_FIELD(hv, rec, gid,             uint32_t);
    STORE_FIELD(hv, rec, jobid,           uint32_t);
    STORE_FIELD(hv, rec, jobname,         charp);
    STORE_FIELD(hv, rec, lft,             uint32_t);
    STORE_FIELD(hv, rec, partition,       charp);
    STORE_FIELD(hv, rec, nodes,           charp);
    STORE_FIELD(hv, rec, priority,        uint32_t);
    STORE_FIELD(hv, rec, qosid,           uint32_t);
    STORE_FIELD(hv, rec, req_cpus,        uint32_t);
    STORE_FIELD(hv, rec, req_mem,         uint64_t);
    STORE_FIELD(hv, rec, requid,          uint32_t);
    STORE_FIELD(hv, rec, resvid,          uint32_t);
    STORE_FIELD(hv, rec, resv_name,       charp);
    STORE_FIELD(hv, rec, script,          charp);
    STORE_FIELD(hv, rec, show_full,       uint32_t);
    STORE_FIELD(hv, rec, start,           time_t);
    STORE_FIELD(hv, rec, state,           uint32_t);
    STORE_FIELD(hv, rec, submit,          time_t);
    STORE_FIELD(hv, rec, submit_line,     charp);
    STORE_FIELD(hv, rec, suspended,       uint32_t);
    STORE_FIELD(hv, rec, sys_cpu_sec,     uint32_t);
    STORE_FIELD(hv, rec, sys_cpu_usec,    uint32_t);
    STORE_FIELD(hv, rec, timelimit,       uint32_t);
    STORE_FIELD(hv, rec, tot_cpu_sec,     uint32_t);
    STORE_FIELD(hv, rec, tot_cpu_usec,    uint32_t);
    STORE_FIELD(hv, rec, tres_alloc_str,  charp);
    STORE_FIELD(hv, rec, tres_req_str,    charp);
    STORE_FIELD(hv, rec, uid,             uint32_t);
    STORE_FIELD(hv, rec, used_gres,       charp);
    STORE_FIELD(hv, rec, user,            charp);
    STORE_FIELD(hv, rec, user_cpu_sec,    uint32_t);
    STORE_FIELD(hv, rec, user_cpu_usec,   uint32_t);
    STORE_FIELD(hv, rec, wckey,           charp);
    STORE_FIELD(hv, rec, wckeyid,         uint32_t);

    return 0;
}

int
hv_to_qos_cond(HV* hv, slurmdb_qos_cond_t* qos_cond)
{
    AV*    element_av;
    char*  str = NULL;
    int    i, elements = 0;

    FETCH_FIELD(hv, qos_cond, preempt_mode, uint16_t, FALSE);
    FETCH_FIELD(hv, qos_cond, with_deleted, uint16_t, FALSE);

    FETCH_LIST_FIELD(hv, qos_cond, description_list);
    FETCH_LIST_FIELD(hv, qos_cond, id_list);
    FETCH_LIST_FIELD(hv, qos_cond, name_list);

    return 0;
}

int
qos_rec_to_hv(slurmdb_qos_rec_t* rec, HV* hv, List all_qos)
{
    char *preempt = NULL;
    preempt = slurmdb_get_qos_complete_str_bitstr(all_qos, rec->preempt_bitstr);
    hv_store_charp(hv, "preempt", preempt);

    STORE_FIELD(hv, rec, description,         charp);
    STORE_FIELD(hv, rec, id,                  uint32_t);
    STORE_FIELD(hv, rec, flags,               uint32_t);
    STORE_FIELD(hv, rec, grace_time,          uint32_t);
    STORE_FIELD(hv, rec, grp_jobs,            uint32_t);
    STORE_FIELD(hv, rec, grp_submit_jobs,     uint32_t);
    STORE_FIELD(hv, rec, grp_tres,            charp);
    STORE_FIELD(hv, rec, grp_tres_mins,       charp);
    STORE_FIELD(hv, rec, grp_tres_run_mins,   charp);
    STORE_FIELD(hv, rec, grp_wall,            uint32_t);
    STORE_FIELD(hv, rec, limit_factor,        double);
    STORE_FIELD(hv, rec, max_jobs_pu,         uint32_t);
    STORE_FIELD(hv, rec, max_submit_jobs_pu,  uint32_t);
    STORE_FIELD(hv, rec, max_tres_mins_pj,    charp);
    STORE_FIELD(hv, rec, max_tres_pj,         charp);
    STORE_FIELD(hv, rec, max_tres_pn,         charp);
    STORE_FIELD(hv, rec, max_tres_pu,         charp);
    STORE_FIELD(hv, rec, max_tres_run_mins_pu,charp);
    STORE_FIELD(hv, rec, max_wall_pj,         uint32_t);
    STORE_FIELD(hv, rec, min_tres_pj,         charp);
    STORE_FIELD(hv, rec, name,                charp);
    STORE_FIELD(hv, rec, preempt_mode,        uint16_t);
    STORE_FIELD(hv, rec, priority,            uint32_t);
    STORE_FIELD(hv, rec, usage_factor,        double);
    STORE_FIELD(hv, rec, usage_thres,         double);

    return 0;
}

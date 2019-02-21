#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "ppport.h"

#include <slurm/slurm.h>
#include <slurm/slurmdb.h>
#include "slurmdb-perl.h"

#include "const-c.inc"

extern void slurmdb_destroy_assoc_cond(void *object);
extern void slurmdb_destroy_cluster_cond(void *object);
extern void slurmdb_destroy_job_cond(void *object);
extern void slurmdb_destroy_user_cond(void *object);


MODULE = Slurmdb	PACKAGE = Slurmdb	PREFIX=slurmdb_

INCLUDE: const-xs.inc
PROTOTYPES: ENABLE

void*
slurmdb_connection_get()

int
slurmdb_connection_close(db_conn)
	void* db_conn

SV*
slurmdb_clusters_get(db_conn, conditions)
	void* db_conn
	HV*   conditions
    INIT:
	AV*   results;
	HV*   rh;
	List  list = NULL;
	ListIterator itr;
	slurmdb_cluster_cond_t *cluster_cond =
		xmalloc(sizeof(slurmdb_cluster_cond_t));
	slurmdb_init_cluster_cond(cluster_cond, 0);
	slurmdb_cluster_rec_t *rec = NULL;

	if (hv_to_cluster_cond(conditions, cluster_cond) < 0) {
		XSRETURN_UNDEF;
	}
	results = (AV*)sv_2mortal((SV*)newAV());
    CODE:
	list = slurmdb_clusters_get(db_conn, cluster_cond);
	if (list) {
	    itr = slurm_list_iterator_create(list);

	    while ((rec = slurm_list_next(itr))) {
		rh = (HV *)sv_2mortal((SV*)newHV());
		if (cluster_rec_to_hv(rec, rh) < 0) {
		    XSRETURN_UNDEF;
		}
		av_push(results, newRV((SV*)rh));
	    }
	    slurm_list_destroy(list);
	}
	RETVAL = newRV((SV*)results);
	slurmdb_destroy_cluster_cond(cluster_cond);
    OUTPUT:
        RETVAL

SV*
slurmdb_report_cluster_account_by_user(db_conn, assoc_condition)
	void* db_conn
	HV*   assoc_condition
    INIT:
	AV*   results;
	List  list = NULL;
	slurmdb_assoc_cond_t *assoc_cond =
		xmalloc(sizeof(slurmdb_assoc_cond_t));

	if (hv_to_assoc_cond(assoc_condition, assoc_cond) < 0) {
		XSRETURN_UNDEF;
	}
	results = (AV*)sv_2mortal((SV*)newAV());
    CODE:
	list = slurmdb_report_cluster_account_by_user(db_conn, assoc_cond);
	if (list) {
	    if (report_cluster_rec_list_to_av(list, results) < 0) {
		XSRETURN_UNDEF;
	    }
	    slurm_list_destroy(list);
	}
	RETVAL = newRV((SV*)results);
	slurmdb_destroy_assoc_cond(assoc_cond);
    OUTPUT:
        RETVAL

SV*
slurmdb_report_cluster_user_by_account(db_conn, assoc_condition)
	void* db_conn
	HV*   assoc_condition
    INIT:
	AV*   results;
	List  list = NULL;
	slurmdb_assoc_cond_t *assoc_cond =
		xmalloc(sizeof(slurmdb_assoc_cond_t));

	if (hv_to_assoc_cond(assoc_condition, assoc_cond) < 0) {
		XSRETURN_UNDEF;
	}
	results = (AV*)sv_2mortal((SV*)newAV());
    CODE:
	list = slurmdb_report_cluster_user_by_account(db_conn, assoc_cond);
	if (list) {
	    if (report_cluster_rec_list_to_av(list, results) < 0) {
		XSRETURN_UNDEF;
	    }
	    slurm_list_destroy(list);
	}
	RETVAL = newRV((SV*)results);
	slurmdb_destroy_assoc_cond(assoc_cond);
    OUTPUT:
        RETVAL

SV*
slurmdb_report_job_sizes_grouped_by_account(db_conn, job_condition, grouping_array, flat_view, acct_as_parent)
	void* db_conn
	HV*   job_condition
	AV*   grouping_array
	bool  flat_view
	bool  acct_as_parent
    INIT:
	AV*   results;
	List  list = NULL;
	List grouping_list = slurm_list_create(NULL);
	slurmdb_job_cond_t *job_cond =
		xmalloc(sizeof(slurmdb_job_cond_t));
	if (hv_to_job_cond(job_condition, job_cond) < 0) {
		XSRETURN_UNDEF;
	}
	if (av_to_cluster_grouping_list(grouping_array, grouping_list) < 0) {
		XSRETURN_UNDEF;
	}
	results = (AV*)sv_2mortal((SV*)newAV());
    CODE:
	list = slurmdb_report_job_sizes_grouped_by_account(db_conn,
	                   job_cond, grouping_list, flat_view, acct_as_parent);
	if (list) {
	    if (cluster_grouping_list_to_av(list, results) < 0) {
		XSRETURN_UNDEF;
	    }
	    slurm_list_destroy(list);
	}
	RETVAL = newRV((SV*)results);
	slurmdb_destroy_job_cond(job_cond);
	slurm_list_destroy(grouping_list);
    OUTPUT:
        RETVAL

SV*
slurmdb_report_user_top_usage(db_conn, user_condition, group_accounts)
	void* db_conn
	HV*   user_condition
	bool  group_accounts
    INIT:
	AV*   results;
	List  list = NULL;
	slurmdb_user_cond_t *user_cond =
		xmalloc(sizeof(slurmdb_user_cond_t));
	user_cond->assoc_cond =
		xmalloc(sizeof(slurmdb_assoc_cond_t));
	if (hv_to_user_cond(user_condition, user_cond) < 0) {
		XSRETURN_UNDEF;
	}
	results = (AV*)sv_2mortal((SV*)newAV());
    CODE:
	list = slurmdb_report_user_top_usage(db_conn, user_cond,
					     group_accounts);
	if (list) {
	    if (report_cluster_rec_list_to_av(list, results) < 0) {
		XSRETURN_UNDEF;
	    }
	    slurm_list_destroy(list);
	}
	RETVAL = newRV((SV*)results);
	slurmdb_destroy_user_cond(user_cond);
    OUTPUT:
        RETVAL

SV*
slurmdb_jobs_get(db_conn, conditions)
	void* db_conn
	HV*   conditions
    INIT:
	AV*   results;
	HV*   rh;
	List  list = NULL;
	ListIterator itr;
	slurmdb_job_cond_t *job_cond =
		xmalloc(sizeof(slurmdb_job_cond_t));
	slurmdb_job_rec_t *rec = NULL;

	if (hv_to_job_cond(conditions, job_cond) < 0) {
		XSRETURN_UNDEF;
	}
	results = (AV*)sv_2mortal((SV*)newAV());
    CODE:
	list = slurmdb_jobs_get(db_conn, job_cond);
	if (list) {
	    itr = slurm_list_iterator_create(list);

	    while ((rec = slurm_list_next(itr))) {
		rh = (HV *)sv_2mortal((SV*)newHV());
		if (job_rec_to_hv(rec, rh) < 0) {
		    XSRETURN_UNDEF;
		}
		av_push(results, newRV((SV*)rh));
	    }
	    slurm_list_destroy(list);
	}
	RETVAL = newRV((SV*)results);
	slurmdb_destroy_job_cond(job_cond);
    OUTPUT:
        RETVAL


SV*
slurmdb_qos_get(db_conn, conditions)
	void* db_conn
	HV*   conditions
    INIT:
	AV*   results;
	HV*   rh;
	List  list = NULL, all = NULL;
	ListIterator itr;
	slurmdb_qos_cond_t *qos_cond =
		xmalloc(sizeof(slurmdb_qos_cond_t));
	slurmdb_qos_rec_t *rec = NULL;

	if (hv_to_qos_cond(conditions, qos_cond) < 0) {
		XSRETURN_UNDEF;
	}
	results = (AV*)sv_2mortal((SV*)newAV());
    CODE:
	list = slurmdb_qos_get(db_conn, qos_cond);
	all = slurmdb_qos_get(db_conn, NULL);
	if (list) {
	    itr = slurm_list_iterator_create(list);

	    while ((rec = slurm_list_next(itr))) {
		rh = (HV *)sv_2mortal((SV*)newHV());
		if (qos_rec_to_hv(rec, rh, all) < 0) {
		    XSRETURN_UNDEF;
		}
		av_push(results, newRV((SV*)rh));
	    }
	    slurm_list_destroy(list);
	}
	RETVAL = newRV((SV*)results);
	slurmdb_destroy_qos_cond(qos_cond);
    OUTPUT:
        RETVAL

UV
slurmdb_find_tres_count_in_string(tres_str_in, id)
	char *tres_str_in
	int id

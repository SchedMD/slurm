#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "ppport.h"

#include <slurm/slurm.h>
#include <slurm/slurmdb.h>
#include "slurmdb-perl.h"

#include "const-c.inc"

extern void *slurm_xmalloc(size_t, const char *, int, const char *);
extern void slurmdb_destroy_association_cond(void *object);
extern void slurmdb_destroy_cluster_cond(void *object);


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
	AV * results;
	HV * rh;
	List list = NULL;
	ListIterator itr;
	slurmdb_cluster_cond_t *cluster_cond = (slurmdb_cluster_cond_t*)
		slurm_xmalloc(sizeof(slurmdb_cluster_cond_t), __FILE__,
		__LINE__, "slurmdb_clusters_get");
	slurmdb_cluster_rec_t *rec = NULL;

	if (hv_to_cluster_cond(conditions, cluster_cond) < 0) {
		XSRETURN_UNDEF;
	}
	results = (AV *)sv_2mortal((SV *)newAV());
    CODE:
	list = slurmdb_clusters_get(db_conn, cluster_cond);
	if (list) {
	    itr = slurm_list_iterator_create(list);

	    while ((rec = slurm_list_next(itr))) {
		rh = (HV *)sv_2mortal((SV *)newHV());
		if (cluster_rec_to_hv(rec, rh) < 0) {
		    XSRETURN_UNDEF;
		}
		av_push(results, newRV((SV *)rh));
	    }
	    slurm_list_destroy(list);
	}
	RETVAL = newRV((SV *)results);
	slurmdb_destroy_cluster_cond(cluster_cond);
    OUTPUT:
        RETVAL

SV*
slurmdb_report_cluster_account_by_user(db_conn, assoc_condition)
	void* db_conn
	HV*   assoc_condition
    INIT:
	AV * results;
	List list = NULL;
	slurmdb_association_cond_t *assoc_cond = (slurmdb_association_cond_t*)
		slurm_xmalloc(sizeof(slurmdb_association_cond_t), __FILE__,
		__LINE__, "slurmdb_report_cluster_account_by_user");

	if (hv_to_assoc_cond(assoc_condition, assoc_cond) < 0) {
		XSRETURN_UNDEF;
	}
	results = (AV *)sv_2mortal((SV *)newAV());
    CODE:
	list = slurmdb_report_cluster_account_by_user(db_conn, assoc_cond);
	if (list) {
	    if (report_cluster_rec_list_to_av(list, results) < 0) {
		XSRETURN_UNDEF;
	    }

	    slurm_list_destroy(list);
	}
	RETVAL = newRV((SV *)results);
	slurmdb_destroy_association_cond(assoc_cond);
    OUTPUT:
        RETVAL

#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "ppport.h"

#include <slurm/slurm.h>
#include <slurm/slurmdb.h>
#include "msg.h"

#include "const-c.inc"


MODULE = Slurmdb	PACKAGE = Slurmdb	PREFIX=slurmdb_

INCLUDE: const-xs.inc
PROTOTYPES: ENABLE

void*
slurmdb_connection_get()

int
slurmdb_connection_close(db_conn)
	void *db_conn

SV*
slurmdb_clusters_get(db_conn, conditions)
	void *db_conn
	HV* conditions
    INIT:
	AV * results;
	HV * rh;
	List list = slurm_list_create(NULL);
	ListIterator itr;
	slurmdb_cluster_cond_t cluster_cond;
	slurmdb_cluster_rec_t *rec = NULL;

	if (hv_to_cluster_cond(conditions, &cluster_cond) < 0) {
		XSRETURN_UNDEF;
	}
	results = (AV *)sv_2mortal((SV *)newAV());
    CODE:
	list = slurmdb_clusters_get(db_conn, &cluster_cond);
	itr = slurm_list_iterator_create(list);

	while ((rec = slurm_list_next(itr))) {
	    rh = (HV *)sv_2mortal((SV *)newHV());
	    if (cluster_rec_to_hv(rec, rh) < 0) {
	       	XSRETURN_UNDEF;
	    }
	    av_push(results, newRV((SV *)rh));
	}
	RETVAL = newRV((SV *)results);
	slurm_list_destroy(cluster_cond.cluster_list);
	slurm_list_destroy(list);
    OUTPUT:
        RETVAL


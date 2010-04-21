/*
 * slurmdb-perl.h - prototypes of msg-hv converting functions
 */

#ifndef _SLURMDB_PERL_H
#define _SLURMDB_PERL_H

#include <msg.h>


extern int hv_to_cluster_cond(HV* hv, slurmdb_cluster_cond_t* cluster_cond);
extern int hv_to_assoc_cond(HV* hv, slurmdb_association_cond_t* assoc_cond);
extern int cluster_accounting_rec_to_hv(slurmdb_cluster_accounting_rec_t *ar,
					HV* hv);
extern int cluster_rec_to_hv(slurmdb_cluster_rec_t *rec, HV* hv);
extern int report_assoc_rec_to_hv(slurmdb_report_assoc_rec_t *ar, HV* hv);
extern int report_user_rec_to_hv(slurmdb_report_user_rec_t *rec, HV* hv);
extern int report_cluster_rec_to_hv(slurmdb_report_cluster_rec_t* rec, HV* hv);
extern int report_cluster_rec_list_to_av(List list, AV* av);


#endif /* _SLURMDB_PERL_H */

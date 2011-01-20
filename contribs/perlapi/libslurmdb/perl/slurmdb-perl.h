/*
 * slurmdb-perl.h - prototypes of msg-hv converting functions
 */

#ifndef _SLURMDB_PERL_H
#define _SLURMDB_PERL_H

#include <msg.h>

#define FETCH_LIST_FIELD(hv, ptr, field) \
	do { \
	    SV** svp; \
	    if ( (svp = hv_fetch (hv, #field, strlen(#field), FALSE)) ) { \
		if(SvROK(*svp) && SvTYPE(SvRV(*svp)) == SVt_PVAV) { \
		    ptr->field = slurm_list_create(NULL); \
		    element_av = (AV*)SvRV(*svp); \
		    elements = av_len(element_av) + 1; \
		    for(i = 0; i < elements; i ++) { \
			if((svp = av_fetch(element_av, i, FALSE))) { \
			    str = slurm_xstrdup((char*)SvPV_nolen(*svp)); \
			    slurm_list_append(ptr->field, str); \
			} else { \
			    Perl_warn(aTHX_ "error fetching \"" #field "\" from \"" #ptr "\""); \
			    return -1; \
			} \
		    } \
		} else { \
		    Perl_warn(aTHX_ "\"" #field "\" of \"" #ptr "\" is not an array reference"); \
		    return -1; \
		} \
	    } \
	} while (0)


extern int av_to_cluster_grouping_list(AV* av, List grouping_list);
extern int hv_to_assoc_cond(HV* hv, slurmdb_association_cond_t* assoc_cond);
extern int hv_to_cluster_cond(HV* hv, slurmdb_cluster_cond_t* cluster_cond);
extern int hv_to_job_cond(HV* hv, slurmdb_job_cond_t* job_cond);
extern int hv_to_user_cond(HV* hv, slurmdb_user_cond_t* user_cond);

extern int cluster_grouping_list_to_av(List list, AV* av);
extern int cluster_rec_to_hv(slurmdb_cluster_rec_t *rec, HV* hv);
extern int report_cluster_rec_list_to_av(List list, AV* av);
extern int report_user_rec_to_hv(slurmdb_report_user_rec_t *rec, HV* hv);


#endif /* _SLURMDB_PERL_H */

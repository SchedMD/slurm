/*
 * slurmdb-perl.h - prototypes of msg-hv converting functions
 */

#ifndef _SLURMDB_PERL_H
#define _SLURMDB_PERL_H

#include <msg.h>

/* these declaration are not in slurm.h */
#ifndef xfree
#define xfree(__p) \
        slurm_xfree((void **)&(__p), __FILE__, __LINE__, __func__)
#define xmalloc(__sz) \
        slurm_xcalloc(1, __sz, true, false, __FILE__, __LINE__, __func__)
#endif

extern void slurm_xfree(void **, const char *, int, const char *);
extern void *slurm_xcalloc(size_t, size_t, bool, bool, const char *, int, const char *);

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

extern uint64_t slurmdb_find_tres_count_in_string(char *tres_str_in, int id);

extern int av_to_cluster_grouping_list(AV* av, List grouping_list);
extern int hv_to_assoc_cond(HV* hv, slurmdb_assoc_cond_t* assoc_cond);
extern int hv_to_cluster_cond(HV* hv, slurmdb_cluster_cond_t* cluster_cond);
extern int hv_to_job_cond(HV* hv, slurmdb_job_cond_t* job_cond);
extern int hv_to_user_cond(HV* hv, slurmdb_user_cond_t* user_cond);
extern int hv_to_qos_cond(HV* hv, slurmdb_qos_cond_t* qos_cond);

extern int cluster_grouping_list_to_av(List list, AV* av);
extern int cluster_rec_to_hv(slurmdb_cluster_rec_t *rec, HV* hv);
extern int report_cluster_rec_list_to_av(List list, AV* av);
extern int report_user_rec_to_hv(slurmdb_report_user_rec_t *rec, HV* hv);
extern int job_rec_to_hv(slurmdb_job_rec_t *rec, HV* hv);
extern int qos_rec_to_hv(slurmdb_qos_rec_t *rec, HV* hv, List all_qos);


#endif /* _SLURMDB_PERL_H */

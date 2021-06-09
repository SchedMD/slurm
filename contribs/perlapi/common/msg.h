/*
 * msg.h - prototypes of msg-hv converting functions
 */

#ifndef _MSG_H
#define _MSG_H

#include <EXTERN.h>
#include <stdint.h>
#include <perl.h>
#include <XSUB.h>
#include <slurm/slurm.h>

typedef char* charp;

/*
 * store an uint16_t into AV
 */
inline static int av_store_uint16_t(AV* av, int index, uint16_t val)
{
	SV* sv = NULL;
	/* Perl has a hard time figuring out the an unsigned int is
	   equal to INFINITE or NO_VAL since they are treated as
	   signed ints so we will handle this here. */
	if(val == INFINITE16)
		sv = newSViv(INFINITE);
	else if(val == NO_VAL16)
		sv = newSViv(NO_VAL);
	else
		sv = newSViv(val);

	if (av_store(av, (I32)index, sv) == NULL) {
		SvREFCNT_dec(sv);
		return -1;
	}
	return 0;
}

/*
 * store an uint32_t into AV
 */
inline static int av_store_uint32_t(AV* av, int index, uint32_t val)
{
	SV* sv = NULL;
	/* Perl has a hard time figuring out the an unsigned int is
	   equal to INFINITE or NO_VAL since they are treated as
	   signed ints so we will handle this here. */
	if(val == INFINITE)
		sv = newSViv(INFINITE);
	else if(val == NO_VAL)
		sv = newSViv(NO_VAL);
	else
		sv = newSViv(val);

	if (av_store(av, (I32)index, sv) == NULL) {
		SvREFCNT_dec(sv);
		return -1;
	}
	return 0;
}

/*
 * store an int into AV
 */
inline static int av_store_int(AV* av, int index, int val)
{
	SV* sv = newSViv(val);

	if (av_store(av, (I32)index, sv) == NULL) {
		SvREFCNT_dec(sv);
		return -1;
	}
	return 0;
}

/*
 * store an uint32_t into AV
 */
inline static int av_store_int32_t(AV* av, int index, int32_t val)
{
	return av_store_int(av, index, val);
}

/*
 * store a string into HV
 */
inline static int hv_store_charp(HV* hv, const char *key, charp val)
{
	SV* sv = NULL;

	if (val) {
		sv = newSVpv(val, 0);

		if (!key || hv_store(hv, key, (I32)strlen(key), sv, 0) == NULL) {
			SvREFCNT_dec(sv);
			return -1;
		}
	}
	return 0;
}

/*
 * store an unsigned 64b int into HV
 */
inline static int hv_store_uint64_t(HV* hv, const char *key, uint64_t val)
{
	SV* sv = NULL;
	/* Perl has a hard time figuring out the an unsigned int is
	   equal to INFINITE or NO_VAL since they are treated as
	   signed ints so we will handle this here. */
	if(val == (uint64_t)INFINITE)
		sv = newSViv(INFINITE);
	else if(val == (uint64_t)NO_VAL)
		sv = newSViv(NO_VAL);
	else
		sv = newSVuv(val);

	if (!key || hv_store(hv, key, (I32)strlen(key), sv, 0) == NULL) {
		SvREFCNT_dec(sv);
		return -1;
	}
	return 0;
}

/*
 * store an unsigned 32b int into HV
 */
inline static int hv_store_uint32_t(HV* hv, const char *key, uint32_t val)
{
	SV* sv = NULL;
	/* Perl has a hard time figuring out the an unsigned int is
	   equal to INFINITE or NO_VAL since they are treated as
	   signed ints so we will handle this here. */
	if(val == INFINITE)
		sv = newSViv(INFINITE);
	else if(val == NO_VAL)
		sv = newSViv(NO_VAL);
	else
		sv = newSVuv(val);

	if (!key || hv_store(hv, key, (I32)strlen(key), sv, 0) == NULL) {
		SvREFCNT_dec(sv);
		return -1;
	}
	return 0;
}

/*
 * store an unsigned 16b int into HV
 */
inline static int hv_store_uint16_t(HV* hv, const char *key, uint16_t val)
{
	SV* sv = NULL;
	/* Perl has a hard time figuring out the an unsigned int is
	   equal to INFINITE or NO_VAL since they are treated as
	   signed ints so we will handle this here. */
	if(val == INFINITE16)
		sv = newSViv(INFINITE);
	else if(val == NO_VAL16)
		sv = newSViv(NO_VAL);
	else
		sv = newSVuv(val);

	if (!key || hv_store(hv, key, (I32)strlen(key), sv, 0) == NULL) {
		SvREFCNT_dec(sv);
		return -1;
	}
	return 0;
}

/*
 * store an unsigned 8b int into HV
 */
inline static int hv_store_uint8_t(HV* hv, const char *key, uint8_t val)
{
	SV* sv = NULL;
	/* Perl has a hard time figuring out the an unsigned int is
	   equal to INFINITE or NO_VAL since they are treated as
	   signed ints so we will handle this here. */
	if(val == INFINITE8)
		sv = newSViv(INFINITE);
	else if(val == NO_VAL8)
		sv = newSViv(NO_VAL);
	else
		sv = newSVuv(val);

	if (!key || hv_store(hv, key, (I32)strlen(key), sv, 0) == NULL) {
		SvREFCNT_dec(sv);
		return -1;
	}
	return 0;
}

/*
 * store a uid_t into HV
 */
inline static int hv_store_uid_t(HV* hv, const char *key, uid_t val)
{
	SV* sv = newSVuv(val);

	if (!key || hv_store(hv, key, (I32)strlen(key), sv, 0) == NULL) {
		SvREFCNT_dec(sv);
		return -1;
	}
	return 0;
}

/*
 * store a signed int into HV
 */
inline static int hv_store_int(HV* hv, const char *key, int val)
{
	SV* sv = newSViv(val);

	if (!key || hv_store(hv, key, (I32)strlen(key), sv, 0) == NULL) {
		SvREFCNT_dec(sv);
		return -1;
	}
	return 0;
}

/*
 * store a double
 */
inline static int hv_store_double(HV* hv, const char *key, double val)
{
	SV* sv = newSVnv(val);

	if (!key || hv_store(hv, key, (I32)strlen(key), sv, 0) == NULL) {
		SvREFCNT_dec(sv);
		return -1;
	}
	return 0;
}

/*
 * store a signed 32b int into HV
 */
inline static int hv_store_int32_t(HV* hv, const char *key, int32_t val)
{
	return hv_store_int(hv, key, val);
}

/*
 * store a bool into HV
 */
inline static int hv_store_bool(HV* hv, const char *key, bool val)
{
	if (!key || hv_store(hv, key, (I32)strlen(key), (val ? &PL_sv_yes : &PL_sv_no), 0) == NULL) {
		return -1;
	}
	return 0;
}

/*
 * store a time_t into HV
 */
inline static int hv_store_time_t(HV* hv, const char *key, time_t val)
{
	SV* sv = newSVuv(val);

	if (!key || hv_store(hv, key, (I32)strlen(key), sv, 0) == NULL) {
		SvREFCNT_dec(sv);
		return -1;
	}
	return 0;
}

/*
 * store a SV into HV
 */
inline static int hv_store_sv(HV* hv, const char *key, SV* sv)
{
	if (!key || hv_store(hv, key, (I32)strlen(key), sv, 0) == NULL) {
		return -1;
	}
	return 0;
}

/*
 * store a PTR into HV
 * set classname to Nullch to avoid blessing the created SV.
 */
inline static int hv_store_ptr(HV* hv, const char *key, void* ptr, const char *classname)
{
	SV* sv = NULL;

	/* 
	 * if ptr == NULL and we call sv_setref_pv() and store the sv in hv, 
	 * sv_isobject() will fail later when FETCH_PTR_FIELD.
	 */
	if(ptr) {
		sv = newSV(0);
		sv_setref_pv(sv, classname, ptr);
		if (!key || hv_store(hv, key, (I32)strlen(key), sv, 0) == NULL) {
			SvREFCNT_dec(sv);
			return -1;
		}
	}

	return 0;
}

#define SV2int(sv)      SvIV(sv)
#define SV2int32_t(sv)  SvIV(sv)
#define SV2uint64_t(sv) SvUV(sv)
#define SV2uint32_t(sv) SvUV(sv)
#define SV2uint16_t(sv) SvUV(sv)
#define SV2uint8_t(sv)  SvUV(sv)
#define SV2time_t(sv)   SvUV(sv)
#define SV2charp(sv)    SvPV_nolen(sv)
#define SV2bool(sv)     SvTRUE(sv)

#if 0
/* Error on some 32-bit systems */
#define SV2ptr(sv)      SvIV(SvRV(sv))
#else
static inline void * SV2ptr(SV *sv)
{
	void * ptr = (void *) ((intptr_t) SvIV(SvRV(sv)));
	return ptr;
}
#endif
		
#define FETCH_FIELD(hv, ptr, field, type, required) \
	do { \
		SV** svp; \
		if ( (svp = hv_fetch (hv, #field, strlen(#field), FALSE)) ) { \
			ptr->field = (type) (SV2##type (*svp)); \
		} else if (required) { \
			Perl_warn (aTHX_ "Required field \"" #field "\" missing in HV at %s:%d",__FILE__,__LINE__); \
			return -1; \
		} \
	} while (0)

#define FETCH_PTR_FIELD(hv, ptr, field, classname, required) \
	do { \
		SV** svp; \
		if ( (svp = hv_fetch (hv, #field, strlen(#field), FALSE)) ) { \
			if (classname) { \
				if (! ( sv_isobject(*svp) && \
				        SvTYPE(SvRV(*svp)) == SVt_PVMG && \
				        sv_derived_from(*svp, classname)) ) { \
					Perl_croak(aTHX_ "field %s is not an object of %s", #field, classname); \
				} \
			} \
			ptr->field = (typeof(ptr->field)) (SV2ptr (*svp)); \
		} else if (required) { \
			Perl_warn (aTHX_ "Required field \"" #field "\" missing in HV"); \
			return -1; \
		} \
	} while (0)

#define STORE_FIELD(hv, ptr, field, type) \
	do { \
		if (hv_store_##type(hv, #field, ptr->field)) { \
			Perl_warn (aTHX_ "Failed to store field \"" #field "\""); \
			return -1; \
		} \
	} while (0)

#define STORE_PTR_FIELD(hv, ptr, field, classname) \
	do { \
		if (hv_store_ptr(hv, #field, ptr->field, classname)) { \
			Perl_warn (aTHX_ "Failed to store field \"" #field "\""); \
			return -1; \
		} \
	} while (0)

inline static int step_id_to_hv(slurm_step_id_t *step_id, HV *hv)
{
	STORE_FIELD(hv, step_id, job_id, uint32_t);
	STORE_FIELD(hv, step_id, step_het_comp, uint32_t);
	STORE_FIELD(hv, step_id, step_id, uint32_t);

	return 0;
}

inline static int hv_to_step_id(slurm_step_id_t *step_id, HV *hv)
{
	FETCH_FIELD(hv, step_id, job_id, uint32_t, TRUE);
	FETCH_FIELD(hv, step_id, step_het_comp, uint32_t, TRUE);
	FETCH_FIELD(hv, step_id, step_id, uint32_t, TRUE);

	return 0;
}

#endif /* _MSG_H */

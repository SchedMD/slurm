/*
 * msg2hv.h - prototypes of msg-hv converting functions
 */

#ifndef _MSG_H
#define _MSG_H

#include <perl.h>

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
	if(val == (uint16_t)INFINITE)
		sv = newSViv(INFINITE);
	else if(val == (uint16_t)NO_VAL)
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
	if(val == (uint32_t)INFINITE)
		sv = newSViv(INFINITE);
	else if(val == (uint32_t)NO_VAL)
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
 * store a string into HV
 */
inline static int hv_store_charp(HV* hv, const char *key, charp val)
{
	SV* sv = NULL;

	if(val)
		sv = newSVpv(val, 0);

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
	if(val == (uint32_t)INFINITE)
		sv = newSViv(INFINITE);
	else if(val == (uint32_t)NO_VAL)
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
	if(val == (uint16_t)INFINITE)
		sv = newSViv(INFINITE);
	else if(val == (uint16_t)NO_VAL)
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
	if(val == (uint8_t)INFINITE)
		sv = newSViv(INFINITE);
	else if(val == (uint8_t)NO_VAL)
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

#define SV2uint32_t(sv) SvUV(sv)
#define SV2uint16_t(sv) SvUV(sv)
#define SV2uint8_t(sv)  SvUV(sv)
#define SV2time_t(sv)   SvUV(sv)
#define SV2charp(sv)    SvPV_nolen(sv)
#define SV2bool(sv)     SvTRUE(sv)


#define FETCH_FIELD(hv, ptr, field, type, required) \
	do { \
		SV** svp; \
		if ( (svp = hv_fetch (hv, #field, strlen(#field), FALSE)) ) { \
			ptr->field = (type) (SV2##type (*svp)); \
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


extern int hv_to_job_desc_msg(HV* hv, job_desc_msg_t* job_desc_msg);
extern void free_job_desc_msg_memory(job_desc_msg_t *msg);
extern int resource_allocation_response_msg_to_hv(resource_allocation_response_msg_t* resp_msg, HV* hv);
extern int job_alloc_info_response_msg_to_hv(job_alloc_info_response_msg_t *resp_msg, HV* hv);
extern int submit_response_msg_to_hv(submit_response_msg_t *resp_msg, HV* hv);

extern int job_info_msg_to_hv(job_info_msg_t* job_info_msg, HV* hv);
extern int job_step_info_response_msg_to_hv(job_step_info_response_msg_t* job_step_info_msg, HV* hv);
extern int slurm_step_layout_to_hv(slurm_step_layout_t* step_layout, HV* hv);

extern int node_info_msg_to_hv(node_info_msg_t* node_info_msg, HV* hv);
extern int hv_to_update_node_msg(HV* hv, update_node_msg_t *update_msg);

extern int partition_info_msg_to_hv(partition_info_msg_t* part_info_msg, HV* hv);
extern int hv_to_update_part_msg(HV* hv, update_part_msg_t* part_msg);

extern int slurm_ctl_conf_to_hv(slurm_ctl_conf_t* conf, HV* hv);

extern int trigger_info_to_hv(trigger_info_t *info, HV* hv);
extern int trigger_info_msg_to_hv(trigger_info_msg_t *msg, HV* hv);
extern int hv_to_trigger_info(HV* hv, trigger_info_t* info);

extern int hv_to_slurm_step_ctx_params(HV* hv, slurm_step_ctx_params_t* params);
extern int hv_to_slurm_step_launch_params(HV* hv, slurm_step_launch_params_t *params);
extern void free_slurm_step_launch_params_memory(slurm_step_launch_params_t *params);

#endif /* _MSG_H */

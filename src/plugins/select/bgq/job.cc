#include <slurm/slurm.h>

#include <src/common/bitstring.h>
#include <src/common/list.h>
#include <src/common/pack.h>

extern "C" {

int
select_p_job_test(
        struct job_record *job_ptr,
        bitstr_t *bitmap,
        uint32_t min_nodes,
        uint32_t max_nodes, 
        uint32_t req_nodes,
        int mode,
        List preemptee_candidates,
        List *preemptee_job_list
        )
{

    return SLURM_SUCCESS;
}

int
select_p_job_init(
        List job_list
        )
{
	return SLURM_SUCCESS;
}

int
select_p_job_begin(
        struct job_record *job_ptr
        )
{

    return SLURM_SUCCESS;
}

int
select_p_job_ready(
        struct job_record *job_ptr
        )
{

    return SLURM_SUCCESS;
}

int
select_p_job_fini(
        struct job_record *job_ptr
        )
{
    
    return SLURM_SUCCESS;
}

int
select_p_job_suspend(
        struct job_record *job_ptr
        )
{

    return SLURM_SUCCESS;
}

int
select_p_job_resume(
        struct job_record *job_ptr
        )
{

    return SLURM_SUCCESS;
}

int
select_p_pack_select_info(
        time_t last_query_time, 
        Buf *buffer_ptr
        )
{
    
    return SLURM_SUCCESS;
}

int
select_p_select_nodeinfo_pack(
        select_nodeinfo_t *nodeinfo, 
        Buf buffer
        )
{

    return SLURM_SUCCESS;
}

int
select_p_select_nodeinfo_unpack(
        select_nodeinfo_t **nodeinfo, 
        Buf buffer
        )
{

    return SLURM_SUCCESS;
}

select_nodeinfo_t*
select_p_select_nodeinfo_alloc(
        uint32_t size
        )
{
    
    return SLURM_SUCCESS;
}

int
select_p_select_nodeinfo_free(
        select_nodeinfo_t *nodeinfo
        )
{

    return SLURM_SUCCESS;
}

int
select_p_select_nodeinfo_set_all(
        time_t last_query_time
        )
{
    return SLURM_SUCCESS;
}

int
select_p_select_nodeinfo_set(
        struct job_record *job_ptr
        )
{
    
    return SLURM_SUCCESS;
}


int
select_p_select_nodeinfo_get(
        select_nodeinfo_t *nodeinfo, 
        enum select_nodedata_type dinfo,
        enum node_states state,
        void *data
        )
{

    return SLURM_SUCCESS;
}

select_jobinfo_t*
select_p_select_jobinfo_alloc()
{
	return SLURM_SUCCESS;
}

int
select_p_select_jobinfo_set(
        select_jobinfo_t *jobinfo,
        enum select_jobdata_type data_type,
        void *data
        )
{

	return SLURM_SUCCESS;
}

int
select_p_select_jobinfo_get (
        select_jobinfo_t *jobinfo,
        enum select_jobdata_type data_type, 
        void *data
        )
{
	return SLURM_SUCCESS;
}

select_jobinfo_t*
select_p_select_jobinfo_copy(
        select_jobinfo_t *jobinfo
        )
{
	return NULL;
}

int
select_p_select_jobinfo_free(
        select_jobinfo_t *jobinfo
        )
{
	return SLURM_SUCCESS;
}

int select_p_select_jobinfo_pack(
        select_jobinfo_t *jobinfo,
        Buf buffer
        )
{
	return SLURM_SUCCESS;
}

int select_p_select_jobinfo_unpack(
        select_jobinfo_t **jobinfo,
        Buf buffer
        )
{
    return SLURM_SUCCESS;
}

char*
select_p_select_jobinfo_sprint(
        select_jobinfo_t *jobinfo,
        char *buf, size_t size, int mode
        )
{
    return NULL;
}

char*
select_p_select_jobinfo_xstrdup(
        select_jobinfo_t *jobinfo, 
        int mode
        )
{
	return NULL;
}

} // extern "C"

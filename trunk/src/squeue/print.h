

#include <src/api/slurm.h>
#include <src/common/list.h>

#ifndef _SQUEUE_PRINT_H_
#define _SQUEUE_PRINT_H_


#define FORMAT_STRING_SIZE 32

/*****************************************************************************
 * Format Structures
 *****************************************************************************/
typedef struct job_format {
	int (*function)(job_info_t*,int,bool);
	uint32_t width;
	bool left_justify;
} job_format_t;

typedef struct step_format {
	int (*function)(job_step_info_t*,int,bool);
	uint32_t width;
	bool left_justify;
} step_format_t;

int print_jobs_list( List jobs, List format );
int print_steps_list( List steps, List format );

int print_jobs_array( job_info_t* jobs, int size, List format );
int print_steps_array( job_step_info_t* steps, int size, List format );

int print_job_from_format( job_info_t* job, List list );
int print_step_from_format( job_step_info_t* job_step, List list );

/*****************************************************************************
 * Job Line Format Options
 *****************************************************************************/
int job_format_add_function ( List list, int width, bool left_justify, 
		int (*function)(job_info_t*,int,bool) );

#define job_format_add_job_id(list,wid,left) \
		job_format_add_function(list,wid,left,_print_job_job_id)
#define job_format_add_name(list,wid,left) \
		job_format_add_function(list,wid,left,_print_job_name)
#define job_format_add_user_name(list,wid,left) \
		job_format_add_function(list,wid,left,_print_job_user_name)
#define job_format_add_job_state(list,wid,left) \
		job_format_add_function(list,wid,left,_print_job_job_state)
#define job_format_add_job_state_compact(list,wid,left) \
		job_format_add_function(list,wid,left,_print_job_job_state_compact)
#define job_format_add_time_limit(list,wid,left) \
		job_format_add_function(list,wid,left,_print_job_time_limit)
#define job_format_add_start_time(list,wid,left) \
		job_format_add_function(list,wid,left,_print_job_start_time)
#define job_format_add_end_time(list,wid,left) \
		job_format_add_function(list,wid,left,_print_job_end_time)
#define job_format_add_priority(list,wid,left) \
		job_format_add_function(list,wid,left,_print_job_priority)
#define job_format_add_nodes(list,wid,left) \
		job_format_add_function(list,wid,left,_print_job_nodes)
#define job_format_add_node_inx(list,wid,left) \
		job_format_add_function(list,wid,left,_print_job_node_inx)
#define job_format_add_partition(list,wid,left) \
		job_format_add_function(list,wid,left,_print_job_partition)
#define job_format_add_num_procs(list,wid,left) \
		job_format_add_function(list,wid,left,_print_job_num_procs)
#define job_format_add_num_nodes(list,wid,left) \
		job_format_add_function(list,wid,left,_print_job_num_nodes)
#define job_format_add_shared(list,wid,left) \
		job_format_add_function(list,wid,left,_print_job_shared)
#define job_format_add_contiguous(list,wid,left) \
		job_format_add_function(list,wid,left,_print_job_contiguous)
#define job_format_add_min_procs(list,wid,left) \
		job_format_add_function(list,wid,left,_print_job_min_procs)
#define job_format_add_min_memory(list,wid,left) \
		job_format_add_function(list,wid,left,_print_job_min_memory)
#define job_format_add_min_tmp_disk(list,wid,left) \
		job_format_add_function(list,wid,left,_print_job_min_tmp_disk)
#define job_format_add_req_nodes(list,wid,left) \
		job_format_add_function(list,wid,left,_print_job_req_nodes)
#define job_format_add_req_node_inx(list,wid,left) \
		job_format_add_function(list,wid,left,_print_job_req_node_inx)
#define job_format_add_features(list,wid,left) \
		job_format_add_function(list,wid,left,_print_job_features)

/*****************************************************************************
 * Job Line Print Functions
 *****************************************************************************/
int _print_job_job_id( job_info_t* job, int width, bool left_justify );    
int _print_job_name( job_info_t* job, int width, bool left_justify );
int _print_job_user_id( job_info_t* job, int width, bool left_justify );
int _print_job_user_name( job_info_t* job, int width, bool left_justify );
int _print_job_job_state( job_info_t* job, int width, bool left_justify );
int _print_job_job_state_compact( job_info_t* job, int width, bool left_justify );
int _print_job_time_limit( job_info_t* job, int width, bool left_justify );
int _print_job_start_time( job_info_t* job, int width, bool left_justify );
int _print_job_end_time( job_info_t* job, int width, bool left_justify );
int _print_job_priority( job_info_t* job, int width, bool left_justify );
int _print_job_nodes( job_info_t* job, int width, bool left_justify );
int _print_job_node_inx( job_info_t* job, int width, bool left_justify );
int _print_job_partition( job_info_t* job, int width, bool left_justify );
int _print_job_num_procs( job_info_t* job, int width, bool left_justify );
int _print_job_num_nodes( job_info_t* job, int width, bool left_justify );
int _print_job_shared( job_info_t* job, int width, bool left_justify );
int _print_job_contiguous( job_info_t* job, int width, bool left_justify );
int _print_job_min_procs( job_info_t* job, int width, bool left_justify );
int _print_job_min_memory( job_info_t* job, int width, bool left_justify );
int _print_job_min_tmp_disk( job_info_t* job, int width, bool left_justify );
int _print_job_req_nodes( job_info_t* job, int width, bool left_justify );
int _print_job_req_node_inx( job_info_t* job, int width, bool left_justify );
int _print_job_features( job_info_t* job, int width, bool left_justify );

/*****************************************************************************
 * Step Print Format Functions
 *****************************************************************************/
int step_format_add_function ( List list, int width, bool left_justify, 
		int (*function)(job_step_info_t*,int,bool) );

#define step_format_add_id(list,wid,left) \
		step_format_add_function(list,wid,left,_print_step_id)
#define step_format_add_user_id(list,wid,left) \
		step_format_add_function(list,wid,left,_print_step_user_id)
#define step_format_add_start_time(list,wid,left) \
		step_format_add_function(list,wid,left,_print_step_start_time)
#define step_format_add_nodes(list,wid,left) \
		step_format_add_function(list,wid,left,_print_step_nodes)

/*****************************************************************************
 * Step Line Print Functions
 *****************************************************************************/
int _print_step_id( job_step_info_t* step, int width, bool left_justify );
int _print_step_user_id( job_step_info_t* step, int width, bool left_justify );
int _print_step_start_time( job_step_info_t* step, int width, bool left_justify );
int _print_step_nodes( job_step_info_t* step, int width, bool left_justify );

#endif

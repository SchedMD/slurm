/* *mode: c; c-basic-offset: 8; indent-tabs-mode: nil;*
* vim:expandtab:shiftwidth=8:tabstop=8:
*/

/*****************************************************************************\
 *  slurm_php.c - php interface to slurm.
 *
 *****************************************************************************
 *  Copyright (C) 2011 - Trinity Centre for High Performance Computing
 *  Copyright (C) 2011 - Trinity College Dublin
 *  Written By : Vermeulen Peter <HoWest><Belgium>
 *
 *  This file is part of php-slurm, a resource management program.
 *  Please also read the included file: DISCLAIMER.
 *
 *  php-slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  php-slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with php-slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

/*****************************************************************************\
*
*	Documentation for each function can be found in the slurm_php.h file
*
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "slurm_php.h"

static function_entry slurm_functions[] = {
	PHP_FE(slurm_ping,NULL)
	PHP_FE(slurm_slurmd_status,NULL)
	PHP_FE(slurm_print_partition_names, NULL)
	PHP_FE(slurm_get_specific_partition_info, NULL)
	PHP_FE(slurm_get_partition_node_names, NULL)
	PHP_FE(slurm_version,NULL)
	PHP_FE(slurm_get_node_names,NULL)
	PHP_FE(slurm_get_node_elements,NULL)
	PHP_FE(slurm_get_node_element_by_name,NULL)
	PHP_FE(slurm_get_node_state_by_name,NULL)
	PHP_FE(slurm_get_control_configuration_keys,NULL)
	PHP_FE(slurm_get_control_configuration_values,NULL)
	PHP_FE(slurm_load_job_information,NULL)
	PHP_FE(slurm_load_partition_jobs,NULL)
	PHP_FE(slurm_get_node_states,NULL)
	PHP_FE(slurm_hostlist_to_array,NULL)
	PHP_FE(slurm_array_to_hostlist,NULL) {
		NULL, NULL, NULL
	}
};

zend_module_entry slurm_php_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	SLURM_PHP_EXTNAME,
	slurm_functions,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
#if ZEND_MODULE_API_NO >= 20010901
	SLURM_PHP_VERSION,
#endif
	STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_SLURM_PHP
ZEND_GET_MODULE(slurm_php)
#endif


/*****************************************************************************\
 *	TODO
 *****************************************************************************
 *	[ADJUSTING EXISTING FUNCTIONS]
 *		- parse_node_pointer
 *			dynamic_plugin_data_t is currently not returned
 *	[EXTRA FUNCTIONS]
 *		- Functions that filter jobs on the nodes they are running on
 *		- Scheduling
 *		- ...
\*****************************************************************************/

/*****************************************************************************\
 *	HELPER FUNCTIONS
\*****************************************************************************/

time_t now()
{
	time_t * t;

	time(t);
	return (int)t;
}


int ld_partition_info(partition_info_msg_t ** part_pptr, uint16_t show_flags)
{
	return slurm_load_partitions(now(), part_pptr, show_flags);
}


int ld_node_info(node_info_msg_t ** node_pptr, uint16_t show_flags)
{
	return slurm_load_node(now(), node_pptr, show_flags);
}


int ld_job_info(job_info_msg_t ** job_pptr, uint16_t show_flags)
{
	return slurm_load_jobs(now(), job_pptr, show_flags);
}


void parse_node_pointer(zval * sub_arr, node_info_t * node_arr)
{
	zval *sub_arr_2 = NULL;

	zend_add_valid_assoc_string(sub_arr, "Name", node_arr->name);
	zend_add_valid_assoc_string(sub_arr, "Arch.", node_arr->arch);
	zend_add_valid_assoc_time_string(sub_arr, "Boot Time",
			&node_arr->boot_time);
	add_assoc_long(sub_arr, "#CPU'S", node_arr->cpus);
	add_assoc_long(sub_arr, "#Cores/CPU", node_arr->cores);

	if (node_arr->features == NULL) {
		add_assoc_null(sub_arr, "Features");
	} else {
		ALLOC_INIT_ZVAL(sub_arr_2);
		array_init(sub_arr_2);
		parse_array(node_arr->features, ",", sub_arr_2);
		add_assoc_zval(sub_arr, "Features", sub_arr_2);
	}

	zend_add_valid_assoc_string(sub_arr, "GRES", node_arr->gres);
	add_assoc_long(sub_arr, "State", node_arr->node_state);
	zend_add_valid_assoc_string(sub_arr, "OS", node_arr->os);
	add_assoc_long(sub_arr, "Real Mem", node_arr->real_memory);

	if(node_arr->reason!=NULL) {
		zend_add_valid_assoc_string(sub_arr, "Reason",
				node_arr->reason);
		zend_add_valid_assoc_time_string(sub_arr,"Reason Timestamp",
				&node_arr->reason_time);
		add_assoc_long(sub_arr, "Reason User Id",
				node_arr->reason_uid);
	} else {
		add_assoc_null(sub_arr, "Reason");
		add_assoc_null(sub_arr, "Reason Timestamp");
		add_assoc_null(sub_arr, "Reason User Id");
	}

	zend_add_valid_assoc_time_string(sub_arr, "Slurmd Startup Time",
				&node_arr->slurmd_start_time);
	add_assoc_long(sub_arr, "#Sockets/Node", node_arr->sockets);
	add_assoc_long(sub_arr, "#Threads/Core", node_arr->threads);
	add_assoc_long(sub_arr, "TmpDisk", node_arr->tmp_disk);
	add_assoc_long(sub_arr, "Weight", node_arr->weight);
}


void parse_assoc_array(char * char_arr, char * delims, zval * result_arr)
{
	char * rslt = NULL;
	char * tmp;
	int i = 0;

	rslt = strtok(char_arr, delims);
	while (rslt != NULL) {
		if (i == 0) {
			tmp = rslt;
		} else if (i == 1) {
			if(strcmp(rslt,"(null)")==0) {
				add_assoc_null(result_arr, tmp);
			} else {
				zend_add_valid_assoc_string(result_arr,
						tmp, rslt);
			}
		}
		i++;
		if (i == 2) {
			i = 0;
		}
		rslt = strtok(NULL, delims);
	}
}


void parse_array(char * char_arr, char * delims, zval * rslt_arr)
{
	char * rslt = NULL;

	rslt = strtok(char_arr, delims);
	while (rslt != NULL) {
		if(strcmp(rslt,"(null)")==0) {
			add_next_index_null(rslt_arr);
		} else {
			add_next_index_string(rslt_arr, rslt, 1);
		}
		rslt = strtok(NULL, delims);
	}
}


partition_info_t * get_partition_from_name(char * name,
		partition_info_t * prt_data,
		partition_info_msg_t * prt_ptr)
{
	int i;

	if (prt_ptr->record_count != 0) {
		for (i = 0; i < prt_ptr->record_count; i++) {
			if (strcmp(prt_ptr->partition_array->name, name) == 0) {
				prt_data = prt_ptr->partition_array;
				break;
			}
			prt_ptr->partition_array++;
		}
	}

	return prt_data;
}


void zend_add_valid_assoc_string(zval * rstl_arr, char * key, char * val)
{
	if(val==NULL) {
		add_assoc_null(rstl_arr, key);
	} else {
		add_assoc_string(rstl_arr, key, val, 1);
	}
}


void zend_add_valid_assoc_time_string(zval * rstl_arr, char * key, time_t * val)
{
	char buf[80];
	struct tm * timeinfo;

	if(val==0) {
		add_assoc_null(rstl_arr, key);
	} else {
		timeinfo = localtime(val);
		strftime(buf,80,TIME_FORMAT_STRING,timeinfo);
		add_assoc_string(rstl_arr, key, buf, 1);
	}
}


/*****************************************************************************\
 *	SLURM STATUS FUNCTIONS
\*****************************************************************************/

PHP_FUNCTION(slurm_ping)
{
	int err = SLURM_SUCCESS;

	array_init(return_value);
	err = slurm_ping(1);
	add_assoc_long(return_value,"Prim. Controller",err);
	err = slurm_ping(2);
	add_assoc_long(return_value,"Sec. Controller",err);
}


PHP_FUNCTION(slurm_slurmd_status)
{
	int err = SLURM_SUCCESS;
	static slurmd_status_t * status_ptr = NULL;

	err = slurm_load_slurmd_status(&status_ptr);
	if (err) {
		RETURN_LONG(-2);
	}

	array_init(return_value);
	zend_add_valid_assoc_time_string(return_value,"Booted_at",
			&status_ptr->booted);
	zend_add_valid_assoc_time_string(return_value,"Last_Msg",
			&status_ptr->last_slurmctld_msg);
	add_assoc_long(return_value,"Logging_Level", status_ptr->slurmd_debug);
	add_assoc_long(return_value,"Actual_CPU's", status_ptr->actual_cpus);
	add_assoc_long(return_value,"Actual_Sockets",
			status_ptr->actual_sockets);
	add_assoc_long(return_value,"Actual_Cores",status_ptr->actual_cores);
	add_assoc_long(return_value,"Actual_Threads",
			status_ptr->actual_threads);
	add_assoc_long(return_value,"Actual_Real_Mem",
			status_ptr->actual_real_mem);
	add_assoc_long(return_value,"Actual_Tmp_Disk",
			status_ptr->actual_tmp_disk);
	add_assoc_long(return_value,"PID",status_ptr->pid);
	zend_add_valid_assoc_string(return_value, "Hostname",
			status_ptr->hostname);
	zend_add_valid_assoc_string(return_value, "Slurm Logfile",
			status_ptr->slurmd_logfile);
	zend_add_valid_assoc_string(return_value, "Step List",
			status_ptr->step_list);
	zend_add_valid_assoc_string(return_value, "Version",
			status_ptr->version);
}


PHP_FUNCTION(slurm_version)
{
	long option = -1;

	if (zend_parse_parameters(ZEND_NUM_ARGS()TSRMLS_CC,
			"l", &option) == FAILURE) {
		RETURN_LONG(-3);
	}

	switch (option) {
	case 0:
		RETURN_LONG(SLURM_VERSION_MAJOR(SLURM_VERSION_NUMBER));
		break;
	case 1:
		RETURN_LONG(SLURM_VERSION_MINOR(SLURM_VERSION_NUMBER));
		break;
	case 2:
		RETURN_LONG(SLURM_VERSION_MICRO(SLURM_VERSION_NUMBER));
		break;
	default:
		array_init(return_value);
		add_next_index_long(return_value,
			SLURM_VERSION_MAJOR(SLURM_VERSION_NUMBER));
		add_next_index_long(return_value,
			SLURM_VERSION_MINOR(SLURM_VERSION_NUMBER));
		add_next_index_long(return_value,
			SLURM_VERSION_MICRO(SLURM_VERSION_NUMBER));
		break;
	}
}


/*****************************************************************************\
 *	SLURM PHP HOSTLIST FUNCTIONS
\*****************************************************************************/

PHP_FUNCTION(slurm_hostlist_to_array)
{
	long lngth = 0;
	char * host_list = NULL;
	int err = SLURM_SUCCESS;
	hostlist_t hl = NULL;
	int hl_length = 0;
	int i=0;

	if(zend_parse_parameters(ZEND_NUM_ARGS()TSRMLS_CC, "s|d",
			&host_list, &lngth) == FAILURE) {
		RETURN_LONG(-3);
	}

	if ((host_list == NULL) || (strcmp(host_list,"")==0)) {
		RETURN_LONG(-3);
	}

	hl = slurm_hostlist_create(host_list);
	hl_length = slurm_hostlist_count(hl);

	if(hl_length==0) {
		RETURN_LONG(-2);
	}

	array_init(return_value);
	for(i=0; i<hl_length; i++) {
		add_next_index_string(return_value,slurm_hostlist_shift(hl),1);
	}
}


PHP_FUNCTION(slurm_array_to_hostlist)
{
	zval * node_arr = NULL, ** data;
	hostlist_t hl = NULL;
	int i = 0;
	HashTable *arr_hash;
	HashPosition pointer;
	int arr_length = 0;
	char *buf;

	if(zend_parse_parameters(ZEND_NUM_ARGS()TSRMLS_CC, "a",
			&node_arr) == FAILURE) {
		RETURN_LONG(-3);
	}

	if (node_arr == NULL) {
		RETURN_LONG(-3);
	}

	arr_hash = Z_ARRVAL_P(node_arr);
	arr_length = zend_hash_num_elements(arr_hash);

	if(arr_length==0) {
		RETURN_LONG(-2);
	}

	hl = slurm_hostlist_create(NULL);
	for(    zend_hash_internal_pointer_reset_ex(arr_hash, &pointer);
	                zend_hash_get_current_data_ex(arr_hash, (void**) &data,
					&pointer) == SUCCESS;
	                zend_hash_move_forward_ex(arr_hash, &pointer)) {
		if (Z_TYPE_PP(data) == IS_STRING) {
			slurm_hostlist_push_host(hl,Z_STRVAL_PP(data));
		}
	}

	array_init(return_value);
	buf = slurm_hostlist_ranged_string_xmalloc(hl);
	zend_add_valid_assoc_string(return_value,"HOSTLIST",buf);
}


/*****************************************************************************\
 *	SLURM PARTITION READ FUNCTIONS
\*****************************************************************************/

PHP_FUNCTION(slurm_print_partition_names)
{
	int err = SLURM_SUCCESS;
	int i;
	static partition_info_msg_t * prt_ptr = NULL;

	err = ld_partition_info(&prt_ptr,0);
	if (err) {
		RETURN_LONG(-2);
	}

	if (prt_ptr->record_count == 0) {
		RETURN_LONG(-1);
	}

	array_init(return_value);
	for (i = 0; i < prt_ptr->record_count; i++) {
		add_next_index_string(return_value,
				prt_ptr->partition_array->name, 1);
		prt_ptr->partition_array++;
	}
}


PHP_FUNCTION(slurm_get_specific_partition_info)
{
	long lngth = 0;
	int err = SLURM_SUCCESS;
	static partition_info_msg_t * prt_ptr = NULL;
	partition_info_t * prt_data = NULL;
	char * name = NULL;
	char * tmp = NULL;

	if (zend_parse_parameters(ZEND_NUM_ARGS()TSRMLS_CC, "s|d", &name,
			&lngth) == FAILURE) {
		RETURN_LONG(-3);
	}

	if ((name == NULL) || (strcmp(name,"")==NULL)) {
		RETURN_LONG(-3);
	}

	err = ld_partition_info(&prt_ptr,0);
	if (err) {
		RETURN_LONG(-2);
	}

	prt_data = get_partition_from_name(name, prt_data, prt_ptr);
	if (prt_data == NULL) {
		RETURN_LONG(-1);
	}

	tmp = slurm_sprint_partition_info(prt_data, 1);
	array_init(return_value);
	parse_assoc_array(tmp, "= ", return_value);
}


PHP_FUNCTION(slurm_get_partition_node_names)
{
	char * prt_name = NULL;
	long lngth = 0;
	int err = SLURM_SUCCESS;
	static partition_info_msg_t * prt_ptr = NULL;
	partition_info_t * prt_data = NULL;
	char *tmp = NULL;

	if (zend_parse_parameters(ZEND_NUM_ARGS()TSRMLS_CC, "s|d", &prt_name,
			&lngth) == FAILURE) {
		RETURN_LONG(-3);
	}

	if ((prt_name == NULL) || (strcmp(prt_name,"")==0)) {
		RETURN_LONG(-3);
	}

	err = ld_partition_info(&prt_ptr,0);

	if (err) {
		RETURN_LONG(-2);
	}

	prt_data = get_partition_from_name(prt_name, prt_data, prt_ptr);
	if (prt_data == NULL) {
		RETURN_LONG(-1);
	}

	array_init(return_value);
	add_next_index_string(return_value,prt_data->nodes,1);
}


/*****************************************************************************\
 *	SLURM NODE CONFIGURATION READ FUNCTIONS
\*****************************************************************************/

PHP_FUNCTION(slurm_get_node_names)
{
	int err = SLURM_SUCCESS;
	int i = 0;
	static node_info_msg_t * node_ptr;

	err = ld_node_info(&node_ptr,0);
	if (err) {
		RETURN_LONG(-2);
	}

	if (node_ptr->record_count != 0) {
		array_init(return_value);
		for (i = 0; i < node_ptr->record_count; i++) {
			add_next_index_string(return_value,
					node_ptr->node_array->name, 1);
			node_ptr->node_array++;
		}
	} else {
		RETURN_LONG(-1);
	}
}


PHP_FUNCTION(slurm_get_node_elements)
{
	int err = SLURM_SUCCESS;
	int i = 0;
	static node_info_msg_t *node_ptr;
	zval *sub_arr = NULL;

	err = ld_node_info(&node_ptr,0);
	if (err) {
		RETURN_LONG(-2);
	}

	if (node_ptr->record_count == 0) {
		RETURN_LONG(-1);
	}

	array_init(return_value);
	for (i = 0; i < node_ptr->record_count; i++) {
		ALLOC_INIT_ZVAL(sub_arr);
		array_init(sub_arr);
		parse_node_pointer(sub_arr, node_ptr->node_array);
		add_assoc_zval(return_value, node_ptr->node_array->name,
				sub_arr);
		node_ptr->node_array++;
	}
}


PHP_FUNCTION(slurm_get_node_element_by_name)
{
	int err = SLURM_SUCCESS;
	int i = 0,y = 0;
	static node_info_msg_t * node_ptr;
	char * node_name = NULL;
	long lngth;
	zval *sub_arr = NULL;

	if (zend_parse_parameters(ZEND_NUM_ARGS()TSRMLS_CC, "s|d", &node_name,
			&lngth) == FAILURE) {
		RETURN_LONG(-3);
	}

	if ((node_name == NULL) || (strcmp(node_name,"")==0)) {
		RETURN_LONG(-3);
	}

	err = ld_node_info(&node_ptr,0);
	if (err) {
		RETURN_LONG(-2);
	}

	if (node_ptr->record_count == 0) {
		RETURN_LONG(-1);
	}

	array_init(return_value);
	for (i = 0; i < node_ptr->record_count; i++) {
		if (strcmp(node_ptr->node_array->name, node_name) == 0) {
			y++;
			ALLOC_INIT_ZVAL(sub_arr);
			array_init(sub_arr);
			parse_node_pointer(sub_arr, node_ptr->node_array);
			add_assoc_zval(return_value, node_ptr->node_array->name,
					sub_arr);
			break;
		}
		node_ptr->node_array++;
	}

	if(y==0) {
		RETURN_LONG(-1);
	}
}


PHP_FUNCTION(slurm_get_node_state_by_name)
{
	int err = SLURM_SUCCESS;
	int i = 0,y = 0;
	static node_info_msg_t * node_ptr;
	char * node_name = NULL;
	long lngth;

	if (zend_parse_parameters(ZEND_NUM_ARGS()TSRMLS_CC, "s|d", &node_name,
			&lngth) == FAILURE) {
		RETURN_LONG(-3);
	}

	if ((node_name == NULL) || (strcmp(node_name,"")==0)) {
		RETURN_LONG(-3);
	}

	err = ld_node_info(&node_ptr,0);
	if (err) {
		RETURN_LONG(-2);
	}

	if (node_ptr->record_count == 0) {
		RETURN_LONG(-1);
	}

	for (i = 0; i < node_ptr->record_count; i++) {
		if (strcmp(node_ptr->node_array->name, node_name) == 0) {
			y++;
			RETURN_LONG(node_ptr->node_array->node_state);
			break;
		}
		node_ptr->node_array++;
	}

	if(y==0) {
		RETURN_LONG(-1);
	}
}


PHP_FUNCTION(slurm_get_node_states)
{
	int err = SLURM_SUCCESS;
	int i = 0;
	static node_info_msg_t *node_ptr;
	zval *sub_arr = NULL;

	err = ld_node_info(&node_ptr,0);
	if (err) {
		RETURN_LONG(-2);
	}

	if (node_ptr->record_count == 0) {
		RETURN_LONG(-1);
	}

	array_init(return_value);
	for (i = 0; i < node_ptr->record_count; i++) {
		add_next_index_long(return_value,
				node_ptr->node_array->node_state);
		node_ptr->node_array++;
	}
}


/*****************************************************************************\
 *	SLURM CONFIGURATION READ FUNCTIONS
\*****************************************************************************/

PHP_FUNCTION(slurm_get_control_configuration_keys)
{
	int err = SLURM_SUCCESS;
	static slurm_ctl_conf_t * ctrl_conf_ptr;
	List lst;
	ListIterator iter = NULL;
	key_pair * k_p;

	err = slurm_load_ctl_conf(now(), &ctrl_conf_ptr);
	if (err) {
		RETURN_LONG(-2);
	}

	lst = slurm_ctl_conf_2_key_pairs(ctrl_conf_ptr);
	if (!lst) {
		RETURN_LONG(-1);
	}

	iter = slurm_list_iterator_create(lst);
	array_init(return_value);
	while ((k_p = slurm_list_next(iter))) {
		add_next_index_string(return_value,k_p->name,1);
	}
}


PHP_FUNCTION(slurm_get_control_configuration_values)
{
	int err = SLURM_SUCCESS;
	static slurm_ctl_conf_t * ctrl_conf_ptr;
	List lst;
	ListIterator iter = NULL;
	key_pair * k_p;

	err = slurm_load_ctl_conf(now(), &ctrl_conf_ptr);
	if (err) {
		RETURN_LONG(-2);
	}

	lst = slurm_ctl_conf_2_key_pairs(ctrl_conf_ptr);
	if (!lst) {
		RETURN_LONG(-1);
	}

	iter = slurm_list_iterator_create(lst);
	array_init(return_value);
	while ((k_p = slurm_list_next(iter))) {
		if(k_p->value==NULL) {
			add_next_index_null(return_value);
		} else {
			add_next_index_string(return_value,k_p->value,1);
		}
	}
}


/*****************************************************************************\
 *	SLURM JOB READ FUNCTIONS
\*****************************************************************************/

PHP_FUNCTION(slurm_load_job_information)
{
	int err = SLURM_SUCCESS;
	int i = 0;
	static job_info_msg_t * job_ptr;
	zval * sub_arr = NULL;
	char * tmp;

	err = ld_job_info(&job_ptr,0);
	if (err) {
		RETURN_LONG(-2);
	}

	if (job_ptr->record_count == 0) {
		RETURN_LONG(-1);
	}

	array_init(return_value);
	for (i = 0; i < job_ptr->record_count; i++) {
		tmp = malloc(sizeof(uint32_t) + 1);
		sprintf(tmp, "%d", job_ptr->job_array->job_id);
		ALLOC_INIT_ZVAL(sub_arr);
		array_init(sub_arr);
		parse_assoc_array(slurm_sprint_job_info(job_ptr->job_array, 1),
				"= ", sub_arr);
		add_assoc_zval(return_value, tmp, sub_arr);
		job_ptr->job_array++;
		free(tmp);
	}
}


PHP_FUNCTION(slurm_load_partition_jobs)
{
	int err = SLURM_SUCCESS;
	int i = 0;
	static job_info_msg_t * job_ptr;
	zval * sub_arr = NULL;
	char * tmp;
	char * pname = NULL;
	long lngth;
	long checker = 0;

	if (zend_parse_parameters(ZEND_NUM_ARGS()TSRMLS_CC, "s|d", &pname,
			&lngth) == FAILURE) {
		RETURN_LONG(-3);
	}

	if ((pname == NULL) || (strcmp(pname,"")==0)) {
		RETURN_LONG(-3);
	}

	err = ld_job_info(&job_ptr,0);
	if (err) {
		RETURN_LONG(-2);
	}

	if (job_ptr->record_count == 0) {
		RETURN_LONG(-1);
	}

	array_init(return_value);
	for (i = 0; i < job_ptr->record_count; i++) {
		if(strcmp(job_ptr->job_array->partition,pname)==0) {
			checker++;
			tmp = malloc(sizeof(uint32_t) + 1);
			sprintf(tmp, "%d", job_ptr->job_array->job_id);
			ALLOC_INIT_ZVAL(sub_arr);
			array_init(sub_arr);
			parse_assoc_array(slurm_sprint_job_info(
					job_ptr->job_array, 1), "= ", sub_arr);
			add_assoc_zval(return_value, tmp, sub_arr);
			free(tmp);
		}
		job_ptr->job_array++;
	}

	if(checker==0)	{
		RETURN_LONG(-1);
	}
}

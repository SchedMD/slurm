/*****************************************************************************\
 *  slurm_php.c - php interface to slurm.
 *
 *  $Id: account_gold.c 13061 2008-01-22 21:23:56Z da $
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "slurm_php.h"
#include "slurm/slurm.h"
#include "src/common/list.h"

static function_entry slurm_functions[] = {
    PHP_FE(hello_world, NULL)
    PHP_FE(print_partitions, NULL)
    {NULL, NULL, NULL}
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

PHP_FUNCTION(hello_world)
{
    RETURN_STRING("Hello World\n", 1);
}

PHP_FUNCTION(print_partitions)
{
	List sinfo_list = NULL;
	int error_code = SLURM_SUCCESS;
	uint16_t show_flags = 0;
	static partition_info_msg_t *new_part_ptr;
	printf("hey\n");
	slurm_info("got here!");
	printf("hey\n");
	error_code = slurm_load_partitions((time_t) NULL, &new_part_ptr,
					   show_flags);
	if (error_code) {
		error("slurm_load_part");
		RETURN_INT(error_code);
	}

//	sinfo_list = list_create(_sinfo_list_delete);
			
	RETURN_INT(error_code);
}

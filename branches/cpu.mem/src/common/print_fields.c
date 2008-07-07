/*****************************************************************************\
 *  print.c - definitions for all printing functions.
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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
#include "src/common/print_fields.h"
#include "src/common/parse_time.h"

int print_fields_parsable_print = 0;
int print_fields_have_header = 1;

extern void destroy_print_field(void *object)
{
	print_field_t *field = (print_field_t *)object;

	if(field) {
		xfree(field->name);
		xfree(field);
	}
}

extern void print_fields_header(List print_fields_list)
{
	ListIterator itr = NULL;
	print_field_t *object = NULL;

	if(!print_fields_list || !print_fields_have_header) 
		return;

	itr = list_iterator_create(print_fields_list);
	while((object = list_next(itr))) {
		(object->print_routine)(SLURM_PRINT_HEADLINE, object, 0);
	}
	list_iterator_reset(itr);
	printf("\n");
	if(print_fields_parsable_print)
		return;
	while((object = list_next(itr))) {
		(object->print_routine)(SLURM_PRINT_UNDERSCORE, object, 0);
	}
	list_iterator_destroy(itr);
	printf("\n");	
}

extern void print_fields_date(void)
{
	time_t now;

	now = time(NULL);
	printf("%s", ctime(&now));

}

extern void print_fields_str(type_t type, print_field_t *field, char *value)
{
	char *print_this = value;

	switch(type) {
	case SLURM_PRINT_HEADLINE:
		if(print_fields_parsable_print)
			printf("%s|", field->name);
		else
			printf("%-*.*s ", field->len, field->len, field->name);
		break;
	case SLURM_PRINT_UNDERSCORE:
		if(!print_fields_parsable_print)
			printf("%-*.*s ", field->len, field->len, 
			       "---------------------------------------");
		break;
	case SLURM_PRINT_VALUE:
		if(!print_this) {
			if(print_fields_parsable_print)
				print_this = "";
			else
				print_this = " ";
		}

		if(print_fields_parsable_print)
			printf("%s|", print_this);
		else {
			if(strlen(print_this) > field->len) 
				print_this[field->len-1] = '+';

			printf("%-*.*s ", field->len, field->len, print_this);
		}
		break;
	default:
		if(print_fields_parsable_print)
			printf("%s|", "n/a");
		else
			printf("%-*s ", field->len, "n/a");
		break;
	}
}

extern void print_fields_uint32(type_t type, print_field_t *field,
				uint32_t value)
{
	switch(type) {
	case SLURM_PRINT_HEADLINE:
		if(print_fields_parsable_print)
			printf("%s|", field->name);
		else
			printf("%*.*s ", field->len, field->len, field->name);
		break;
	case SLURM_PRINT_UNDERSCORE:
		if(!print_fields_parsable_print)
			printf("%*.*s ", field->len, field->len, 
			       "---------------------------------------");
		break;
	case SLURM_PRINT_VALUE:
		/* (value == unset)  || (value == cleared) */
		if((value == NO_VAL) || (value == INFINITE)) {
			if(print_fields_parsable_print)
				printf("|");	
			else				
				printf("%*s ", field->len, " ");
		} else {
			if(print_fields_parsable_print)
				printf("%u|", value);	
			else
				printf("%*u ", field->len, value);
		}
		break;
	default:
		if(print_fields_parsable_print)
			printf("%s|", "n/a");
		else
			printf("%*.*s ", field->len, field->len, "n/a");
		break;
	}
}

extern void print_fields_uint64(type_t type, print_field_t *field,
				uint64_t value)
{
	switch(type) {
	case SLURM_PRINT_HEADLINE:
		if(print_fields_parsable_print)
			printf("%s|", field->name);
		else
			printf("%*.*s ", field->len, field->len, field->name);
		break;
	case SLURM_PRINT_UNDERSCORE:
		if(!print_fields_parsable_print)
			printf("%*.*s ", field->len, field->len, 
			       "---------------------------------------");
		break;
	case SLURM_PRINT_VALUE:
		/* (value == unset)  || (value == cleared) */
		if((value == NO_VAL) || (value == INFINITE)) {
			if(print_fields_parsable_print)
				printf("|");	
			else				
				printf("%*s ", field->len, " ");
		} else {
			if(print_fields_parsable_print)
				printf("%llu|", (long long unsigned) value);	
			else
				printf("%*llu ", field->len, 
				       (long long unsigned) value);
		}
		break;
	default:
		if(print_fields_parsable_print)
			printf("%s|", "n/a");
		else
			printf("%*.*s ", field->len, field->len, "n/a");
		break;
	}
}

extern void print_fields_time(type_t type, print_field_t *field, uint32_t value)
{
	switch(type) {
	case SLURM_PRINT_HEADLINE:
		if(print_fields_parsable_print)
			printf("%s|", field->name);
		else
			printf("%*.*s ", field->len, field->len, field->name);
		break;
	case SLURM_PRINT_UNDERSCORE:
		if(!print_fields_parsable_print)
			printf("%*.*s ", field->len, field->len, 
			       "---------------------------------------");
		break;
	case SLURM_PRINT_VALUE:
		/* (value == unset)  || (value == cleared) */
		if((value == NO_VAL) || (value == INFINITE)) {
			if(print_fields_parsable_print)
				printf("|");	
			else
				printf("%*s ", field->len, " ");
		} else {
			char time_buf[32];
			mins2time_str((time_t) value, 
				      time_buf, sizeof(time_buf));
			if(print_fields_parsable_print)
				printf("%s|", time_buf);
			else
				printf("%*s ", field->len, time_buf);
		}
		break;
	default:
		printf("%*.*s ", field->len, field->len, "n/a");
		break;
	}
}

extern void print_fields_char_list(type_t type, print_field_t *field,
				   List value)
{
	ListIterator itr = NULL;
	char *print_this = NULL;
	char *object = NULL;
	
	switch(type) {
	case SLURM_PRINT_HEADLINE:
		if(print_fields_parsable_print)
			printf("%s|", field->name);
		else
			printf("%-*.*s ", field->len, field->len, field->name);
		break;
	case SLURM_PRINT_UNDERSCORE:
		if(!print_fields_parsable_print)
			printf("%-*.*s ", field->len, field->len, 
			       "---------------------------------------");
		break;
	case SLURM_PRINT_VALUE:
		if(!value || !list_count(value)) {
			if(print_fields_parsable_print)
				print_this = xstrdup("");
			else
				print_this = xstrdup(" ");
		} else {
			itr = list_iterator_create(value);
			while((object = list_next(itr))) {
				if(print_this) 
					xstrfmtcat(print_this, ",%s", object);
				else 
					print_this = xstrdup(object);
			}
			list_iterator_destroy(itr);
		}

		if(print_fields_parsable_print)
			printf("%s|", print_this);
		else {
			if(strlen(print_this) > field->len) 
				print_this[field->len-1] = '+';
			
			printf("%-*.*s ", field->len, field->len, print_this);
		}
		xfree(print_this);
		break;
	default:
		if(print_fields_parsable_print)
			printf("%s|", "n/a");
		else
			printf("%-*s ", field->len, "n/a");
		break;
	}
}

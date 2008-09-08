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

static int _sort_char_list(char *name_a, char *name_b)
{
	int diff = strcmp(name_a, name_b);

	if (diff < 0)
		return -1;
	else if (diff > 0)
		return 1;
	
	return 0;
}

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
	print_field_t *field = NULL;
	int curr_inx = 1;
	int field_count = 0;
	if(!print_fields_list || !print_fields_have_header) 
		return;

	field_count = list_count(print_fields_list);

	itr = list_iterator_create(print_fields_list);
	while((field = list_next(itr))) {
		if(print_fields_parsable_print
		   == PRINT_FIELDS_PARSABLE_NO_ENDING
		   && (curr_inx == field_count))
			printf("%s", field->name);	
		else if(print_fields_parsable_print)
			printf("%s|", field->name);
		else
			printf("%-*.*s ", field->len, field->len, field->name);
		curr_inx++;
	}
	list_iterator_reset(itr);
	printf("\n");
	if(print_fields_parsable_print)
		return;
	while((field = list_next(itr))) {
		printf("%-*.*s ", field->len, field->len, 
		       "-----------------------------------------------------");
	}
	list_iterator_destroy(itr);
	printf("\n");	
}

extern void print_fields_date(print_field_t *field, time_t value, int last)
{
	char temp_char[field->len];
	time_t now = value;

	if(!now)
		now = time(NULL);
	slurm_make_time_str(&value, (char *)temp_char, field->len);
	if(print_fields_parsable_print == PRINT_FIELDS_PARSABLE_NO_ENDING
	   && last)
		printf("%s", temp_char);	
	else if(print_fields_parsable_print)
		printf("%s|", temp_char);
	else 
		printf("%-*.*s ", field->len, field->len, temp_char);
}

extern void print_fields_str(print_field_t *field, char *value, int last)
{
	char temp_char[field->len];
	char *print_this = NULL;

	if(!value) {
		if(print_fields_parsable_print)
			print_this = "";
		else
			print_this = " ";
	}
	
	if(print_fields_parsable_print == PRINT_FIELDS_PARSABLE_NO_ENDING
	   && last)
		printf("%s", value);	
	else if(print_fields_parsable_print)
		printf("%s|", value);
	else {
		if(!print_this) {
			memcpy(&temp_char, value, field->len);
			
			if(strlen(value) > field->len) 
				temp_char[field->len-1] = '+';
			print_this = temp_char;
		}
		printf("%-*.*s ", field->len, field->len, print_this);
	}
}

extern void print_fields_uint32(print_field_t *field, uint32_t value, int last)
{
	/* (value == unset)  || (value == cleared) */
	if((value == NO_VAL) || (value == INFINITE)) {
		if(print_fields_parsable_print 
		   == PRINT_FIELDS_PARSABLE_NO_ENDING
		   && last)
			;
		else if(print_fields_parsable_print)
			printf("|");	
		else				
			printf("%*s ", field->len, " ");
	} else {
		if(print_fields_parsable_print
		   == PRINT_FIELDS_PARSABLE_NO_ENDING
		   && last)
			printf("%u", value);	
		else if(print_fields_parsable_print)
			printf("%u|", value);	
		else
			printf("%*u ", field->len, value);
	}
}

extern void print_fields_uint64(print_field_t *field, uint64_t value, int last)
{
	/* (value == unset)  || (value == cleared) */
	if((value == NO_VAL) || (value == INFINITE)) {
		if(print_fields_parsable_print 
		   == PRINT_FIELDS_PARSABLE_NO_ENDING
		   && last)
			;
		else if(print_fields_parsable_print)
			printf("|");	
		else				
			printf("%*s ", field->len, " ");
	} else {
		if(print_fields_parsable_print
		   == PRINT_FIELDS_PARSABLE_NO_ENDING
		   && last)
			printf("%llu", (long long unsigned) value);	
		else if(print_fields_parsable_print)
			printf("%llu|", (long long unsigned) value);	
		else
			printf("%*llu ", field->len, 
			       (long long unsigned) value);
	}
}

extern void print_fields_time(print_field_t *field, uint32_t value, int last)
{
	/* (value == unset)  || (value == cleared) */
	if((value == NO_VAL) || (value == INFINITE)) {
		if(print_fields_parsable_print 
		   == PRINT_FIELDS_PARSABLE_NO_ENDING
		   && last)
			;
		else if(print_fields_parsable_print)
			printf("|");	
		else
			printf("%*s ", field->len, " ");
	} else {
		char time_buf[32];
		mins2time_str((time_t) value, time_buf, sizeof(time_buf));
		if(print_fields_parsable_print 
		   == PRINT_FIELDS_PARSABLE_NO_ENDING
		   && last)
			printf("%s", time_buf);
		else if(print_fields_parsable_print)
			printf("%s|", time_buf);
		else
			printf("%*s ", field->len, time_buf);
	}
}

extern void print_fields_char_list(print_field_t *field, List value, int last)
{
	ListIterator itr = NULL;
	char *print_this = NULL;
	char *object = NULL;
	
	if(!value || !list_count(value)) {
		if(print_fields_parsable_print)
			print_this = xstrdup("");
		else
			print_this = xstrdup(" ");
	} else {
		list_sort(value, (ListCmpF)_sort_char_list);
		itr = list_iterator_create(value);
		while((object = list_next(itr))) {
			if(print_this) 
				xstrfmtcat(print_this, ",%s", object);
			else 
				print_this = xstrdup(object);
		}
		list_iterator_destroy(itr);
	}
	
	if(print_fields_parsable_print == PRINT_FIELDS_PARSABLE_NO_ENDING
	   && last)
		printf("%s", print_this);
	else if(print_fields_parsable_print)
		printf("%s|", print_this);
	else {
		if(strlen(print_this) > field->len) 
			print_this[field->len-1] = '+';
		
		printf("%-*.*s ", field->len, field->len, print_this);
	}
	xfree(print_this);
}

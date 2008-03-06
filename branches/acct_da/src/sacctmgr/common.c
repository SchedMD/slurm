/*****************************************************************************\
 *  print.c - definitions for all printing functions.
 *****************************************************************************
 *  Copyright (C) 2002-2008 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  UCRL-CODE-226842.
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

#include "sacctmgr.h"
#define FORMAT_STRING_SIZE 32


extern void print_header(void)
{
/* 	int	i,j; */
/* 	for (i=0; i<nprintfields; i++) { */
/* 		if (i) */
/* 			printf(" "); */
/* 		j=printfields[i]; */
/* 		(fields[j].print_routine)(HEADLINE, 0); */
/* 	} */
/* 	printf("\n"); */
/* 	for (i=0; i<nprintfields; i++) { */
/* 		if (i) */
/* 			printf(" "); */
/* 		j=printfields[i]; */
/* 		(fields[j].print_routine)(UNDERSCORE, 0); */
/* 	} */
/* 	printf("\n"); */
}

extern int  print_str(char *str, int width, bool right, bool cut_output)
{
	char format[64];
	int printed = 0;

	if (right == true && width != 0)
		snprintf(format, 64, "%%%ds", width);
	else if (width != 0)
		snprintf(format, 64, "%%.%ds", width);
	else {
		format[0] = '%';
		format[1] = 's';
		format[2] = '\0';
	}

	if ((width == 0) || (cut_output == false)) {
		if ((printed = printf(format, str)) < 0)
			return printed;
	} else {
		char temp[width + 1];
		snprintf(temp, width + 1, format, str);
		if ((printed = printf("%s",temp)) < 0)
			return printed;
	}

	while (printed++ < width)
		printf(" ");

	return printed;
}
extern void print_date(void)
{
	time_t now;

	now = time(NULL);
	printf("%s", ctime(&now));

}

extern int print_secs(long time, int width, bool right, bool cut_output)
{
	char str[FORMAT_STRING_SIZE];
	long days, hours, minutes, seconds;

	seconds =  time % 60;
	minutes = (time / 60)   % 60;
	hours   = (time / 3600) % 24;
	days    =  time / 86400;

	if (days) 
		snprintf(str, FORMAT_STRING_SIZE,
			 "%ld-%2.2ld:%2.2ld:%2.2ld",
		         days, hours, minutes, seconds);
	else if (hours)
		snprintf(str, FORMAT_STRING_SIZE,
			 "%ld:%2.2ld:%2.2ld",
		         hours, minutes, seconds);
	else
		snprintf(str, FORMAT_STRING_SIZE,
			 "%ld:%2.2ld",
		         minutes, seconds);

	print_str(str, width, right, cut_output);
	return SLURM_SUCCESS;
}

extern void addto_char_list(List char_list, char *names)
{
	int i=0, start=0;
	char *name = NULL, *tmp_char = NULL;
	ListIterator itr = list_iterator_create(char_list);

	if(names && char_list) {
		if (names[i] == '\"' || names[i] == '\'')
			i++;
		start = i;
		while(names[i]) {
			if(names[i] == '\"' || names[i] == '\'')
				break;
			else if(names[i] == ',') {
				if((i-start) > 0) {
					name = xmalloc((i-start+1));
					memcpy(name, names+start, (i-start));

					while((tmp_char = list_next(itr))) {
						if(!strcasecmp(tmp_char, name))
							break;
					}

					if(!tmp_char)
						list_append(char_list, name);
					else 
						xfree(name);
					list_iterator_reset(itr);
				}
				i++;
				start = i;
			}
			i++;
		}
		if((i-start) > 0) {
			name = xmalloc((i-start)+1);
			memcpy(name, names+start, (i-start));
			while((tmp_char = list_next(itr))) {
				if(!strcasecmp(tmp_char, name))
					break;
			}
			
			if(!tmp_char)
				list_append(char_list, name);
			else 
				xfree(name);
		}
	}	
	list_iterator_destroy(itr);
} 

extern void destroy_sacctmgr_action(void *object)
{
	sacctmgr_action_t *action = (sacctmgr_action_t *)object;
	
	if(action) {
		if(action->list)
			list_destroy(action->list);
			
		switch(action->type) {
		case SACCTMGR_ACTION_NOTSET:
		case SACCTMGR_USER_CREATE:
		case SACCTMGR_ACCOUNT_CREATE:
		case SACCTMGR_CLUSTER_CREATE:
		case SACCTMGR_ASSOCIATION_CREATE:
			/* These only have a list so there isn't
			 * anything else to free 
			 */
			break;
		case SACCTMGR_USER_MODIFY:
			destroy_acct_user_rec(action->rec);
			destroy_acct_user_cond(action->cond);
			break;
		case SACCTMGR_USER_DELETE:
			destroy_acct_user_cond(action->cond);
			break;
		case SACCTMGR_ACCOUNT_MODIFY:
			destroy_acct_account_rec(action->rec);
			destroy_acct_account_cond(action->cond);
			break;
		case SACCTMGR_ACCOUNT_DELETE:
			destroy_acct_account_cond(action->cond);
			break;
		case SACCTMGR_CLUSTER_MODIFY:
			destroy_acct_cluster_rec(action->rec);
			destroy_acct_cluster_cond(action->cond);
			break;
		case SACCTMGR_CLUSTER_DELETE:
			destroy_acct_cluster_cond(action->cond);
			break;
		case SACCTMGR_ASSOCIATION_MODIFY:
			destroy_acct_association_rec(action->rec);
			destroy_acct_association_cond(action->cond);
			break;
		case SACCTMGR_ASSOCIATION_DELETE:
			destroy_acct_association_cond(action->cond);
			break;
		case SACCTMGR_ADMIN_MODIFY:
			destroy_acct_user_cond(action->cond);
			break;
		case SACCTMGR_COORD_CREATE:
			xfree(action->rec);
			destroy_acct_user_cond(action->cond);
			break;
		case SACCTMGR_COORD_DELETE:
			xfree(action->rec);
			destroy_acct_user_cond(action->cond);
			break;	
		default:
			error("unknown action %d", action->type);
			break;
		}
		xfree(action);
	}
}

extern int commit_check(char *warning) 
{
	int ans = 0;

	printf("%s\n", warning);
	while(ans != 'Y' && ans != 'y'
	      && ans != 'N' && ans != 'n'
	      && ans != '\n') {
		if(ans) {
			getchar();  //grab the \n
			printf("Y or N please\n");
		}
		printf("(N/y): ");
		ans = getchar();
	}
	if(ans != '\n')
		getchar(); //grab the \n
	
	if(ans == 'Y' || ans == 'y') 
		return 1;			
	
	return 0;
}

extern int sacctmgr_init()
{
	static int inited = 0;

	if(inited) 
		return SLURM_SUCCESS;
	sacctmgr_action_list = list_create(destroy_sacctmgr_action);

	if(!sacctmgr_user_list)
		sacctmgr_user_list = acct_storage_g_get_users(NULL);
	if(!sacctmgr_account_list)
		sacctmgr_account_list = acct_storage_g_get_accounts(NULL);
	if(!sacctmgr_cluster_list)
		sacctmgr_cluster_list = acct_storage_g_get_clusters(NULL);
	if(!sacctmgr_association_list)
		sacctmgr_association_list = acct_storage_g_get_associations(
			NULL);

	inited = 1;

	return SLURM_SUCCESS;
}

extern int sacctmgr_remove_from_list(List list, void *object)
{
	ListIterator itr = NULL;
	void *new_object = NULL;
	int rc = SLURM_ERROR;

	if(!list || !object) {
		printf(" No list or object was given.\n");
		return SLURM_ERROR;
	}

	itr = list_iterator_create(list);
	
	while((new_object = list_next(itr))) {
		if(object == new_object) {
			if(list_delete_item(itr))
				rc = SLURM_SUCCESS;
			break;
		}		
	}
	list_iterator_destroy(itr);
	return SLURM_ERROR;
}

extern acct_association_rec_t *sacctmgr_find_association(char *user,
							 char *account,
							 char *cluster,
							 char *partition)
{
	ListIterator itr = NULL;
	acct_association_rec_t * assoc = NULL;
	
	if(!sacctmgr_association_list)
		sacctmgr_association_list =
			acct_storage_g_get_associations(NULL);
	
	itr = list_iterator_create(sacctmgr_association_list);
	while((assoc = list_next(itr))) {
		if((user && (!assoc->user || strcasecmp(user, assoc->user)))
		   || (!user && assoc->user && strcasecmp("none", assoc->user))
		   || (account && (!assoc->acct 
				   || strcasecmp(account, assoc->acct)))
		   || (cluster && (!assoc->cluster 
				   || strcasecmp(cluster, assoc->cluster)))
		   || (partition && (!assoc->partition 
				     || strcasecmp(partition, 
						   assoc->partition))))
			continue;
		break;
	}
	list_iterator_destroy(itr);
	
	return assoc;
}

extern acct_association_rec_t *sacctmgr_find_parent_assoc(char *account,
							  char *cluster)
{
	ListIterator itr = NULL;
	acct_association_rec_t *assoc = NULL;
	uint32_t par_id = 0;

	if(!account || !cluster)
		return NULL;

	if(!strcasecmp(account, "root"))
		return NULL;

	if(!sacctmgr_association_list)
		sacctmgr_association_list =
			acct_storage_g_get_associations(NULL);

	itr = list_iterator_create(sacctmgr_association_list);
	while((assoc = list_next(itr))) {
		if(par_id) {
			if(assoc->id == par_id)
				break;
			else
				continue;
		}
		if((assoc->user && strcmp(assoc->user, "NONE"))
		   || strcasecmp(account, assoc->acct)
		   || strcasecmp(cluster, assoc->cluster))
			continue;
		list_iterator_reset(itr);
		par_id = assoc->parent;
	}
	list_iterator_destroy(itr);

	return assoc;
}

extern acct_association_rec_t *sacctmgr_find_account_base_assoc(char *account,
								char *cluster)
{
	ListIterator itr = NULL;
	acct_association_rec_t *assoc = NULL;
	char *temp = "root";

	if(!cluster)
		return NULL;

	if(!sacctmgr_association_list)
		sacctmgr_association_list =
			acct_storage_g_get_associations(NULL);

	if(account)
		temp = account;
	
	itr = list_iterator_create(sacctmgr_association_list);
	while((assoc = list_next(itr))) {
		if((assoc->user && strcmp(assoc->user, "NONE"))
		   || strcasecmp(temp, assoc->acct)
		   || strcasecmp(cluster, assoc->cluster))
			continue;
		
		break;
	}
	list_iterator_destroy(itr);

	return assoc;
}

extern acct_user_rec_t *sacctmgr_find_user(char *name)
{
	ListIterator itr = NULL;
	acct_user_rec_t *user = NULL;
	
	if(!name)
		return NULL;
	
	if(!sacctmgr_user_list)
		sacctmgr_user_list = acct_storage_g_get_users(NULL);

	itr = list_iterator_create(sacctmgr_user_list);
	while((user = list_next(itr))) {
		if(!strcasecmp(name, user->name))
			break;
	}
	list_iterator_destroy(itr);
	
	return user;
}

extern acct_account_rec_t *sacctmgr_find_account(char *name)
{
	ListIterator itr = NULL;
	acct_account_rec_t *account = NULL;
	
	if(!name)
		return NULL;

	if(!sacctmgr_account_list)
		sacctmgr_account_list = acct_storage_g_get_accounts(NULL);
	
	itr = list_iterator_create(sacctmgr_account_list);
	while((account = list_next(itr))) {
		if(!strcasecmp(name, account->name))
			break;
	}
	list_iterator_destroy(itr);
	
	return account;
}

extern acct_cluster_rec_t *sacctmgr_find_cluster(char *name)
{
	ListIterator itr = NULL;
	acct_cluster_rec_t *cluster = NULL;
	if(!name)
		return NULL;

	if(!sacctmgr_cluster_list)
		sacctmgr_cluster_list = acct_storage_g_get_clusters(NULL);

	itr = list_iterator_create(sacctmgr_cluster_list);
	while((cluster = list_next(itr))) {
		if(!strcasecmp(name, cluster->name))
			break;
	}
	list_iterator_destroy(itr);
	
	return cluster;
}


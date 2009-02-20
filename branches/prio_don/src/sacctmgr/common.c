/*****************************************************************************\
 *  common.c - definitions for functions common to all modules in sacctmgr.
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

#include "src/sacctmgr/sacctmgr.h"
#include "src/common/slurmdbd_defs.h"

#include <unistd.h>
#include <termios.h>

#define FORMAT_STRING_SIZE 32

static pthread_t lock_warning_thread;

static void *_print_lock_warn(void *no_data)
{
	sleep(5);
	printf(" Database is busy or waiting for lock from other user.\n");

	return NULL;
}

static void _nonblock(int state)
{
	struct termios ttystate;

	//get the terminal state
	tcgetattr(STDIN_FILENO, &ttystate);

	switch(state) {
	case 1:
		//turn off canonical mode
		ttystate.c_lflag &= ~ICANON;
		//minimum of number input read.
		ttystate.c_cc[VMIN] = 1;
		break;
	default:
		//turn on canonical mode
		ttystate.c_lflag |= ICANON;
	}
	//set the terminal attributes.
	tcsetattr(STDIN_FILENO, TCSANOW, &ttystate);

}

static char *_get_qos_list_str(List qos_list)
{
	char *qos_char = NULL;
	ListIterator itr = NULL;
	acct_qos_rec_t *qos = NULL;

	if(!qos_list)
		return NULL;

	itr = list_iterator_create(qos_list);
	while((qos = list_next(itr))) {
		if(qos_char) 
			xstrfmtcat(qos_char, ",%s", qos->name);
		else
			xstrcat(qos_char, qos->name);
	}
	list_iterator_destroy(itr);

	return qos_char;
}

extern int parse_option_end(char *option)
{
	int end = 0;

	if(!option)
		return 0;

	while(option[end]) {
		if((option[end] == '=')
		   || (option[end] == '+' && option[end+1] == '=')
		   || (option[end] == '-' && option[end+1] == '='))
			break;
		end++;		
	}

	if(!option[end])
		return 0;

	end++;
	return end;
}

/* you need to xfree whatever is sent from here */
extern char *strip_quotes(char *option, int *increased, bool make_lower)
{
	int end = 0;
	int i=0, start=0;
	char *meat = NULL;
	char quote_c = '\0';
	int quote = 0;

	if(!option)
		return NULL;

	/* first strip off the ("|')'s */
	if (option[i] == '\"' || option[i] == '\'') {
		quote_c = option[i];
		quote = 1;
		i++;
	}
	start = i;

	while(option[i]) {
		if(quote && option[i] == quote_c) {
			end++;
			break;
		} else if(option[i] == '\"' || option[i] == '\'')
			option[i] = '`';
		else if(make_lower) {
			char lower = tolower(option[i]);
			if(lower != option[i])
				option[i] = lower;
		}
		
		i++;
	}
	end += i;

	meat = xmalloc((i-start)+1);
	memcpy(meat, option+start, (i-start));

	if(increased)
		(*increased) += end;

	return meat;
}

extern int notice_thread_init()
{
	pthread_attr_t attr;
	
	slurm_attr_init(&attr);
	if(pthread_create(&lock_warning_thread, &attr, &_print_lock_warn, NULL))
		error ("pthread_create error %m");
	slurm_attr_destroy(&attr);
	return SLURM_SUCCESS;
}

extern int notice_thread_fini()
{
	return pthread_cancel(lock_warning_thread);
}

extern int commit_check(char *warning) 
{
	int ans = 0;
	char c = '\0';
	int fd = fileno(stdin);
	fd_set rfds;
	struct timeval tv;

	if(!rollback_flag)
		return 1;

	printf("%s (You have 30 seconds to decide)\n", warning);
	_nonblock(1);
	while(c != 'Y' && c != 'y'
	      && c != 'N' && c != 'n'
	      && c != '\n') {
		if(c) {
			printf("Y or N please\n");
		}
		printf("(N/y): ");
		fflush(stdout);
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		/* Wait up to 30 seconds. */
		tv.tv_sec = 30;
		tv.tv_usec = 0;
		if((ans = select(fd+1, &rfds, NULL, NULL, &tv)) <= 0)
			break;
		
		c = getchar();
		printf("\n");
	}
	_nonblock(0);
	if(ans <= 0) 
		printf("timeout\n");
	else if(c == 'Y' || c == 'y') 
		return 1;			
	
	return 0;
}

extern acct_association_rec_t *sacctmgr_find_association(char *user,
							 char *account,
							 char *cluster,
							 char *partition)
{
	acct_association_rec_t * assoc = NULL;
	acct_association_cond_t assoc_cond;
	List assoc_list = NULL;

	memset(&assoc_cond, 0, sizeof(acct_association_cond_t));
	if(account) {
		assoc_cond.acct_list = list_create(NULL);
		list_append(assoc_cond.acct_list, account);
	} else {
		error("need an account to find association");
		return NULL;
	}
	if(cluster) {
		assoc_cond.cluster_list = list_create(NULL);
		list_append(assoc_cond.cluster_list, cluster);
	} else {
		if(assoc_cond.acct_list)
			list_destroy(assoc_cond.acct_list);
		error("need an cluster to find association");
		return NULL;
	}

	assoc_cond.user_list = list_create(NULL);
	if(user) 
		list_append(assoc_cond.user_list, user);
	else
		list_append(assoc_cond.user_list, "");
	
	assoc_cond.partition_list = list_create(NULL);
	if(partition) 
		list_append(assoc_cond.partition_list, partition);
	else
		list_append(assoc_cond.partition_list, "");
	
	assoc_list = acct_storage_g_get_associations(db_conn, my_uid,
						     &assoc_cond);
	
	list_destroy(assoc_cond.acct_list);
	list_destroy(assoc_cond.cluster_list);
	list_destroy(assoc_cond.user_list);
	list_destroy(assoc_cond.partition_list);

	if(assoc_list)
		assoc = list_pop(assoc_list);

	list_destroy(assoc_list);
	
	return assoc;
}

extern acct_association_rec_t *sacctmgr_find_account_base_assoc(char *account,
								char *cluster)
{
	acct_association_rec_t *assoc = NULL;
	char *temp = "root";
	acct_association_cond_t assoc_cond;
	List assoc_list = NULL;

	if(!cluster)
		return NULL;

	if(account)
		temp = account;

	memset(&assoc_cond, 0, sizeof(acct_association_cond_t));
	assoc_cond.acct_list = list_create(NULL);
	list_append(assoc_cond.cluster_list, temp);
	assoc_cond.cluster_list = list_create(NULL);
	list_append(assoc_cond.cluster_list, cluster);
	assoc_cond.user_list = list_create(NULL);
	list_append(assoc_cond.user_list, "");

	assoc_list = acct_storage_g_get_associations(db_conn, my_uid,
						     &assoc_cond);

	list_destroy(assoc_cond.acct_list);
	list_destroy(assoc_cond.cluster_list);
	list_destroy(assoc_cond.user_list);

	if(assoc_list)
		assoc = list_pop(assoc_list);

	list_destroy(assoc_list);

	return assoc;
}

extern acct_association_rec_t *sacctmgr_find_root_assoc(char *cluster)
{
	return sacctmgr_find_account_base_assoc(NULL, cluster);
}

extern acct_user_rec_t *sacctmgr_find_user(char *name)
{
	acct_user_rec_t *user = NULL;
	acct_user_cond_t user_cond;
	acct_association_cond_t assoc_cond;
	List user_list = NULL;
	
	if(!name)
		return NULL;
	
	memset(&user_cond, 0, sizeof(acct_user_cond_t));
	memset(&assoc_cond, 0, sizeof(acct_association_cond_t));
	assoc_cond.user_list = list_create(NULL);
	list_append(assoc_cond.user_list, name);
	user_cond.assoc_cond = &assoc_cond;

	user_list = acct_storage_g_get_users(db_conn, my_uid,
					     &user_cond);

	list_destroy(assoc_cond.user_list);

	if(user_list)
		user = list_pop(user_list);

	list_destroy(user_list);

	return user;
}

extern acct_account_rec_t *sacctmgr_find_account(char *name)
{
	acct_account_rec_t *account = NULL;
	acct_account_cond_t account_cond;
	acct_association_cond_t assoc_cond;
	List account_list = NULL;
	
	if(!name)
		return NULL;

	memset(&account_cond, 0, sizeof(acct_account_cond_t));
	memset(&assoc_cond, 0, sizeof(acct_association_cond_t));
	assoc_cond.acct_list = list_create(NULL);
	list_append(assoc_cond.acct_list, name);
	account_cond.assoc_cond = &assoc_cond;

	account_list = acct_storage_g_get_accounts(db_conn, my_uid,
						   &account_cond);
	
	list_destroy(assoc_cond.acct_list);

	if(account_list)
		account = list_pop(account_list);

	list_destroy(account_list);

	return account;
}

extern acct_cluster_rec_t *sacctmgr_find_cluster(char *name)
{
	acct_cluster_rec_t *cluster = NULL;
	acct_cluster_cond_t cluster_cond;
	List cluster_list = NULL;

	if(!name)
		return NULL;

	memset(&cluster_cond, 0, sizeof(acct_cluster_cond_t));
	cluster_cond.cluster_list = list_create(NULL);
	list_append(cluster_cond.cluster_list, name);

	cluster_list = acct_storage_g_get_clusters(db_conn, my_uid,
						   &cluster_cond);

	list_destroy(cluster_cond.cluster_list);

	if(cluster_list) 
		cluster = list_pop(cluster_list);

	list_destroy(cluster_list);
	
	return cluster;
}

extern acct_association_rec_t *sacctmgr_find_association_from_list(
	List assoc_list, char *user, char *account, 
	char *cluster, char *partition)
{
	ListIterator itr = NULL;
	acct_association_rec_t * assoc = NULL;
	
	if(!assoc_list)
		return NULL;
	
	itr = list_iterator_create(assoc_list);
	while((assoc = list_next(itr))) {
		if(((!user && assoc->user)
		    || (user && (!assoc->user
				 || strcasecmp(user, assoc->user))))
		   || ((!account && assoc->acct)
		       || (account && (!assoc->acct 
				       || strcasecmp(account, assoc->acct))))
		   || ((!cluster && assoc->cluster)
		       || (cluster && (!assoc->cluster 
				       || strcasecmp(cluster, assoc->cluster))))
		   || ((!partition && assoc->partition)
		       || (partition && (!assoc->partition 
					 || strcasecmp(partition, 
						       assoc->partition)))))
			continue;
		break;
	}
	list_iterator_destroy(itr);
	
	return assoc;
}

extern acct_association_rec_t *sacctmgr_find_account_base_assoc_from_list(
	List assoc_list, char *account, char *cluster)
{
	ListIterator itr = NULL;
	acct_association_rec_t *assoc = NULL;
	char *temp = "root";

	if(!cluster || !assoc_list)
		return NULL;

	if(account)
		temp = account;
	/* info("looking for %s %s in %d", account, cluster, */
/* 	     list_count(assoc_list)); */
	itr = list_iterator_create(assoc_list);
	while((assoc = list_next(itr))) {
		/* info("is it %s %s %s", assoc->user, assoc->acct, assoc->cluster); */
		if(assoc->user
		   || strcasecmp(temp, assoc->acct)
		   || strcasecmp(cluster, assoc->cluster))
			continue;
	/* 	info("found it"); */
		break;
	}
	list_iterator_destroy(itr);

	return assoc;
}

extern acct_qos_rec_t *sacctmgr_find_qos_from_list(
	List qos_list, char *name)
{
	ListIterator itr = NULL;
	acct_qos_rec_t *qos = NULL;
	char *working_name = NULL;
	
	if(!name || !qos_list)
		return NULL;

	if(name[0] == '+' || name[0] == '-')
		working_name = name+1;
	else
		working_name = name;
	
	itr = list_iterator_create(qos_list);
	while((qos = list_next(itr))) {
		if(!strcasecmp(working_name, qos->name))
			break;
	}
	list_iterator_destroy(itr);
	
	return qos;

}

extern acct_user_rec_t *sacctmgr_find_user_from_list(
	List user_list, char *name)
{
	ListIterator itr = NULL;
	acct_user_rec_t *user = NULL;
	
	if(!name || !user_list)
		return NULL;
	
	itr = list_iterator_create(user_list);
	while((user = list_next(itr))) {
		if(!strcasecmp(name, user->name))
			break;
	}
	list_iterator_destroy(itr);
	
	return user;

}

extern acct_account_rec_t *sacctmgr_find_account_from_list(
	List acct_list, char *name)
{
	ListIterator itr = NULL;
	acct_account_rec_t *account = NULL;
	
	if(!name || !acct_list)
		return NULL;

	itr = list_iterator_create(acct_list);
	while((account = list_next(itr))) {
		if(!strcasecmp(name, account->name))
			break;
	}
	list_iterator_destroy(itr);
	
	return account;

}

extern acct_cluster_rec_t *sacctmgr_find_cluster_from_list(
	List cluster_list, char *name)
{
	ListIterator itr = NULL;
	acct_cluster_rec_t *cluster = NULL;

	if(!name || !cluster_list)
		return NULL;

	itr = list_iterator_create(cluster_list);
	while((cluster = list_next(itr))) {
		if(!strcasecmp(name, cluster->name))
			break;
	}
	list_iterator_destroy(itr);
	
	return cluster;
}

extern acct_wckey_rec_t *sacctmgr_find_wckey_from_list(
	List wckey_list, char *user, char *name, char *cluster)
{
	ListIterator itr = NULL;
	acct_wckey_rec_t * wckey = NULL;
	
	if(!wckey_list)
		return NULL;
	
	itr = list_iterator_create(wckey_list);
	while((wckey = list_next(itr))) {
		if(((!user && wckey->user)
		    || (user && (!wckey->user
				 || strcasecmp(user, wckey->user))))
		   || ((!name && wckey->name)
		       || (name && (!wckey->name 
				    || strcasecmp(name, wckey->name))))
		   || ((!cluster && wckey->cluster)
		       || (cluster && (!wckey->cluster 
				       || strcasecmp(cluster,
						     wckey->cluster)))))
			continue;
		break;
	}
	list_iterator_destroy(itr);
	
	return wckey;
}

extern int get_uint(char *in_value, uint32_t *out_value, char *type)
{
	char *ptr = NULL, *meat = NULL;
	long num;
	
	if(!(meat = strip_quotes(in_value, NULL, 1))) {
		error("Problem with strip_quotes");
		return SLURM_ERROR;
	}
	num = strtol(meat, &ptr, 10);
	if ((num == 0) && ptr && ptr[0]) {
		error("Invalid value for %s (%s)", type, meat);
		xfree(meat);
		return SLURM_ERROR;
	}
	xfree(meat);
	
	if (num < 0)
		*out_value = INFINITE;		/* flag to clear */
	else
		*out_value = (uint32_t) num;
	return SLURM_SUCCESS;
}

extern int get_uint16(char *in_value, uint16_t *out_value, char *type)
{
	char *ptr = NULL, *meat = NULL;
	long num;
	
	if(!(meat = strip_quotes(in_value, NULL, 1))) {
		error("Problem with strip_quotes");
		return SLURM_ERROR;
	}

	num = strtol(meat, &ptr, 10);
	if ((num == 0) && ptr && ptr[0]) {
		error("Invalid value for %s (%s)", type, meat);
		xfree(meat);
		return SLURM_ERROR;
	}
	xfree(meat);
	
	if (num < 0)
		*out_value = (uint16_t) INFINITE; /* flag to clear */
	else
		*out_value = (uint16_t) num;
	return SLURM_SUCCESS;
}

extern int get_uint64(char *in_value, uint64_t *out_value, char *type)
{
	char *ptr = NULL, *meat = NULL;
	long long num;
	
	if(!(meat = strip_quotes(in_value, NULL, 1))) {
		error("Problem with strip_quotes");
		return SLURM_ERROR;
	}

	num = strtoll(meat, &ptr, 10);
	if ((num == 0) && ptr && ptr[0]) {
		error("Invalid value for %s (%s)", type, meat);
		xfree(meat);
		return SLURM_ERROR;
	}
	xfree(meat);
	
	if (num < 0)
		*out_value = INFINITE;		/* flag to clear */
	else
		*out_value = (uint64_t) num;
	return SLURM_SUCCESS;
}

extern int get_double(char *in_value, double *out_value, char *type)
{
	char *ptr = NULL, *meat = NULL;
	double num;
	
	if(!(meat = strip_quotes(in_value, NULL, 1))) {
		error("Problem with strip_quotes");
		return SLURM_ERROR;
	}
	num = strtod(meat, &ptr);
	if ((num == 0) && ptr && ptr[0]) {
		error("Invalid value for %s (%s)", type, meat);
		xfree(meat);
		return SLURM_ERROR;
	}
	xfree(meat);
	
	if (num < 0)
		*out_value = (double) INFINITE;		/* flag to clear */
	else
		*out_value = (double) num;
	return SLURM_SUCCESS;
}

extern int addto_qos_char_list(List char_list, List qos_list, char *names, 
			       int option)
{
	int i=0, start=0;
	char *name = NULL, *tmp_char = NULL;
	ListIterator itr = NULL;
	char quote_c = '\0';
	int quote = 0;
	uint32_t id=0;
	int count = 0;
	int equal_set = 0;
	int add_set = 0;

	if(!char_list) {
		error("No list was given to fill in");
		return 0;
	}

	if(!qos_list || !list_count(qos_list)) {
		debug2("No real qos_list");
		exit_code = 1;
		return 0;
	}

	itr = list_iterator_create(char_list);
	if(names) {
		if (names[i] == '\"' || names[i] == '\'') {
			quote_c = names[i];
			quote = 1;
			i++;
		}
		start = i;
		while(names[i]) {
			if(quote && names[i] == quote_c)
				break;
			else if (names[i] == '\"' || names[i] == '\'')
				names[i] = '`';
			else if(names[i] == ',') {
				if((i-start) > 0) {
					int tmp_option = option;
					if(names[start] == '+' 
					   || names[start] == '-') {
						tmp_option = names[start];
						start++;
					}
					name = xmalloc((i-start+1));
					memcpy(name, names+start, (i-start));
					
					id = str_2_acct_qos(qos_list, name);
					if(id == NO_VAL) {
						char *tmp = _get_qos_list_str(
							qos_list);
						error("You gave a bad qos "
						      "'%s'.  Valid QOS's are "
						      "%s",
						      name, tmp);
						xfree(tmp);
						exit_code = 1;
						xfree(name);
						break;
					}
					xfree(name);
					
					if(tmp_option) {
						if(equal_set) {
							error("You can't set "
							      "qos equal to "
							      "something and "
							      "then add or "
							      "subtract from "
							      "it in the same "
							      "line");
							exit_code = 1;
							break;
						}
						add_set = 1;
						name = xstrdup_printf(
							"%c%u", tmp_option, id);
					} else {
						if(add_set) {
							error("You can't set "
							      "qos equal to "
							      "something and "
							      "then add or "
							      "subtract from "
							      "it in the same "
							      "line");
							exit_code = 1;
							break;
						}
						equal_set = 1;
						name = xstrdup_printf("%u", id);
					}
					while((tmp_char = list_next(itr))) {
						if(!strcasecmp(tmp_char, name))
							break;
					}
					list_iterator_reset(itr);

					if(!tmp_char) {
						list_append(char_list, name);
						count++;
					} else 
						xfree(name);
				} else if (!(i-start)) {
					list_append(char_list, xstrdup(""));
					count++;
				}

				i++;
				start = i;
				if(!names[i]) {
					error("There is a problem with "
					      "your request.  It appears you "
					      "have spaces inside your list.");
					exit_code = 1;
					break;
				}
			}
			i++;
		}
		if((i-start) > 0) {
			int tmp_option = option;
			if(names[start] == '+' || names[start] == '-') {
				tmp_option = names[start];
				start++;
			}
			name = xmalloc((i-start)+1);
			memcpy(name, names+start, (i-start));
			
			id = str_2_acct_qos(qos_list, name);
			if(id == NO_VAL) {
				char *tmp = _get_qos_list_str(qos_list);
				error("You gave a bad qos "
				      "'%s'.  Valid QOS's are "
				      "%s",
				      name, tmp);
				xfree(tmp);
				xfree(name);
				goto end_it;
			}
			xfree(name);

			if(tmp_option) {
				if(equal_set) {
					error("You can't set "
					      "qos equal to "
					      "something and "
					      "then add or "
					      "subtract from "
					      "it in the same "
					      "line");
					exit_code = 1;
					goto end_it;
				}
				name = xstrdup_printf(
					"%c%u", tmp_option, id);
			} else {
				if(add_set) {
					error("You can't set "
					      "qos equal to "
					      "something and "
					      "then add or "
					      "subtract from "
					      "it in the same "
					      "line");
					exit_code = 1;
					goto end_it;
				}
				name = xstrdup_printf("%u", id);
			}
			while((tmp_char = list_next(itr))) {
				if(!strcasecmp(tmp_char, name))
					break;
			}
			
			if(!tmp_char) {
				list_append(char_list, name);
				count++;
			} else 
				xfree(name);
		} else if (!(i-start)) {
			list_append(char_list, xstrdup(""));
			count++;
		}
	}	
	if(!count) {
		error("You gave me an empty qos list");
		exit_code = 1;
	}

end_it:
	list_iterator_destroy(itr);
	return count;
}
 
extern int addto_action_char_list(List char_list, char *names)
{
	int i=0, start=0;
	char *name = NULL, *tmp_char = NULL;
	ListIterator itr = NULL;
	char quote_c = '\0';
	int quote = 0;
	uint32_t id=0;
	int count = 0;

	if(!char_list) {
		error("No list was given to fill in");
		return 0;
	}

	itr = list_iterator_create(char_list);
	if(names) {
		if (names[i] == '\"' || names[i] == '\'') {
			quote_c = names[i];
			quote = 1;
			i++;
		}
		start = i;
		while(names[i]) {
			if(quote && names[i] == quote_c)
				break;
			else if (names[i] == '\"' || names[i] == '\'')
				names[i] = '`';
			else if(names[i] == ',') {
				if((i-start) > 0) {
					name = xmalloc((i-start+1));
					memcpy(name, names+start, (i-start));
					
					id = str_2_slurmdbd_msg_type(name);
					if(id == NO_VAL) {
						error("You gave a bad action "
						      "'%s'.", name);
						xfree(name);
						break;
					}
					xfree(name);

					name = xstrdup_printf("%u", id);
					while((tmp_char = list_next(itr))) {
						if(!strcasecmp(tmp_char, name))
							break;
					}
					list_iterator_reset(itr);

					if(!tmp_char) {
						list_append(char_list, name);
						count++;
					} else 
						xfree(name);
				}

				i++;
				start = i;
				if(!names[i]) {
					error("There is a problem with "
					      "your request.  It appears you "
					      "have spaces inside your list.");
					break;
				}
			}
			i++;
		}
		if((i-start) > 0) {
			name = xmalloc((i-start)+1);
			memcpy(name, names+start, (i-start));
			
			id = str_2_slurmdbd_msg_type(name);
			if(id == NO_VAL)  {
				error("You gave a bad action '%s'.",
				      name);
				xfree(name);
				goto end_it;
			}
			xfree(name);
			
			name = xstrdup_printf("%u", id);
			while((tmp_char = list_next(itr))) {
				if(!strcasecmp(tmp_char, name))
					break;
			}
			
			if(!tmp_char) {
				list_append(char_list, name);
				count++;
			} else 
				xfree(name);
		}
	}	
end_it:
	list_iterator_destroy(itr);
	return count;
}
 
extern List copy_char_list(List char_list) 
{
	List ret_list = NULL;
	char *tmp_char = NULL;
	ListIterator itr = NULL;

	if(!char_list || !list_count(char_list))
		return NULL;

	itr = list_iterator_create(char_list);
	ret_list = list_create(slurm_destroy_char);
	
	while((tmp_char = list_next(itr))) 
		list_append(ret_list, xstrdup(tmp_char));
	
	list_iterator_destroy(itr);
	
	return ret_list;
}

extern void sacctmgr_print_coord_list(
	print_field_t *field, List value, int last)
{
	ListIterator itr = NULL;
	char *print_this = NULL;
	acct_coord_rec_t *object = NULL;
	
	if(!value || !list_count(value)) {
		if(print_fields_parsable_print)
			print_this = xstrdup("");
		else
			print_this = xstrdup(" ");
	} else {
		list_sort(value, (ListCmpF)sort_coord_list);
		itr = list_iterator_create(value);
		while((object = list_next(itr))) {
			if(print_this) 
				xstrfmtcat(print_this, ",%s", 
					   object->name);
			else 
				print_this = xstrdup(object->name);
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

extern void sacctmgr_print_qos_list(print_field_t *field, List qos_list,
				    List value, int last)
{
	char *print_this = NULL;

	print_this = get_qos_complete_str(qos_list, value);
	
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

extern void sacctmgr_print_assoc_limits(acct_association_rec_t *assoc)
{
	if(!assoc)
		return;

	if(assoc->shares_raw == INFINITE)
		printf("  Fairshare     = NONE\n");
	else if(assoc->shares_raw != NO_VAL) 
		printf("  Fairshare     = %u\n", assoc->shares_raw);

	if(assoc->grp_cpu_mins == INFINITE)
		printf("  GrpCPUMins    = NONE\n");
	else if(assoc->grp_cpu_mins != NO_VAL) 
		printf("  GrpCPUMins    = %llu\n", 
		       (long long unsigned)assoc->grp_cpu_mins);
		
	if(assoc->grp_cpus == INFINITE)
		printf("  GrpCPUs       = NONE\n");
	else if(assoc->grp_cpus != NO_VAL) 
		printf("  GrpCPUs       = %u\n", assoc->grp_cpus);
				
	if(assoc->grp_jobs == INFINITE) 
		printf("  GrpJobs       = NONE\n");
	else if(assoc->grp_jobs != NO_VAL) 
		printf("  GrpJobs       = %u\n", assoc->grp_jobs);
		
	if(assoc->grp_nodes == INFINITE)
		printf("  GrpNodes      = NONE\n");
	else if(assoc->grp_nodes != NO_VAL)
		printf("  GrpNodes      = %u\n", assoc->grp_nodes);
		
	if(assoc->grp_submit_jobs == INFINITE) 
		printf("  GrpSubmitJobs = NONE\n");
	else if(assoc->grp_submit_jobs != NO_VAL) 
		printf("  GrpSubmitJobs = %u\n", 
		       assoc->grp_submit_jobs);
		
	if(assoc->grp_wall == INFINITE) 
		printf("  GrpWall       = NONE\n");		
	else if(assoc->grp_wall != NO_VAL) {
		char time_buf[32];
		mins2time_str((time_t) assoc->grp_wall, 
			      time_buf, sizeof(time_buf));
		printf("  GrpWall       = %s\n", time_buf);
	}

	if(assoc->max_cpu_mins_pj == INFINITE)
		printf("  MaxCPUMins    = NONE\n");
	else if(assoc->max_cpu_mins_pj != NO_VAL) 
		printf("  MaxCPUMins    = %llu\n", 
		       (long long unsigned)assoc->max_cpu_mins_pj);
		
	if(assoc->max_cpus_pj == INFINITE)
		printf("  MaxCPUs       = NONE\n");
	else if(assoc->max_cpus_pj != NO_VAL) 
		printf("  MaxCPUs       = %u\n", assoc->max_cpus_pj);
				
	if(assoc->max_jobs == INFINITE) 
		printf("  MaxJobs       = NONE\n");
	else if(assoc->max_jobs != NO_VAL) 
		printf("  MaxJobs       = %u\n", assoc->max_jobs);
		
	if(assoc->max_nodes_pj == INFINITE)
		printf("  MaxNodes      = NONE\n");
	else if(assoc->max_nodes_pj != NO_VAL)
		printf("  MaxNodes      = %u\n", assoc->max_nodes_pj);
		
	if(assoc->max_submit_jobs == INFINITE) 
		printf("  MaxSubmitJobs = NONE\n");
	else if(assoc->max_submit_jobs != NO_VAL) 
		printf("  MaxSubmitJobs = %u\n", 
		       assoc->max_submit_jobs);
		
	if(assoc->max_wall_pj == INFINITE) 
		printf("  MaxWall       = NONE\n");		
	else if(assoc->max_wall_pj != NO_VAL) {
		char time_buf[32];
		mins2time_str((time_t) assoc->max_wall_pj, 
			      time_buf, sizeof(time_buf));
		printf("  MaxWall       = %s\n", time_buf);
	}

	if(assoc->qos_list) {
		List qos_list = acct_storage_g_get_qos(db_conn, my_uid, NULL);
		char *temp_char = get_qos_complete_str(qos_list,
						       assoc->qos_list);
		if(temp_char) {		
			printf("  QOS           = %s\n", temp_char);
			xfree(temp_char);
		}
		if(qos_list)
			list_destroy(qos_list);
	} 
}

extern void sacctmgr_print_qos_limits(acct_qos_rec_t *qos)
{
	List qos_list = NULL;
	if(!qos)
		return;

	if(qos->preemptee_list || qos->preemptor_list)
		qos_list = acct_storage_g_get_qos(db_conn, my_uid, NULL);

	if(qos->job_flags)
		printf("  JobFlags       = %s", qos->job_flags);

	if(qos->grp_cpu_mins == INFINITE)
		printf("  GrpCPUMins     = NONE\n");
	else if(qos->grp_cpu_mins != NO_VAL) 
		printf("  GrpCPUMins     = %llu\n", 
		       (long long unsigned)qos->grp_cpu_mins);
		
	if(qos->grp_cpus == INFINITE)
		printf("  GrpCPUs        = NONE\n");
	else if(qos->grp_cpus != NO_VAL) 
		printf("  GrpCPUs        = %u\n", qos->grp_cpus);
				
	if(qos->grp_jobs == INFINITE) 
		printf("  GrpJobs        = NONE\n");
	else if(qos->grp_jobs != NO_VAL) 
		printf("  GrpJobs        = %u\n", qos->grp_jobs);
		
	if(qos->grp_nodes == INFINITE)
		printf("  GrpNodes       = NONE\n");
	else if(qos->grp_nodes != NO_VAL)
		printf("  GrpNodes       = %u\n", qos->grp_nodes);
		
	if(qos->grp_submit_jobs == INFINITE) 
		printf("  GrpSubmitJobs  = NONE\n");
	else if(qos->grp_submit_jobs != NO_VAL) 
		printf("  GrpSubmitJobs  = %u\n", 
		       qos->grp_submit_jobs);
		
	if(qos->grp_wall == INFINITE) 
		printf("  GrpWall        = NONE\n");		
	else if(qos->grp_wall != NO_VAL) {
		char time_buf[32];
		mins2time_str((time_t) qos->grp_wall, 
			      time_buf, sizeof(time_buf));
		printf("  GrpWall        = %s\n", time_buf);
	}

	if(qos->max_cpu_mins_pu == INFINITE)
		printf("  MaxCPUMins     = NONE\n");
	else if(qos->max_cpu_mins_pu != NO_VAL) 
		printf("  MaxCPUMins     = %llu\n", 
		       (long long unsigned)qos->max_cpu_mins_pu);
		
	if(qos->max_cpus_pu == INFINITE)
		printf("  MaxCPUs        = NONE\n");
	else if(qos->max_cpus_pu != NO_VAL) 
		printf("  MaxCPUs        = %u\n", qos->max_cpus_pu);
				
	if(qos->max_jobs_pu == INFINITE) 
		printf("  MaxJobs        = NONE\n");
	else if(qos->max_jobs_pu != NO_VAL) 
		printf("  MaxJobs        = %u\n", qos->max_jobs_pu);
		
	if(qos->max_nodes_pu == INFINITE)
		printf("  MaxNodes       = NONE\n");
	else if(qos->max_nodes_pu != NO_VAL)
		printf("  MaxNodes       = %u\n", qos->max_nodes_pu);
		
	if(qos->max_submit_jobs_pu == INFINITE) 
		printf("  MaxSubmitJobs  = NONE\n");
	else if(qos->max_submit_jobs_pu != NO_VAL) 
		printf("  MaxSubmitJobs  = %u\n", 
		       qos->max_submit_jobs_pu);
		
	if(qos->max_wall_pu == INFINITE) 
		printf("  MaxWall        = NONE\n");		
	else if(qos->max_wall_pu != NO_VAL) {
		char time_buf[32];
		mins2time_str((time_t) qos->max_wall_pu, 
			      time_buf, sizeof(time_buf));
		printf("  MaxWall        = %s\n", time_buf);
	}

	if(qos->preemptee_list) {
		char *temp_char = get_qos_complete_str(qos_list,
						       qos->preemptee_list);
		if(temp_char) {		
			printf("  Preemptable by = %s\n", temp_char);
			xfree(temp_char);
		}
	}
	if(qos->preemptor_list) {
		char *temp_char = get_qos_complete_str(qos_list,
						       qos->preemptee_list);
		if(temp_char) {		
			printf("  Can Preempt    = %s\n", temp_char);
			xfree(temp_char);
		}
	} 

	if(qos->priority == INFINITE)
		printf("  Priority       = NONE\n");
	else if(qos->priority != NO_VAL) 
		printf("  Priority       = %d\n", qos->priority);

	if(qos_list)
		list_destroy(qos_list);
}

extern int sort_coord_list(acct_coord_rec_t *coord_a, acct_coord_rec_t *coord_b)
{
	int diff = strcmp(coord_a->name, coord_b->name);

	if (diff < 0)
		return -1;
	else if (diff > 0)
		return 1;
	
	return 0;
}

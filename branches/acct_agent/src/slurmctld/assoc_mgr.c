/*****************************************************************************\
 *  accounting_storage_slurmdbd.c - accounting interface to slurmdbd.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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
#include <pwd.h>
#include <sys/types.h>

#include "assoc_mgr.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/xstring.h"


static List local_association_list = NULL;
static List local_user_list = NULL;
static pthread_mutex_t local_association_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t local_user_lock = PTHREAD_MUTEX_INITIALIZER;

static slurm_fd assoc_server_fd = -1;
static pthread_t assoc_thread = 0;
static time_t assoc_shutdown = 0;

static int _get_local_association_list(void *db_conn)
{
	acct_association_cond_t assoc_q;
	char *cluster_name = NULL;

	slurm_mutex_lock(&local_association_lock);
	if(local_association_list)
		list_destroy(local_association_list);

	memset(&assoc_q, 0, sizeof(acct_association_cond_t));
	assoc_q.cluster_list = list_create(slurm_destroy_char);
	cluster_name = xstrdup(slurmctld_cluster_name);
	if(!cluster_name) {
		if(slurmctld_conf.accounting_storage_enforce) {
			error("_get_local_association_list: "
			      "no cluster name here going to get "
			      "all associations.");
		}
	} else 
		list_append(assoc_q.cluster_list, cluster_name);
	
	local_association_list =
		acct_storage_g_get_associations(db_conn, &assoc_q);
	list_destroy(assoc_q.cluster_list);
	
	if(!local_association_list) {
		if(slurmctld_conf.accounting_storage_enforce) {
			error("_get_local_association_list: "
			      "no list was made.");
			slurm_mutex_unlock(&local_association_lock);
			return SLURM_ERROR;
		} else {
			return SLURM_SUCCESS;
		}
	} else {
		acct_association_rec_t *assoc = NULL;
		struct passwd *passwd_ptr = NULL;
		ListIterator itr = list_iterator_create(local_association_list);
		while((assoc = list_next(itr))) {
			passwd_ptr = getpwnam(assoc->user);
			if(passwd_ptr) 
				assoc->uid = passwd_ptr->pw_uid;
		}
		list_iterator_destroy(itr);
	}
	slurm_mutex_unlock(&local_association_lock);

	return SLURM_SUCCESS;
}

static int _get_local_user_list(void *db_conn)
{
	acct_user_cond_t user_q;

	memset(&user_q, 0, sizeof(acct_user_cond_t));

	slurm_mutex_lock(&local_user_lock);
	if(local_user_list)
		list_destroy(local_user_list);
	local_user_list = acct_storage_g_get_users(db_conn, &user_q);

	if(!local_user_list) {
		if(slurmctld_conf.accounting_storage_enforce) {
			error("_get_local_user_list: "
			      "no list was made.");
			slurm_mutex_unlock(&local_user_lock);
			return SLURM_ERROR;
		} else {
			return SLURM_SUCCESS;
		}		
	} else {
		acct_user_rec_t *user = NULL;
		struct passwd *passwd_ptr = NULL;
		ListIterator itr = list_iterator_create(local_user_list);
		while((user = list_next(itr))) {
			passwd_ptr = getpwnam(user->name);
			if(passwd_ptr) 
				user->uid = passwd_ptr->pw_uid;
		}
		list_iterator_destroy(itr);
	}

	slurm_mutex_unlock(&local_user_lock);
	return SLURM_SUCCESS;
}

static void *_assoc_agent(void *args)
{
	slurm_fd newsockfd;
	slurm_addr cli_addr;
	slurmdbd_msg_t msg;

	while (assoc_shutdown == 0) {
		if((newsockfd = slurm_accept_msg_conn(assoc_server_fd,
						      &cli_addr)) ==
		    SLURM_SOCKET_ERROR) {
			if(errno != EINTR)
				error("slurm_accept_msg_conn: %m");
			continue;
		}
		if(assoc_shutdown)
			break;
		if(slurm_recv_slurmdbd_msg(newsockfd, &msg)
		   != SLURM_SUCCESS) {
			error("slurm_recv_slurmdbd_msg: %m");
		} else {
			info("Received some message from SlurmDBD");
			/* NOTE: authentication should be handle within the
			 *	message un/pack for relevant messages */
			/* FIXME: process the RPC and free the message */
		}
		slurm_shutdown_msg_conn(newsockfd);
	}

	return NULL;
}

extern uint16_t assoc_mgr_server(void)
{
	slurm_addr assoc_addr;
	uint16_t assoc_port = 0;
	pthread_attr_t attr;

	if (assoc_server_fd >= 0) {
		error("Association server already spawned");
		return assoc_port;
	}

	assoc_server_fd = slurm_init_msg_engine_port(0);
	if (assoc_server_fd < 0)
		fatal("slurm_init_msg_engine_port: %m");
	if (slurm_get_stream_addr(assoc_server_fd, &assoc_addr) < 0)
                fatal("slurm_get_stream_addr: %m");
        assoc_port = ntohs(((struct sockaddr_in) assoc_addr).sin_port);

	slurm_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&assoc_thread, &attr, _assoc_agent, NULL)) {
		error("Unable to start association agent: %m");
		if(slurm_shutdown_msg_engine(assoc_server_fd))
			error("slurm_shutdown_msg_engine %m");
		assoc_server_fd = -1;
		assoc_port = 0;
	}
	slurm_attr_destroy( &attr );

	return assoc_port;
}

extern int assoc_mgr_init(void *db_conn)
{
	if(!slurmctld_cluster_name)
		slurmctld_cluster_name = slurm_get_cluster_name();
	
	if(!local_association_list) 
		if(_get_local_association_list(db_conn) == SLURM_ERROR)
			return SLURM_ERROR;
	if(!local_user_list)
		if(_get_local_user_list(db_conn) == SLURM_ERROR)
			return SLURM_ERROR;

	return SLURM_SUCCESS;
}

extern int assoc_mgr_fini(void)
{
	if(local_association_list) 
		list_destroy(local_association_list);
	if(local_user_list)
		list_destroy(local_user_list);
	local_association_list = NULL;
	local_user_list = NULL;

	if(assoc_server_fd >= 0) {
		int i;
		assoc_shutdown = time(NULL);
		for(i=0; i<4; i++) {
			if(pthread_cancel(assoc_thread))
				break;;
			usleep(1000);
		}
		if(i >= 4)
			error("Could not kill assoc_thread");
		if(slurm_shutdown_msg_engine(assoc_server_fd))
			error("slurm_shutdown_msg_engine %m");
		assoc_server_fd = -1;
	}

	return SLURM_SUCCESS;
}

extern int get_default_account(void *db_conn, acct_user_rec_t *user)
{
	ListIterator itr = NULL;
	acct_user_rec_t * found_user = NULL;

	if(!local_user_list) 
		if(_get_local_user_list(db_conn) == SLURM_ERROR)
			return SLURM_ERROR;

	if(!local_user_list 
	   && !slurmctld_conf.accounting_storage_enforce) 
		return SLURM_SUCCESS;
	
	slurm_mutex_lock(&local_user_lock);
	itr = list_iterator_create(local_user_list);
	while((found_user = list_next(itr))) {
		if(user->uid == found_user->uid) 
			break;
	}
	list_iterator_destroy(itr);

	if(found_user) {
		user->name = found_user->name;
		user->default_acct = found_user->default_acct;
		slurm_mutex_unlock(&local_user_lock);
		return SLURM_SUCCESS;
	}
	slurm_mutex_unlock(&local_user_lock);
	return SLURM_ERROR;
}

extern int get_assoc_id(void *db_conn, acct_association_rec_t *assoc)
{
	ListIterator itr = NULL;
	acct_association_rec_t * found_assoc = NULL;
	acct_association_rec_t * ret_assoc = NULL;
	
	if(!local_association_list) 
		if(_get_local_association_list(db_conn) == SLURM_ERROR)
			return SLURM_ERROR;

	if(!local_association_list 
	   && !slurmctld_conf.accounting_storage_enforce) 
		return SLURM_SUCCESS;

	if(!assoc->id) {
		if(!assoc->acct) {
			acct_user_rec_t user;

			if(!assoc->uid) {
				if(slurmctld_conf.accounting_storage_enforce) {
					error("get_assoc_id: "
					      "Not enough info to "
					      "get an association");
					return SLURM_ERROR;
				} else
					return SLURM_SUCCESS;

			}
			memset(&user, 0, sizeof(acct_user_rec_t));
			user.uid = assoc->uid;
			if(get_default_account(db_conn, &user) == SLURM_ERROR) {
				if(slurmctld_conf.accounting_storage_enforce) 
					return SLURM_ERROR;
				else
					return SLURM_SUCCESS;
			}					
			assoc->user = user.name;
			assoc->acct = user.default_acct;
		} 
		
		if(!assoc->cluster)
			assoc->cluster = slurmctld_cluster_name;
	}
/* 	info("looking for assoc of user=%u, acct=%s, cluster=%s, partition=%s", */
/* 	     assoc->uid, assoc->acct, assoc->cluster, assoc->partition); */
	slurm_mutex_lock(&local_association_lock);
	itr = list_iterator_create(local_association_list);
	while((found_assoc = list_next(itr))) {
		if(assoc->id) {
			if(assoc->id == found_assoc->id) {
				ret_assoc = found_assoc;
				break;
			}
			continue;
		} else {
			if(!assoc->user && found_assoc->user 
			   && strcasecmp("none", found_assoc->user)) {
				debug3("we are looking for a "
				       "nonuser association");
				continue;
			} else if(assoc->uid != found_assoc->uid) {
				debug3("not the right user");
				continue;
			}
			
			if(found_assoc->acct 
			   && strcasecmp(assoc->acct, found_assoc->acct)) {
				   debug3("not the right account");
				   continue;
			}
/* We shouldn't have to do this since we only have this clusters
 * assocs here */
/* 			if(found_assoc->cluster  */
/* 			   && strcasecmp(assoc->cluster, */
/* 					 found_assoc->cluster)) { */
/* 				debug3("not the right cluster"); */
/* 				continue; */
/* 			} */
	
			if(assoc->partition
			   && (!found_assoc->partition 
			       || strcasecmp(assoc->partition, 
					     found_assoc->partition))) {
				ret_assoc = found_assoc;
				debug3("found association for no partition");
				continue;
			}
		}
		debug3("found correct association");
		ret_assoc = found_assoc;
		break;
	}
	list_iterator_destroy(itr);
	
	if(!ret_assoc) {
		slurm_mutex_unlock(&local_association_lock);
		if(slurmctld_conf.accounting_storage_enforce) 
			return SLURM_ERROR;
		else
			return SLURM_SUCCESS;
	}

	assoc->id = ret_assoc->id;
	if(!assoc->user)
		assoc->user = ret_assoc->user;
	if(!assoc->acct)
		assoc->acct = ret_assoc->acct;
	if(!assoc->cluster)
		assoc->cluster = ret_assoc->cluster;
	if(!assoc->partition)
		assoc->partition = ret_assoc->partition;
	slurm_mutex_unlock(&local_association_lock);

	return SLURM_SUCCESS;
}

extern int remove_local_association(uint32_t id)
{
	ListIterator itr = NULL;
	acct_association_rec_t * found_assoc = NULL;

	if(!local_association_list)
		return SLURM_SUCCESS;

	slurm_mutex_lock(&local_association_lock);
	itr = list_iterator_create(local_association_list);
	while((found_assoc = list_next(itr))) {
		if(id == found_assoc->id) {
			list_delete_item(itr);			
			break;
		}
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&local_association_lock);

	return SLURM_SUCCESS;
}

extern int remove_local_user(char *name)
{
	ListIterator itr = NULL;
	acct_user_rec_t * found_user = NULL;
	acct_association_rec_t * found_assoc = NULL;

	if(!local_user_list)
		return SLURM_SUCCESS;
	
	slurm_mutex_lock(&local_user_lock);
	itr = list_iterator_create(local_user_list);
	while((found_user = list_next(itr))) {
		if(!strcasecmp(name, found_user->name)) {
			list_delete_item(itr);
			break;
		}
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&local_user_lock);

	if(!local_association_list)
		return SLURM_SUCCESS;

	slurm_mutex_lock(&local_association_lock);
	itr = list_iterator_create(local_user_list);
	while((found_assoc = list_next(itr))) {
		if(!strcasecmp(name, found_assoc->user)) 
			list_delete_item(itr);			
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&local_association_lock);

	return SLURM_SUCCESS;
}

extern int update_local_associations(List update_list)
{
	acct_association_rec_t * rec = NULL;
	acct_association_rec_t * update_rec = NULL;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	int rc = SLURM_SUCCESS;

	if(!local_association_list)
		return SLURM_SUCCESS;

	slurm_mutex_lock(&local_association_lock);
	itr = list_iterator_create(update_list);
	itr2 = list_iterator_create(local_user_list);
	while((update_rec = list_next(itr2))) {
		list_iterator_reset(itr2);
		while((rec = list_next(itr2))) {
			if(update_rec->id == rec->id)
				break;
		}
		if(!rec) {
			rc = SLURM_ERROR;
			break;
		}
		/****** FIX ME UPDATE THE STUFF HERE ******/
	}
	slurm_mutex_unlock(&local_association_lock);

	list_iterator_destroy(itr);
	list_iterator_destroy(itr2);

	return rc;	
}

extern int update_local_users(List update_list)
{
	acct_user_rec_t * rec = NULL;
	acct_user_rec_t * update_rec = NULL;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	int rc = SLURM_SUCCESS;

	if(!local_user_list)
		return SLURM_SUCCESS;

	slurm_mutex_lock(&local_user_lock);
	itr = list_iterator_create(update_list);
	itr2 = list_iterator_create(local_user_list);
	while((update_rec = list_next(itr2))) {
		list_iterator_reset(itr2);
		while((rec = list_next(itr2))) {
			if(!strcasecmp(update_rec->name, rec->name))
				break;
		}
		if(!rec) {
			rc = SLURM_ERROR;
			break;
		}
		/****** FIX ME UPDATE THE STUFF HERE ******/
	}
	slurm_mutex_unlock(&local_user_lock);

	list_iterator_destroy(itr);
	list_iterator_destroy(itr2);

	return rc;	
}

extern int validate_assoc_id(void *db_conn, uint32_t assoc_id)
{
	ListIterator itr = NULL;
	acct_association_rec_t * found_assoc = NULL;

	if(!local_association_list) 
		if(_get_local_association_list(db_conn) == SLURM_ERROR)
			return SLURM_ERROR;

	if(!local_association_list 
	   && !slurmctld_conf.accounting_storage_enforce) 
		return SLURM_SUCCESS;
	
	slurm_mutex_lock(&local_association_lock);
	itr = list_iterator_create(local_association_list);
	while((found_assoc = list_next(itr))) {
		if(assoc_id == found_assoc->id) 
			break;
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&local_association_lock);

	if(found_assoc)
		return SLURM_SUCCESS;
	return SLURM_ERROR;
}


/*****************************************************************************\
 *  gold_interface.h - interface to the gold daemon commands.
 *
 *  $Id: storage_filetxt.c 10893 2007-01-29 21:53:48Z da $
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
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

#include "gold_interface.h"
#include "base64.h"

#include <openssl/hmac.h>
#include <openssl/sha.h>

#include "src/common/slurm_protocol_interface.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/uid.h"

#define MAX_RETRY 5

/* This should be updated to match the gold_object_t enum */
char *GOLD_OBJECT_STR[] = {
	"Account", 
	"User", 
	"Project", 
	"Machine", 
	"Job", 
	"RoleUser", 
	"EventLog",
	"MachineHourUsage", 
	"MachineDayUsage", 
	"MachineMonthUsage",
	"AccountHourUsage", 
	"AccountDayUsage", 
	"AccountMonthUsage",
	NULL
};

static char *gold_key = NULL;
static char *gold_host = NULL;
static uint16_t gold_port = 0;
static int gold_init = 0;
pthread_mutex_t gold_mutex = PTHREAD_MUTEX_INITIALIZER;

static char *_get_return_value(char *gold_msg, int *i)
{
	char tmp_buff[256];
	int j=0;
	int pos = (*i);

	while(gold_msg[pos] && j < 256) {
		if(gold_msg[pos] == '<') 
			break;
		
		tmp_buff[j++] = gold_msg[pos++];
	}

	while(gold_msg[pos] && gold_msg[pos] != '>')
		pos++;

	(*i) = pos;

	tmp_buff[j] = '\0';
	return xstrdup(tmp_buff);
}

static char *_get_return_name(char *gold_msg, int *i)
{
	char tmp_buff[256];
	int j=0;
	int pos = *i;

	while(gold_msg[pos] && j < 256) {
		if(gold_msg[pos] == '>') 
			break;
		
		tmp_buff[j++] = gold_msg[pos++];
	}

	(*i) = pos+1;

	tmp_buff[j] = '\0';
	return xstrdup(tmp_buff);
}

static gold_response_entry_t *_create_response_entry(char *object,
						     char *gold_msg, int *i) 
{
	gold_response_entry_t *resp_entry =
		xmalloc(sizeof(gold_response_entry_t));
	gold_name_value_t *name_val = NULL;
	int olen = strlen(object);

	/* FIXME: we might want to check if the last char was a < to
	 * add this if it is 
	 */
	(*i) += (olen + 1); //assume what is coming in is the name
	resp_entry->name_val = list_create(destroy_gold_name_value);
	while(gold_msg[*i]) {
		if(!strncmp(gold_msg+(*i), object, olen)) {
			(*i) += (olen + 1); //get to the end of the object
			break;
		} else if(gold_msg[(*i)] == '<' && gold_msg[(*i)+1] != '/') {
			// found the front of a selection
			(*i)++;
			
			name_val = xmalloc(sizeof(gold_name_value_t));
			name_val->name = _get_return_name(gold_msg, i);
			name_val->value = _get_return_value(gold_msg, i);
			
			debug4("got %s = %s", name_val->name, name_val->value);
			list_append(resp_entry->name_val, name_val);
		}
		(*i)++;
	}

	return resp_entry;
}

static slurm_fd _start_communication()
{
	static slurm_addr gold_addr;
	static int gold_addr_set = 0;
	char *init_msg = "POST /SSSRMAP3 HTTP/1.1\r\nContent-Type: text/xml; charset=\"utf-8\"\r\nTransfer-Encoding: chunked\r\n\r\n";
	int rc = 0;
	slurm_fd gold_fd = 0;

	if(!gold_init) {
		error("start_gold_communication: "
		      "need to run setup_gold_info before this");
		return 0;
	}
	
	if(!gold_addr_set) {
		slurm_set_addr(&gold_addr, gold_port, gold_host);
		gold_addr_set = 1;
	}
	
	if ((gold_fd = slurm_open_msg_conn(&gold_addr)) < 0) {
		error("start_gold_communication to %s: %m", gold_host);
		return 0;
	}

	debug3("Connected to %s(%d)", gold_host, gold_port);
	rc = _slurm_send_timeout(gold_fd, init_msg, strlen(init_msg),
				 SLURM_PROTOCOL_NO_SEND_RECV_FLAGS,
				 (slurm_get_msg_timeout() * 1000));
	
	if (rc < 0) {
		error("_slurm_send_timeout: %m");
		return 0;
	}
	return gold_fd;
}

static int _end_communication(slurm_fd gold_fd)
{
	int rc = SLURM_SUCCESS;
	int retry = 0;
	/* 
	 *  Attempt to close an open connection
	 */
	while ((slurm_shutdown_msg_conn(gold_fd) < 0) && (errno == EINTR)) {
		if (retry++ > MAX_RETRY) {
			rc = SLURM_ERROR;
			break;
		}
	}
	return rc;
}

extern int init_gold(char *keyfile, char *host, uint16_t port)
{
	int fp;
	char key[256];
	int i, bytes_read;
	
	if(!keyfile || !host) {
		error("init_gold: Either no keyfile or host given");
		return SLURM_ERROR;
	}

	fp = open(keyfile, O_RDONLY);
	if (fp < 0)
		fatal("Error opening gold keyfile (%s): %m\n", keyfile);
	bytes_read = read(fp, key, sizeof(key) - 1);
	if (bytes_read == -1) {
		fatal("Error reading hash key from keyfile (%s): %m\n",
		      keyfile);
	}
	key[bytes_read] = '\0'; /* Null terminate the string */
	for (i = 0; i<bytes_read; i++) /* Remove carriage return if any */
	{
		if (key[i] == '\n' || key[i] == '\r') {
			key[i] = '\0';
			break;
		}
	}
	
	/* Close the file */
	close(fp);
	//debug4("got the tolken as %s\n", key);
	gold_key = xstrdup(key);
	gold_host = xstrdup(host);
	gold_port = port;
	gold_init = 1;
	
	return SLURM_SUCCESS;
}

extern int fini_gold()
{
	gold_init = 0;
	xfree(gold_key);
	xfree(gold_host);
	
	return SLURM_SUCCESS;
}

extern gold_request_t *create_gold_request(gold_object_t object,
					   gold_object_t action)
{
	gold_request_t *gold_request = NULL;
	
	gold_request = xmalloc(sizeof(gold_request_t));

	gold_request->object = object;
	gold_request->action = action;
	gold_request->assignments = list_create(destroy_gold_name_value);
	gold_request->conditions = list_create(destroy_gold_name_value);
	gold_request->selections = list_create(destroy_gold_char);
	
	return gold_request;
}

extern int destroy_gold_request(gold_request_t *gold_request)
{
	if(gold_request) {
		if(gold_request->assignments)
			list_destroy(gold_request->assignments);
		if(gold_request->conditions)
			list_destroy(gold_request->conditions);
		if(gold_request->selections)
			list_destroy(gold_request->selections);
		xfree(gold_request->body);
		xfree(gold_request->digest);
		xfree(gold_request->signature);
		xfree(gold_request);
	}
	return SLURM_SUCCESS;
}

extern int gold_request_add_assignment(gold_request_t *gold_request, 
				       char *name, char *value)
{
	gold_name_value_t *name_val = xmalloc(sizeof(gold_name_value_t));
	name_val->name = xstrdup(name);
	name_val->value = xstrdup(value);
	list_append(gold_request->assignments, name_val);
		
	return SLURM_SUCCESS;
}

extern int gold_request_add_condition(gold_request_t *gold_request, 
				      char *name, char *value,
				      gold_operator_t op,
				      int or_statement)
{
	gold_name_value_t *name_val = xmalloc(sizeof(gold_name_value_t));
	name_val->name = xstrdup(name);
	name_val->value = xstrdup(value);
	name_val->op = op;
	name_val->or_statement = or_statement;
	list_append(gold_request->conditions, name_val);
		
	return SLURM_SUCCESS;
}

extern int gold_request_add_selection(gold_request_t *gold_request, char *name)
{
	list_append(gold_request->selections, xstrdup(name));
	return SLURM_SUCCESS;
}


extern gold_response_t *get_gold_response(gold_request_t *gold_request)
{
	unsigned int slen = EVP_MAX_MD_SIZE;
	unsigned int dlen = SHA_DIGEST_LENGTH;
	unsigned char digest[dlen];
	unsigned char signature[slen];
	char c, *user_name;
	char *object = NULL;
	char *action = NULL;
	char *innerds = NULL;
	char *gold_msg = NULL;
	char tmp_buff[256];
	char *tmp_char = NULL;
	uint32_t ret_len = 0;
	static int timeout = 0;
	gold_response_t *gold_response = NULL;
	gold_name_value_t *name_val = NULL;
	ListIterator itr = NULL;
	int rc = 0, i = 0;
	slurm_fd gold_fd = 0;

	if(!gold_init) {
		error("get_gold_response: "
		      "need to run setup_gold_info before this");
		return NULL;
	} else if(!gold_request) {
		error("get_gold_response: No request given.");
		return NULL;
	}

	if(!timeout) 
		timeout = (slurm_get_msg_timeout() * 1000);
	
	if(gold_request->object >= GOLD_OBJECT_COUNT) {
		error("get_gold_response: "
		      "unsupported object %d", gold_request->object);
		return NULL;
	}
	object = GOLD_OBJECT_STR[gold_request->object];

	switch(gold_request->action) {
	case GOLD_ACTION_QUERY:
		action = GOLD_ACTION_QUERY_STR;
		itr = list_iterator_create(gold_request->selections);
		while((tmp_char = list_next(itr))) {
			xstrfmtcat(innerds, "<Get name=\"%s\"></Get>",
				   tmp_char);
		}
		list_iterator_destroy(itr);
		
		break;
	case GOLD_ACTION_CREATE:
		action = GOLD_ACTION_CREATE_STR;
		itr = list_iterator_create(gold_request->assignments);
		while((name_val = list_next(itr))) {
			xstrfmtcat(innerds, "<Set name=\"%s\">%s</Set>",
				   name_val->name, name_val->value);
		}
		list_iterator_destroy(itr);
		break;
	case GOLD_ACTION_MODIFY:
		action = GOLD_ACTION_MODIFY_STR;
		itr = list_iterator_create(gold_request->assignments);
		while((name_val = list_next(itr))) {
			xstrfmtcat(innerds, "<Set name=\"%s\">%s</Set>",
				   name_val->name, name_val->value);
		}
		list_iterator_destroy(itr);
		break;
	case GOLD_ACTION_DELETE:
		action = GOLD_ACTION_DELETE_STR;
		break;
	default:
		error("get_gold_response: "
		      "unsupported action %d", gold_request->action);
	}

	itr = list_iterator_create(gold_request->conditions);
	while((name_val = list_next(itr))) {
		xstrfmtcat(innerds, "<Where name=\"%s\"", name_val->name);

		if(name_val->op != GOLD_OPERATOR_NONE) {
			char *op = NULL;
			switch (name_val->op) {
			case GOLD_OPERATOR_G :
				op = "G";
				break;
			case GOLD_OPERATOR_GE :
				op = "GE";
				break;
			case GOLD_OPERATOR_L :
				op = "L";
				break;
			case GOLD_OPERATOR_LE :
				op = "LE";
				break;
			default:
				error("Unknown operator '%d' "
				      "given to this condition %s = %s",
				      name_val->op, name_val->name,
				      name_val->value);
				xfree(innerds);
				list_iterator_destroy(itr);
				return NULL;
			}
			
			xstrfmtcat(innerds, " op=\"%s\"", op);
		} 

		if(name_val->or_statement == 1) 
			xstrfmtcat(innerds, " conj=\"Or\" groups=\"-1\"");
		else if (name_val->or_statement == 2)
			xstrfmtcat(innerds, " conj=\"And\" groups=\"+1\"");

		xstrfmtcat(innerds, ">%s</Where>", name_val->value);
	}
	list_iterator_destroy(itr);

	user_name = uid_to_string(geteuid());
	xstrfmtcat(gold_request->body,
		   "<Body><Request action=\"%s\" actor=\"%s\">"
		   "<Object>%s</Object>",
		   action, user_name, object);
	xfree(user_name);
	if(innerds) {
		xstrcat(gold_request->body, innerds);
		xfree(innerds);
	}
	xstrcat(gold_request->body, "</Request></Body>");

	SHA1((unsigned char *)gold_request->body, strlen(gold_request->body),
	     digest);	
	gold_request->digest = encode_base64(digest, dlen);
	HMAC(EVP_sha1(), gold_key, strlen(gold_key),
	     digest, dlen, signature, &slen);
	gold_request->signature = encode_base64(signature, slen);

	xstrfmtcat(gold_msg,
		   "<?xml version='1.0' encoding='UTF-8'?><Envelope>%s"
		   "<Signature><DigestValue>%s</DigestValue>"
		   "<SignatureValue>%s</SignatureValue>"
		   "<SecurityToken type='Symmetric'></SecurityToken>"
		   "</Signature></Envelope>",
		   gold_request->body, gold_request->digest,
		   gold_request->signature);
	
	snprintf(tmp_buff, sizeof(tmp_buff), "%X\r\n",
		 (unsigned int)strlen(gold_msg));	

	/* I wish gold could do persistant connections but it only
	 * does one and then ends it so we have to do that also so
	 * every time we start a connection we have to finish it. As
	 * since we can only send one thing at a time we have to lock
	 * the connection.
	 */
//	slurm_mutex_lock(&gold_mutex);
	if(!(gold_fd = _start_communication())) {
		//slurm_mutex_unlock(&gold_mutex);
		return NULL;
	}
	rc = _slurm_send_timeout(gold_fd, tmp_buff, strlen(tmp_buff),
				 SLURM_PROTOCOL_NO_SEND_RECV_FLAGS,
				 timeout);
	
	if (rc < 0) {
		error("get_gold_response 1: _slurm_send_timeout: %m");
		goto error;
	}

	debug3("sending %d '%s'", rc, gold_msg);

	xstrcat(gold_msg, "0\r\n");
	rc = _slurm_send_timeout(gold_fd, gold_msg, strlen(gold_msg),
				 SLURM_PROTOCOL_NO_SEND_RECV_FLAGS,
				 timeout);
	if (rc < 0) {
		error("get_gold_response 2: _slurm_send_timeout: %m");
		goto error;
	}
	
	xfree(gold_msg);
	
	/* we will always get this header 
	 * HTTP/1.1 200 OK 17
	 * Content-Type: text/xml; charset="utf-8" 42
	 * Transfer-Encoding: chunked 28
	 *  
	 * which translates to 87 chars
	 */ 
	if(_slurm_recv_timeout(gold_fd, tmp_buff, 87, 0, timeout) < 0) {
		error("get_gold_response: "
		      "couldn't get the header of the message");
		goto error;
	}
	debug5("got the header '%s'", tmp_buff);
	
	/* then get the size which is ended with '\r\n'
	 */
	i = 0;
	while(read(gold_fd, &c, 1) > 0) {
		if(c == '\r') {
			read(gold_fd, &c, 1);
			break;
		}
		tmp_buff[i++] = c; //////// getting command string
		
	}
	tmp_buff[i] = '\0';
	ret_len = xstrntol(tmp_buff, NULL, i, 16);
		
	debug4("got size %d", ret_len);
	
	gold_msg = xmalloc(ret_len+1);
	
	if(_slurm_recv_timeout(gold_fd, gold_msg, ret_len, 0, timeout) < 0) {
		error("get_gold_response: "
		      "couldn't get the message");
		goto error;
	}

	debug3("got back '%s'", gold_msg);
	if(_slurm_recv_timeout(gold_fd, tmp_buff, 3, 0, timeout) < 0) {
		error("get_gold_response: "
		      "couldn't get the end of the message");
		goto error;
	}
	
	gold_response = xmalloc(sizeof(gold_response_t));
	gold_response->entries = list_create(destroy_gold_response_entry);
	i = 0;
	while(gold_msg[i]) {
		if(!strncmp(gold_msg+i, "<Code>", 6)) {
			i+=6;
			gold_response->rc = atoi(gold_msg+i);
		} else if(!strncmp(gold_msg+i, "<Count>", 7)) {
			i+=7;
			gold_response->entry_cnt = atoi(gold_msg+i);
		} else if(!strncmp(gold_msg+i, "<Message>", 9)) {
			int msg_end = 0;

			i+=9;
			msg_end = i;
			while(gold_msg[msg_end] != '<') 
				msg_end++;
			
			gold_response->message = 
				xstrndup(gold_msg+i, msg_end-i);
			i = msg_end + 10;
		} else if(!strncmp(gold_msg+i, object, strlen(object))) {
			gold_response_entry_t *resp_entry =
				_create_response_entry(object, gold_msg, &i);
			list_append(gold_response->entries, resp_entry);
		}
		i++;	
	}
	xfree(gold_msg);

error:
	/* I wish gold could do persistant connections but it only
	 * does one and then ends it so we have to do that also so
	 * every time we start a connection we have to finish it.
	 */
	_end_communication(gold_fd);
//	slurm_mutex_unlock(&gold_mutex);
		
	return gold_response;

}

extern int destroy_gold_response(gold_response_t *gold_response)
{
	if(gold_response) {
		xfree(gold_response->message);
		if(gold_response->entries) 
			list_destroy(gold_response->entries);
		
		xfree(gold_response);
	}
	return SLURM_SUCCESS;
}

extern void destroy_gold_name_value(void *object)
{
	gold_name_value_t *name_val = (gold_name_value_t *)object;

	if(name_val) {
		xfree(name_val->name);
		xfree(name_val->value);
		xfree(name_val);
	}
}

extern void destroy_gold_char(void *object)
{
	char *name_val = (char *)object;
	xfree(name_val);
}

extern void destroy_gold_response_entry(void *object)
{
	gold_response_entry_t *resp_entry = (gold_response_entry_t *)object;

	if(resp_entry) {
		list_destroy(resp_entry->name_val);
		xfree(resp_entry);
	}
}


#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#if defined(NEEDS_STDLIB_PROTOTYPES)
/* This is included in the MPI include files ONLY */
#include "../../../../include/protofix.h"
#endif

#include "alog.h"

int xx_alog_status = 0x3;	/* Logging turned ON (default) */
int xx_alog_setup_called = 0;	/* setup  not yet called */
int xx_alog_output_called = 0;	/* output not yet called */
char xx_alog_outdir[MAX_DIRNAME_LEN+1] = "";
struct head_trace_buf *xx_buf_head = NULL;


VOID xx_write(head,pid,event,data1,data2)
struct head_trace_buf *head;
int pid,event,data1;
char *data2;
{
	int xx_i, found;
	struct trace_buf *buf;

	if (xx_alog_status >> 1) {
		fprintf(stderr, "ALOG: Error: event %d logging requested by PID %d before doing ALOG setup\n", event, pid);
		return;
	}
	if (head == NULL) return;
	if (head->next_entry == head->max_size +99)
		return;
	found = 0;
	while(!found) {
	    if (head->next_entry >= head->max_size) {
		xx_i = 1;
		head->next_entry = 0;
		if (head->cbuf->next_buf == NULL)
			xx_i = xx_getbuf(head);
		else 
			head->cbuf = head->cbuf->next_buf;
		if ((!xx_i) && (head->trace_flag == ALOG_WRAP))
			head->cbuf = head->xx_list;
		else if (!xx_i) {
			head->next_entry = (head->max_size + 99);
			return;
		}
	    } /* endif */
	    buf = head->cbuf;
	    xx_i = head->next_entry;
	    head->next_entry++;
	    if (buf->ALOG_table[xx_i].event > (-1))
		found = 1;
	} /* end while */
	buf->ALOG_table[xx_i].id 	= pid;
	buf->ALOG_table[xx_i].task_id 	= 0;
	buf->ALOG_table[xx_i].event 	= event;
	buf->ALOG_table[xx_i].data_int	= data1;
	if (event == (-11)){
		buf->ALOG_table[xx_i].tstamp = atol(data2);
		buf->ALOG_table[xx_i].tind = 0;
		strcpy(buf->ALOG_table[xx_i].data_string," ");
		head->prev_time = 0;
	} else if ((event < 0) && (event > (-11))) {
		buf->ALOG_table[xx_i].tstamp = 0; 
		buf->ALOG_table[xx_i].tind = 0;
		head->prev_time = 0;
		strncpy(buf->ALOG_table[xx_i].data_string,data2,MAX_LOG_STRING_LEN);
		buf->ALOG_table[xx_i].data_string[MAX_LOG_STRING_LEN] = '\0';
	} else {
		buf->ALOG_table[xx_i].tstamp = (unsigned long) usc_clock(); 
		if (buf->ALOG_table[xx_i].tstamp < head->prev_time)
			head->ind_time++;
		buf->ALOG_table[xx_i].tind = head->ind_time;
		head->prev_time = buf->ALOG_table[xx_i].tstamp;
		strncpy(buf->ALOG_table[xx_i].data_string,data2,MAX_LOG_STRING_LEN);
		buf->ALOG_table[xx_i].data_string[MAX_LOG_STRING_LEN] = '\0';
	}
}


VOID xx_dump(head)
struct head_trace_buf *head;
{
	struct trace_buf *temp;
	FILE *fp;
	int cycle;
	register int xx_i;

	if (head == NULL) return;
	if (head->file_t == NULL) {
		fprintf(stderr,"ALOG: *** Trace file not written ***\n");
		return;
	}
	fp = head->file_t;
	temp = head->xx_list;
	for(xx_i = 0; xx_i< head->max_size; xx_i++)
		if ((temp->ALOG_table[xx_i].event < 0) && (temp->ALOG_table[xx_i].event >= (-11)))
			fprintf(fp,"%d %d %d %d %lu %lu %s\n",
				temp->ALOG_table[xx_i].event,
				temp->ALOG_table[xx_i].id,
				temp->ALOG_table[xx_i].task_id,
				temp->ALOG_table[xx_i].data_int,
				temp->ALOG_table[xx_i].tind,
				temp->ALOG_table[xx_i].tstamp,
				temp->ALOG_table[xx_i].data_string);

	if (head->trace_flag == ALOG_WRAP) {
		temp = head->cbuf;
		if (temp != NULL)
			xx_dump_aux(temp,fp,head->next_entry,head->max_size);
		cycle = 0;
		while((temp != NULL) && (!cycle)) {
			temp = temp->next_buf;
			if (temp == NULL)
				temp = head->xx_list;
			if (temp == head->cbuf)
				cycle = 1;
			else
				xx_dump_aux(temp,fp,0,head->max_size);
		};
		if (temp != NULL)
			xx_dump_aux(temp,fp,0,head->next_entry);
	} else {
		temp = head->xx_list;
		while(temp != NULL) {
			xx_dump_aux(temp,fp,0,head->max_size);
			temp = temp->next_buf;
		};
	};

	fclose(head->file_t);
}


VOID xx_dump_aux(buf,fp,xx_j,xx_k)
struct trace_buf *buf;
FILE *fp;
int xx_j, xx_k;
{
	register int xx_i;
	for(xx_i = xx_j; xx_i < xx_k; xx_i++)
		if ((buf->ALOG_table[xx_i].id != (-1)) && 
		    ((buf->ALOG_table[xx_i].event >= 0) || (buf->ALOG_table[xx_i].event < (-11)))) 
			fprintf(fp,"%d %d %d %d %lu %lu %s\n",
				buf->ALOG_table[xx_i].event,
				buf->ALOG_table[xx_i].id,
				buf->ALOG_table[xx_i].task_id,
				buf->ALOG_table[xx_i].data_int,
				buf->ALOG_table[xx_i].tind,
				buf->ALOG_table[xx_i].tstamp,
				buf->ALOG_table[xx_i].data_string);
}


int xx_getbuf(head)
struct head_trace_buf *head;
{
	register int i;
	struct trace_buf *temp;

	if (head == NULL) return(0);
	temp = (struct trace_buf *) malloc(sizeof(struct trace_buf));
	if (temp == NULL)
	{
		fprintf(stderr,"alog out of memory\n");
		return(0);
	}
	else {	
		head->cbuf = temp;
		for(i=0;i<MAX_BUF_SIZE;i++)
			temp->ALOG_table[i].id = (-1);
		temp->next_buf = NULL;
		if (head->xx_list == NULL)
			head->xx_list = temp;
		else {
			temp = head->xx_list;
			while(temp->next_buf != NULL)
				temp = temp->next_buf;
			temp->next_buf = head->cbuf;
		};
		return(1);
	};
}


VOID xx_user(head,id)
struct head_trace_buf *head;
int id;
{
	char c[MAX_LOG_STRING_LEN], cd[100];

	if (head == NULL) return;
	if (!strcmp(xx_alog_outdir, ""))
		getcwd(cd,100);
	else
		strcpy(cd, xx_alog_outdir);
	if (cd[strlen(cd)-1] != '/')
		strcat(cd, "/");
	strcat(cd,ALOG_LOGFILE);
	sprintf(c,"%d",id);
	strcat(cd,c);

	if ((head->file_t = fopen(cd,"w")) == NULL)
		fprintf(stderr,"ALOG: *** Trace file creation failure ***\n");
}


VOID xx_user1(head,id)
struct head_trace_buf *head;
int id;
{
	struct stat buf;
	char c[MAX_LOG_STRING_LEN], cd[100], x[100];

	if (head == NULL) return;
	if (!strcmp(xx_alog_outdir, ""))
		getcwd(cd,100);
	else
		strcpy(cd, xx_alog_outdir);
	if (cd[strlen(cd)-1] != '/')
		strcat(cd, "/");
	strcat(cd,ALOG_LOGFILE);
	sprintf(c,"%d",id);
	strcat(cd,c);

	stat(cd,&buf);
	strcpy(cd,(char *) ctime(&buf.st_ctime));

	strcpy(x,"AL");
	strncat(x,cd+4,3);	
	strcat(x,"-");
	strncat(x,cd+8,2);	
	strcat(x,"-");
	strncat(x,cd+22,2);	

	strcpy(c,"            ");
	strncpy(c,x,12);
	xx_write(head,0,(-1),0,c);

	sprintf(x,"%lu", (unsigned long) usc_rollover_val());
	xx_write(head,0,(-11),0,x);
}


VOID xx_alog_setup(pid,flag)
int pid, flag;
{
/*
	register int i;
	char c[MAX_LOG_STRING_LEN], cd[MAX_LOG_STRING_LEN];
 */
	usc_init(); 
	xx_alog_status &= 0x1;    /* set initialized flag */
	xx_buf_head = (struct head_trace_buf *)
        		malloc(sizeof(struct head_trace_buf));
	if (xx_buf_head == NULL) {
        	fprintf(stderr,"ALOG: *** Trace buffer HEADER creation failure;\n");
        	fprintf(stderr,"      *** tracing is being disabled.\n");
		return;
	}
	xx_buf_head->next_entry = 0;
	xx_buf_head->max_size = MAX_BUF_SIZE;
	xx_buf_head->prev_time = 0;
	xx_buf_head->ind_time = 0;
	xx_buf_head->trace_flag = flag;
	xx_buf_head->xx_list = NULL;
	if (!(xx_getbuf(xx_buf_head)))
        	printf("ALOG: **** trace buffer creation failure ****\n");
	xx_user(xx_buf_head,(pid));
}

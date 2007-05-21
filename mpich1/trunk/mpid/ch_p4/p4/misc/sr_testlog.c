#define ALOG_TRACE
#include "p4.h"
#include "sr_user.h"

#define MAX_MESSAGE_SIZE 1500000
char msg[MAX_MESSAGE_SIZE];

#define SENDING 99

int main(argc,argv)
int argc;
char **argv;
{

    p4_initenv(&argc,argv);

    ALOG_ENABLE;
    ALOG_MASTER(p4_get_my_id(),ALOG_TRUNCATE);
    ALOG_DEFINE(SENDING,"Sending","");

    if (p4_get_my_id() == 0)
    {
        p4_create_procgroup();
	master();
    }
    else
    {
        slave();
    }
  
    ALOG_OUTPUT;
    p4_wait_for_end();
}

    
master()
{
    int nslaves;
    int type, size, id, from;
    int my_id;
    char *incoming;
    int done;
    int msgsize, count;
    int starttime, endtime;
    p4_usc_time_t start_ustime, end_ustime;

    nslaves = p4_num_total_slaves();
    printf("number of slaves = %d\n",nslaves);
    my_id = p4_get_my_id();
    
    done = FALSE;
    while (!done)
    {
	printf("message size: ");
	scanf("%d",&msgsize);
	if (msgsize > MAX_MESSAGE_SIZE)
	{
	    printf("too big;  using %d\n",MAX_MESSAGE_SIZE);
	    msgsize = MAX_MESSAGE_SIZE;
	}
	printf("times around loop (or 0 for end): ");
	scanf("%d",&count);
	
	if (count == 0)
	    done = TRUE;
	else
	{
	    starttime = p4_clock();
	    start_ustime = p4_ustimer();
	    while (count > 0)
	    {
                ALOG_LOG(p4_get_my_id(),SENDING,DATA,"");
		p4_sendr(DATA, 1, msg, msgsize);
		type = -1;
		from = -1;
		incoming = NULL;
		p4_recv(&type, &from, &incoming, &size);
		p4_msg_free(incoming);
		count--;
	    }
	    end_ustime = p4_ustimer();
	    endtime = p4_clock();
	    printf("time %d milliseconds\n",endtime-starttime);
	    printf("time %d microseconds\n",end_ustime-start_ustime);
	}
    }

    ALOG_LOG(p4_get_my_id(),SENDING,END,"");
    p4_sendr(END, 1, msg, 0);
    type = -1;
    from = -1;
    incoming = NULL;
    p4_recv(&type, &from, &incoming, &size);
    p4_msg_free(incoming);
    p4_wait_for_end();
    printf("master exiting normally\n");
}

slave()	
{
    int nslaves;
    int done;
    int type, from, size;
    int next;
    int my_id;
    char *incoming;
    
    ALOG_SETUP(p4_get_my_id(),ALOG_TRUNCATE);
    my_id = p4_get_my_id();
    nslaves = p4_num_total_slaves();
    
    if (my_id == nslaves)
        next = 0;
    else
	next = my_id + 1;
    
    done = FALSE;
    while (!done)
    {
	type = -1;
	from = -1;
	incoming = NULL;
	p4_recv(&type,&from, &incoming, &size);
	if (type == END)
	    done = TRUE;
	ALOG_LOG(p4_get_my_id(),SENDING,type,"");
	p4_sendr(type, next, incoming, size);
	p4_msg_free(incoming);
    }
    ALOG_OUTPUT;
}


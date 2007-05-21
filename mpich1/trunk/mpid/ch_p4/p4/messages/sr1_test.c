#include "p4.h"
#include "sr_user.h"

#define MAX_MESSAGE_SIZE 1500000
char msg[MAX_MESSAGE_SIZE];

int main(argc,argv)
int argc;
char **argv;
{

    p4_initenv(&argc,argv);
    p4_create_procgroup();
    if (p4_get_my_id() == 0)
    {
	master();
    }
    else
    {
        slave();
    }
  
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

    p4_sendr(END, 1, msg, 0);
    type = -1;
    from = -1;
    incoming = NULL;
    p4_recv(&type, &from, &incoming, &size);
    p4_msg_free(incoming);
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
	p4_sendr(type, next, incoming, size);
	p4_msg_free(incoming);
    }
}


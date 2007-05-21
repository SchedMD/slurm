#include "p4.h"
#include "sr_user.h"
    
#define MAX_VECLEN 10000

int msg[MAX_VECLEN];

main(argc,argv)
int argc;
char **argv;
{
    int nslaves;
    int type, size, id, from;
    int my_id;
    char *incoming;
    int done;
    int i, veclen, count;
    int starttime, endtime;
    p4_usc_time_t start_ustime, end_ustime, rollover;

    p4_initenv(&argc,argv);
    p4_create_procgroup();
    
    nslaves = p4_num_total_ids() - 1;
    printf("number of slaves = %d\n",nslaves);
    my_id = p4_get_my_id();
    rollover = p4_usrollover();
    printf("rollover=%d\n",rollover);
    
    done = FALSE;
    while (!done)
    {
	printf("vector length: ");
	scanf("%d",&veclen);
	if (veclen > MAX_VECLEN)
	{
	    printf("too big;  using %d\n",MAX_VECLEN);
	    veclen = MAX_VECLEN;
	}
	for (i=0; i<veclen; i++)
	    msg[i] = i;

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
		p4_sendx(DATA, 1, msg, veclen*sizeof(int), P4INT);
		type = -1;
		from = -1;
		incoming = NULL;
		p4_recv(&type, &from, &incoming, &size);
		compare_vec(msg,incoming,veclen);
		p4_msg_free(incoming);
		count--;
	    }
	    end_ustime = p4_ustimer();
	    endtime = p4_clock();
	    printf("time %d milliseconds\n",endtime-starttime);
	    printf("time %d microseconds\n",end_ustime-start_ustime);
	}
    }

    p4_send(END, 1, msg, 0);
    type = -1;
    from = -1;
    incoming = NULL;
    p4_recv(&type, &from, &incoming, &size);
    p4_msg_free(incoming);
    p4_wait_for_end();
    printf("master exiting normally\n");
}

compare_vec(a,b,len)
int a[], b[];
int len;
{
    int i;

    for (i=0; i < len; i++)
    {
	if (a[i] != b[i])
	    printf("a[%d] = %d, b[%d] = %d\n",i,a[i],i,b[i]);
    }
}
	    

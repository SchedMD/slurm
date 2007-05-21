#include "p4.h"
#include "lm.h"
    
slave()	
{
int myid;
char buf[100];
int size;
int i,j,n;
int nslaves;
int start, end;
char *msg;
char loc_msg[200];
int type;
int done;
int from;
int nmsgs;
int value;
int msg_cnt;
int msgs_rcvd[P4_MAXPROCS];  /* cheated and used a p4 internal def */

    myid = p4_get_my_id();
    nslaves = p4_num_total_ids() - 1;
    p4_get_cluster_ids(&start, &end);
    /***
    p4_dprintf("Inside slave process %d: nslaves=%d start=%d end=%d\n",
	     myid, nslaves, start, end);
    ***/

    for (i=0; i < P4_MAXPROCS; i++)
	msgs_rcvd[i] = 0;
    msg_cnt = 0;

    p4_dprintfl(9,"receiving...\n");
    type = CNTL;
    from = 0;
    msg = NULL;
    p4_recv(&type, &from, &msg, &size);
    p4_dprintfl(9,"rcvd from=%d type=%d msg=%s\n",from,type,msg);
    p4_msg_free(msg);
    msgs_rcvd[from]++;
    nmsgs = atoi(msg);

    type = DATA;
    for (j=1; j <= nmsgs; j++)
    {
	for (i=1; i <= nslaves; i++)
	{
	    if (i != myid)
	    {
		sprintf(loc_msg,"%d",j);
		p4_dprintfl(9,"sending %d to %d\n",j,i);
		p4_send(DATA, i, loc_msg, sizeof(loc_msg));
	    }
	}
    }

    done = nmsgs * (nslaves - 1);
    while (msg_cnt < done)
    {
	/**  p4_dprintfl(0,"receiving \n");  **/
	type = DATA;
	from = -1;
	msg = NULL;
	p4_recv(&type, &from, &msg, &size);
	value = atoi(msg);
	msg_cnt++;
	p4_msg_free(msg);
	msgs_rcvd[from]++;
	p4_dprintfl(9,"rcvd from=%d type=%d value=%d\n",from,type,value);
    }
    p4_send(DATA, 0, loc_msg, sizeof(loc_msg));
    p4_dprintf("rcvd from: %d %d %d %d %d %d %d %d \n",
	    msgs_rcvd[0], msgs_rcvd[1], msgs_rcvd[2], msgs_rcvd[3],
	    msgs_rcvd[4], msgs_rcvd[5], msgs_rcvd[6], msgs_rcvd[7]);
	
    p4_dprintfl(0,"%d exiting\n", p4_get_my_id());
}

#include "p4.h"
#include "lm.h"

main(argc,argv)
int argc;
char **argv;
{
char buf[100];
int i, n;
int size;
int slv = 3;
int nmsgs;
int nslaves;
int start, end;
int type;
char msg[200];
char *rcvd_msg;
int from,rcvd_msg_len;

    p4_initenv(&argc,argv);
    p4_create_procgroup();

    p4_dprintf("entering master user code\n");

    nslaves = p4_num_total_ids() - 1;
    p4_get_cluster_ids(&start, &end);

    p4_dprintfl(9,"got nslaves=%d start=%d end=%d\n",nslaves,start,end);

    printf("enter a number of messages:\n");
    scanf("%d",&nmsgs);
	
    type = CNTL;
    sprintf(msg,"%d",nmsgs);
    for (i=1; i <= nslaves; i++)
    {
	p4_dprintfl(9,"sending msg %s to %d size=%d\n",msg,i,sizeof(msg));
	p4_send(type, i, msg, sizeof(msg));
    }
    for (i=1; i <= nslaves; i++)
    {
        type = -1;
        from = -1;
	rcvd_msg = NULL;
	p4_recv(&type, &from, &rcvd_msg, &rcvd_msg_len);
	p4_dprintfl(9,"recvd msg from %d\n",from);
    }

    p4_dprintfl(9,"master entering waitforend\n");
    p4_wait_for_end();
    p4_dprintfl(9,"master past waitforend\n");
    p4_dprintf("exiting master user code\n");
}

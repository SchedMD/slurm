/*  this file contains the direct fiber channel interface:
    p4_initfc
    p4_sendfc
    p4_recvfc
    */

#include "p4.h"
#include "p4_sys.h"

#include <sys/uio.h>

#include <sys/file.h>
#include <sys/comio.h>
/* #include <fcntl.h> */
#include <sys/devinfo.h>
#include <stdlib.h>
/* #include <netdb.h> */
#include "sfcdchi.h"

int y, fds[64];
struct sfc_open *fcopen;
struct sfc_bind *fcbind;
struct sfc_connect *fcconnect;
struct sfc_listen *fclisten;
struct sfc_accept *fcaccept;
struct sfc_read *fcread;
struct sfc_write *fcwrite;
struct sfc_close *fcclose;

p4_initfc()
{
    unsigned char *buf;
    int fd;
    int i;
    int j;
    int k;
    char * device;
    char * localip = NULL;
    char  destip[100];
    struct hostent * hp;
    int remotefd;
    int myid, numnodes, rc;


    device = "/dev/sfc0";

    fcopen = calloc(1,sizeof(struct sfc_open));
    fcbind = calloc(1,sizeof(struct sfc_bind));
    fcbind->name = calloc(1,sizeof(struct sfcaddr_in));
    fcconnect = calloc(1,sizeof(struct sfc_connect));
    fcconnect->name = calloc(1,sizeof(struct sfcaddr_in));
    fcaccept = calloc(1,sizeof(struct sfc_accept));
    fcaccept->address = calloc(1,sizeof(struct sfcaddr_in));
    fclisten = calloc(1,sizeof(struct sfc_listen));
    fcread = calloc(1,sizeof(struct sfc_read));
    fcwrite = calloc(1,sizeof(struct sfc_write));
    fcclose= calloc(1,sizeof(struct sfc_close));

    if ((y=open(device,O_RDWR,0)) < 0) 
    {
	fprintf(stderr, "p4_initfc: Unable to open device '%s': %s\n",
		device, strerror(errno));
	exit(1);
    }

    p4_dprintfl(30,"opening. . .\n");
    if ((i = ioctl(y,SFC_OPEN,fcopen)) != 0)
    {
	p4_error("p4_initfc: open error: ",errno); 
    }
    fd = fcopen->dcd;
    p4_dprintfl(30,"opened fd %d. . .\n",fd);

    p4_dprintfl(30,"binding port 211 to descriptor fd %d. . .\n",fd);
    fcbind->dcd=fd;
    fcbind->name->port = 211;
    fcbind->name->type = IP_ADDR;
    if (localip == NULL)
	fcbind->name->inet_addr = gethostid();
    else 
    {
	hp = gethostbyname(localip);
	if (hp == NULL)
	{
	    fprintf(stderr, "p4_initfc: %s: unknown host\n", localip);
	    exit(1);
	}
	bcopy(hp->h_addr,&fcconnect->name->inet_addr,hp->h_length);
    }
    if ((i = ioctl(y,SFC_BIND,fcbind)) != 0)
    {
	p4_error("p4_initfc: bind error: ",errno); 
    }
    p4_dprintfl(30,"listening for connection requests. . .\n");
    fclisten->dcd = fd;
    fclisten->backlog = 3;
    if ((i = ioctl(y,SFC_LISTEN,fclisten)) != 0)
    {
	p4_error("p4_initfc: listen error: ",errno); 
    }

    /* establish all the connections */
    numnodes = p4_num_total_ids();
    myid     = p4_get_my_id();

    for (i = 0; i < numnodes; i++)
    {
	if (myid < i)
	{
	    int otherid;

	    p4_dprintfl(30,"accepting first connection. . .\n");
	    fcaccept->dcd = fd;
	    fcaccept->timeout = DCMAXTIMEOUT;
	    fcaccept->blockflag = BLOCKING;
	    do {
		errno = 0;
		if ((rc = ioctl(y,SFC_ACCEPT,fcaccept)) != 0
		    && errno != ETIMEOUT)
		{
		    p4_error("p4_initfc: accept error: ",errno); 
		}
	    } while (errno == ETIMEOUT);

	    if (rc==NOERROR)
		remotefd = fcaccept->newdcd;

	    p4_dprintfl(30,"doing initial blocked read\n");
	    fcread->dcd = fcaccept->newdcd;
	    fcread->buff = (char *) &otherid;
	    fcread->nbytes = sizeof(int);
	    fcread->timeout = 10;   /* 10 sec */
	    fcread->blockflag = BLOCKING;
	    rc = ioctl(y,SFC_READ,fcread);

	    if (rc != 0)
		p4_dprintf("err %d\n",errno);
	    else
	    {
		if (fcread->nbytes != sizeof(int))
		    p4_error("initial message wrong size:", fcread->nbytes);
		else
		{
		    p4_dprintfl(30,"read from %d\n",otherid);
		    fds[otherid] = remotefd;
		}
	    }
	}
	if (myid > i)
	{
	    p4_dprintfl(30,"opening. . .\n");
	    if ((rc = ioctl(y,SFC_OPEN,fcopen)) != 0)
	    {
		p4_error("p4_initfc: open error: ",errno); 
	    }
	    fd = fcopen->dcd;
	    p4_dprintfl(30,"opened fd %d. . .\n",fd);
	    translate_name(p4_global->proctable[i].host_name,destip);
	    p4_dprintfl(30,"swname = %s, destip = %s\n",
		       p4_global->proctable[i].host_name,destip);
	    hp = gethostbyname(destip);
	    if (hp == 0)
	    {
		fprintf(stderr, "p4_initfc: %s: unknown host\n",destip);
		exit(2);
	    }
	    p4_dprintfl(30,"connecting. . .\n");

	    do /*try to connect repeatedly until accept on other side occurs */
	    {
		fcconnect->dcd = fd;
		fcconnect->timeout = 10;
		fcconnect->name->port = 211;
		fcconnect->name->type = IP_ADDR;
		bcopy(hp->h_addr, &fcconnect->name->inet_addr, hp->h_length);
		if ((rc = ioctl(y,SFC_CONNECT,fcconnect)) != NOERROR)
		{
		    p4_dprintf("error connecting %d\n", errno);
		}
	    } while (rc != NOERROR);

	    fds[i] = fd;
	    p4_dprintfl(30,"connected. . .\n");

	    p4_dprintfl(30,"%d writing id\n",myid);
	    fcwrite->dcd    = fd;
	    fcwrite->nbytes = sizeof(int);
	    fcwrite->buff   = (char *) &myid;
	    fcwrite->type   = BLOCKING;

	    if ((rc = ioctl(y,SFC_WRITE,fcwrite)) != NOERROR)
	    {
		if (rc < 0 )
		    p4_dprintf("error on initial write was %d \n ",errno);
	    }
	}
    }
}

translate_name(fromname,toname)
char *fromname, *toname;
{
/*  this routine is a temporary hack to derive fiber-channel names from
    the hostnames in the procgroup file that are used to set up the original
    p4 configuration for use by p4_send and p4_recv.  The fiber channel names
    will be used by the direct fiber-channel interface, p4_sendfc and 
    p4_recvfc.
*/

    if (strncmp(fromname,"spnode",6) == 0) /* ANL SP-1 nodes */
    {
	/* chg swnodexxx to fcnodexxx */
	strcpy(toname,fromname);
	toname[0] = 'f';
	toname[1] = 'c';
    }
    else if ((strncmp(fromname,"hamlet",6) == 0) ||
	     (strncmp(fromname,"timon",5) == 0)  ||
	     (strncmp(fromname,"titus",5) == 0)  ||
	     (strncmp(fromname,"ibm1",4) == 0)) /* ANL CTD and HEP */
    {
	/* append "-fc to machine names */
	strcpy(toname,fromname);
	strcat(toname,"-fc");
    }
    else if ((strncmp(fromname,"mercury",7) == 0) ||
	     (strncmp(fromname,"venus",5) == 0)   ||
	     (strncmp(fromname,"earth",5) == 0)   ||
	     (strncmp(fromname,"mars",4) == 0)    ||
	     (strncmp(fromname,"jupiter",7) == 0) ||
	     (strncmp(fromname,"saturn",6) == 0)  ||
	     (strncmp(fromname,"neptune",7) == 0) ||
	     (strncmp(fromname,"uranus",7) == 0) ||
	     (strncmp(fromname,"pluto",5) == 0)) /* ANL SP-1 planets */

    {
	/* append "f1" to planet names */
	strcpy(toname,fromname);
	strcat(toname,"f1");
    }
    else if (strncmp(fromname,"ibms",4) == 0) /* FSU nodes */
    {
	/* append "f" to FSU ibmsxx names */
	strcpy(toname,fromname);
	strcat(toname,"f");
    }
    else
	p4_error("p4_initfc:  couldn't translate name", 0);
}

p4_sendfc(type,to,msg,len)
int type,to,len;
char *msg;
{
    int rc, wrtlen;
    struct p4_net_msg_hdr header;
    
    header.msg_type = type;
    header.to	    = to;
    header.from     = p4_get_my_id();
    header.ack_req  = 0;
    header.msg_len  = len;

    p4_dprintfl(30,"writing header to %d\n",to);
    fcwrite->dcd    = fds[to];
    fcwrite->nbytes = sizeof(struct p4_net_msg_hdr);
    fcwrite->buff   = (char *) &header;
    fcwrite->type   = BLOCKING;

    if ((rc = ioctl(y,SFC_WRITE,fcwrite)) != NOERROR)
    {
	if (rc < 0 )
	    p4_error("p4_sendfc error on header write was ",errno);
    }

    while (len > 0)
    {
	if (len > 65500)
	{
	    len -= 65500;
	    wrtlen = 65500;
	}
	else
	{
	    wrtlen = len;
	    len = 0;
	}
	p4_dprintfl(30,"writing %d bytes of data to %d\n",to);
	fcwrite->dcd    = fds[to];
	fcwrite->nbytes = wrtlen;
	fcwrite->buff   = msg;
	fcwrite->type   = BLOCKING;

	if ((rc = ioctl(y,SFC_WRITE,fcwrite)) != NOERROR)
	{
	    if (rc < 0 )
		p4_error("error on data write was ",errno);
	}
    }
    p4_dprintfl(30,"exiting p4_sendfc\n");
}

p4_recvfc(type,from,msg,len)
int *type, *from, *len;
char **msg;
{
    int rc;
    struct p4_net_msg_hdr header;
    
    if (*msg == NULL)
	p4_error("p4_recvfc: pre-allocated buffers required for fiber channel",0);

    if (*from == -1)
	p4_error("p4_recvfc: wild-card receive not allowed for fiber channel",0);
    
    p4_dprintfl(30,"doing blocked read for header\n");
    fcread->dcd = fds[*from];
    fcread->buff = (char *) &header;
    fcread->nbytes = sizeof(struct p4_net_msg_hdr);
    fcread->timeout = 10;   /* 10 sec */
    fcread->blockflag = BLOCKING;
    rc = ioctl(y,SFC_READ,fcread);

    if (rc != 0)
	p4_error("p4_recvfc: read for header failed",rc);

    p4_dprintfl(30,"length from received header is %d\n",header.msg_len);

    if (header.msg_len != 0)
    {
	p4_dprintfl(30,"doing blocked read for data\n");
	fcread->dcd = fds[*from];
	fcread->buff = *msg;
	fcread->nbytes = header.msg_len;
	fcread->timeout = 10;   /* 10 sec */
	fcread->blockflag = BLOCKING;
	rc = ioctl(y,SFC_READ,fcread);

	if (rc != 0)
	    p4_error("p4_recvfc: read for data failed",rc);

    }
    *type = header.msg_type;
    *from = header.from;
    *len  = header.msg_len;
}


	    

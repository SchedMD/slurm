#include "p4.h"
#include "p4_sys.h"

#define MAXVAL(a,b) (((a)>(b)) ? (a) : (b))
#define MINVAL(a,b) (((a)<(b)) ? (a) : (b))
#define ABSVAL(a)   (((a)>=0 ) ? (a) : -(a))

static P4VOID init_p4_brdcst_info (void);

int p4_broadcastx(type, data, data_len, data_type)
int type;
P4VOID *data;
int data_len, data_type;
/*
  Broadcast my data to all other processes.
  Other processes call p4_recv() in the normal fashion, specifying
  the node (if desired) that originated the broadcast.
*/
{
    int status = 0;

#ifdef P4_WITH_MPD
    if (1) return(0);		/* mpd debugging */
#endif

#if defined(NCUBE)
    int req_type, req_from;
    struct p4_msg *tmsg;

    status = send_message(type, p4_get_my_id(), 0xffff, data, data_len,
		          data_type, P4_FALSE, P4_FALSE);
    req_type = type;
    req_from = p4_get_my_id();
    tmsg = recv_message(req_type,req_from); /* ncube broadcast comes back */
    tmsg->msg_id = (-1);
    free_p4_msg(tmsg);		/* throw it away */
    return(status);
#endif

    init_p4_brdcst_info();

    /* Build the message with broadcast bit set */

    /* send to my subtree */
    status = subtree_broadcast_p4(type, p4_get_my_id(), data, data_len, data_type);
    if (p4_get_my_id() != 0)
    {
	/* send to node 0 for rest of tree */
	status = send_message(type, p4_get_my_id(), 0, data, data_len,
			      data_type, P4_BROADCAST_MASK, P4_FALSE);
    }
    if (status && !(SOFTERR))
	p4_error("p4_broadcast failed, type=", type);

    return status;
}

int subtree_broadcast_p4(type, from, data, data_len, data_type)
P4VOID *data;
int type, from, data_len, data_type;
/*
  Broadcast message to processes in my subtree.

  1) Send to left/right remote cluster masters
  2) Send to left/right local  cluster slaves
*/
{
    int status = 0;
    int nodes[4], i;

    init_p4_brdcst_info();

    p4_dprintfl(90, "subtree_broadcast_p4: type=%d, len=%d\n", type, data_len);

    nodes[0] = p4_brdcst_info.left_cluster;
    nodes[1] = p4_brdcst_info.right_cluster;
    nodes[2] = p4_brdcst_info.left_slave;
    nodes[3] = p4_brdcst_info.right_slave;

    for (i = 0; i < 4; i++)
    {
	if (nodes[i] > 0  &&  nodes[i] != from)
	{
	    if (send_message(type, from, nodes[i], data, data_len,
			     data_type, P4_BROADCAST_MASK, P4_FALSE))
	    {
		status = -1;
		break;
	    }
	}
    }

    if (status && !SOFTERR)
	p4_error("subtree_broadcast_p4 failed, type=", type);

    p4_dprintfl(90, "subtree_broadcast_p4: exit status=%d\n", status);
    return status;
}

static P4VOID init_p4_brdcst_info (void)
/*
  Construct tree connections for cluster-master and slave
  processes and insert into global structure
*/
{
#define MAX_MASTERS P4_MAXPROCS
    int me, my_master, node, n_master, indx = (-1);
    int master_list[MAX_MASTERS], previous_id;

    if (p4_brdcst_info.initialized)	/* Only need to do this once */
	return;

    p4_brdcst_info.initialized = 1;
    p4_brdcst_info.up = -1;	/* -1 means no one there */
    p4_brdcst_info.left_cluster = -1;
    p4_brdcst_info.right_cluster = -1;
    p4_brdcst_info.left_slave = -1;
    p4_brdcst_info.right_slave = -1;

    me = p4_get_my_id();

    /* Make list of cluster masters and find my master */
    /* Ideally should probably use p4_get_cluster_masters here */

    n_master = 0;
    previous_id = -1;
    for (node = 0; node < p4_num_total_ids(); node++)
    {
	if (p4_global->proctable[node].group_id != previous_id)
	{
	    master_list[n_master++] = node;
	    previous_id = p4_global->proctable[node].group_id;
	}
	if (node == me)
	    indx = n_master - 1;
    }
    if (indx < 0)
	p4_error("init_p4_brdcst_info: my master indx bad", indx);
    my_master = master_list[indx];

    /*
    printf("me=%d, indx=%d, n_master=%d, my_master=%d, myclusterid=%d, list=[",
	   me, indx, n_master, my_master, p4_get_my_cluster_id()); 
    for (node=0; node<n_master; node++) 
	printf(" %d",master_list[node]); 
    printf(" ]\n");
    */

    if (me == my_master)
    {
	/* If cluster master assign cluster master tree */

	if ((2 * indx + 1) < n_master)
	    p4_brdcst_info.left_cluster = master_list[2 * indx + 1];

	if ((2 * indx + 2) < n_master)
	    p4_brdcst_info.right_cluster = master_list[2 * indx + 2];

	if (me)
	    p4_brdcst_info.up = master_list[(indx - 1) / 2];
    }

    /* Now assign connections with own subtree */
    p4_dprintfl(90, "brdcst_info: numclusids=%d\n", p4_num_cluster_ids());
    node = 2 * p4_get_my_cluster_id() + 1;
    if (node < p4_num_cluster_ids())
	p4_brdcst_info.left_slave = node + my_master;

    node = 2 * p4_get_my_cluster_id() + 2;
    if (node < p4_num_cluster_ids())
	p4_brdcst_info.right_slave = node + my_master;

    if (me != my_master)
	p4_brdcst_info.up = my_master + (p4_get_my_cluster_id() - 1) / 2;

    p4_dprintfl(90, "brdcst_info: me=%d up=%d clusters(%d, %d) slaves(%d,%d)\n",
		me,
		p4_brdcst_info.up,
		p4_brdcst_info.left_cluster,
		p4_brdcst_info.right_cluster,
		p4_brdcst_info.left_slave,
		p4_brdcst_info.right_slave);

    /* Sanity check */

    if (p4_brdcst_info.up != -1)
	if (CHECKNODE(p4_brdcst_info.up))
	    p4_error("init_p4_brdcst_info: up node is invalid", p4_brdcst_info.up);
    if (p4_brdcst_info.left_cluster != -1)
	if (CHECKNODE(p4_brdcst_info.left_cluster))
	    p4_error("init_p4_brdcst_info: left_cluster node is invalid",
		     p4_brdcst_info.left_cluster);
    if (p4_brdcst_info.right_cluster != -1)
	if (CHECKNODE(p4_brdcst_info.right_cluster))
	    p4_error("init_p4_brdcst_info: right_cluster node is invalid",
		     p4_brdcst_info.right_cluster);
    if (p4_brdcst_info.left_slave != -1)
	if (CHECKNODE(p4_brdcst_info.left_slave))
	    p4_error("init_p4_brdcst_info: left_slave node is invalid",
		     p4_brdcst_info.left_slave);
    if (p4_brdcst_info.right_slave != -1)
	if (CHECKNODE(p4_brdcst_info.right_slave))
	    p4_error("init_p4_brdcst_info: right_slave node is invalid",
		     p4_brdcst_info.right_slave);
}


int p4_global_op(type, x, nelem, size, op, data_type)
int type;
P4VOID *x;
int nelem;
int size;
int data_type;
P4VOID(*op) (char *, char *, int);
/* see userman for more details */
{
    int me = p4_get_my_id();
    int status = 0;
    int zero = 0;
    int msg_len;
    char *msg;

#ifdef P4_WITH_MPD
    p4_dprintfl( 50, "entering AND LEAVING p4_global_op \n");
    if (1)  return(0);  /* mpd debugging */
#endif

    init_p4_brdcst_info();

    /* Accumulate up broadcast tree ... mess is due to return of soft errors */

    if (!status)
	if (p4_brdcst_info.left_slave > 0)
	{
	    msg = NULL;
	    status = p4_recv(&type, &p4_brdcst_info.left_slave, &msg, &msg_len);
	    if (!status)
	    {
		op(x, msg, msg_len / size);
		p4_msg_free(msg);
	    }
	}
    if (!status)
	if (p4_brdcst_info.right_slave > 0)
	{
	    msg = NULL;
	    status = p4_recv(&type, &p4_brdcst_info.right_slave, &msg, &msg_len);
	    if (!status)
	    {
		op(x, msg, msg_len / size);
		p4_msg_free(msg);
	    }
	}
    if (!status)
	if (p4_brdcst_info.left_cluster > 0)
	{
	    msg = NULL;
	    status = p4_recv(&type, &p4_brdcst_info.left_cluster, &msg, &msg_len);
	    if (!status)
	    {
		op(x, msg, msg_len / size);
		p4_msg_free(msg);
	    }
	}
    if (!status)
	if (p4_brdcst_info.right_cluster > 0)
	{
	    msg = NULL;
	    status = p4_recv(&type, &p4_brdcst_info.right_cluster, &msg, &msg_len);
	    if (!status)
	    {
		op(x, msg, msg_len / size);
		p4_msg_free(msg);
	    }
	}

    if ((!status) && p4_get_my_id())
	status = p4_sendx(type, p4_brdcst_info.up, x, nelem * size, data_type);

    /* Broadcast the result back */

    if (!status)
    {
	if (me == 0)
	    status = p4_broadcastx(type, x, nelem * size, data_type);
	else
	{
	    msg = NULL;
	    status = p4_recv(&type, &zero, &msg, &msg_len);
	    if (!status)
	    {
		bcopy(msg, (char *) x, msg_len);
		p4_msg_free(msg);
	    }
	}
    }

    if (status && !SOFTERR)
	p4_error("p4_global_op failed, type=", type);

    return status;
}

/* Standard operations on doubles */

P4VOID p4_dbl_sum_op(x, y, nelem)
char *x, *y;
int nelem;
{
    double *a = (double *) x;
    double *b = (double *) y;

    while (nelem--)
	*a++ += *b++;
}

P4VOID p4_dbl_mult_op(x, y, nelem)
char *x, *y;
int nelem;
{
    double *a = (double *) x;
    double *b = (double *) y;

    while (nelem--)
	*a++ *= *b++;
}

P4VOID p4_dbl_max_op(x, y, nelem)
char *x, *y;
int nelem;
{
    double *a = (double *) x;
    double *b = (double *) y;

    while (nelem--)
    {
	*a = MAXVAL(*a, *b);
	a++;
	b++;
    }
}

P4VOID p4_dbl_min_op(x, y, nelem)
char *x, *y;
int nelem;
{
    double *a = (double *) x;
    double *b = (double *) y;

    while (nelem--)
    {
	*a = MINVAL(*a, *b);
	a++;
	b++;
    }
}

P4VOID p4_dbl_absmax_op(x, y, nelem)
char *x, *y;
int nelem;
{
    double *a = (double *) x;
    double *b = (double *) y;

    while (nelem--)
    {
	*a = MAXVAL(ABSVAL(*a), ABSVAL(*b));
	a++;
	b++;
    }
}

P4VOID p4_dbl_absmin_op(x, y, nelem)
char *x, *y;
int nelem;
{
    double *a = (double *) x;
    double *b = (double *) y;

    while (nelem--)
    {
	*a = MINVAL(ABSVAL(*a), ABSVAL(*b));
	a++;
	b++;
    }
}

/* Standard operations on floats */

P4VOID p4_flt_sum_op(x, y, nelem)
char *x, *y;
int nelem;
{
    float *a = (float *) x;
    float *b = (float *) y;

    while (nelem--)
	*a++ += *b++;
}

P4VOID p4_flt_mult_op(x, y, nelem)
char *x, *y;
int nelem;
{
    float *a = (float *) x;
    float *b = (float *) y;

    while (nelem--)
	*a++ *= *b++;
}

P4VOID p4_flt_max_op(x, y, nelem)
char *x, *y;
int nelem;
{
    float *a = (float *) x;
    float *b = (float *) y;

    while (nelem--)
    {
	*a = MAXVAL(*a, *b);
	a++;
	b++;
    }
}

P4VOID p4_flt_min_op(x, y, nelem)
char *x, *y;
int nelem;
{
    float *a = (float *) x;
    float *b = (float *) y;

    while (nelem--)
    {
	*a = MINVAL(*a, *b);
	a++;
	b++;
    }
}

P4VOID p4_flt_absmax_op(x, y, nelem)
char *x, *y;
int nelem;
{
    float *a = (float *) x;
    float *b = (float *) y;

    while (nelem--)
    {
	*a = MAXVAL(ABSVAL(*a), ABSVAL(*b));
	a++;
	b++;
    }
}

P4VOID p4_flt_absmin_op(x, y, nelem)
char *x, *y;
int nelem;
{
    float *a = (float *) x;
    float *b = (float *) y;

    while (nelem--)
    {
	*a = MINVAL(ABSVAL(*a), ABSVAL(*b));
	a++;
	b++;
    }
}


/* Standard operations on integers */

P4VOID p4_int_sum_op(x, y, nelem)
char *x, *y;
int nelem;
{
    int *a = (int *) x;
    int *b = (int *) y;

    while (nelem--)
	*a++ += *b++;
}

P4VOID p4_int_mult_op(x, y, nelem)
char *x, *y;
int nelem;
{
    int *a = (int *) x;
    int *b = (int *) y;

    while (nelem--)
	*a++ *= *b++;
}

P4VOID p4_int_max_op(x, y, nelem)
char *x, *y;
int nelem;
{
    int *a = (int *) x;
    int *b = (int *) y;

    while (nelem--)
    {
	*a = MAXVAL(*a, *b);
	a++;
	b++;
    }
}

P4VOID p4_int_min_op(x, y, nelem)
char *x, *y;
int nelem;
{
    int *a = (int *) x;
    int *b = (int *) y;

    while (nelem--)
    {
	*a = MINVAL(*a, *b);
	a++;
	b++;
    }
}

P4VOID p4_int_absmax_op(x, y, nelem)
char *x, *y;
int nelem;
{
    int *a = (int *) x;
    int *b = (int *) y;

    while (nelem--)
    {
	*a = MAXVAL(ABSVAL(*a), ABSVAL(*b));
	a++;
	b++;
    }
}

P4VOID p4_int_absmin_op(x, y, nelem)
char *x, *y;
int nelem;
{
    int *a = (int *) x;
    int *b = (int *) y;

    while (nelem--)
    {
	*a = MINVAL(ABSVAL(*a), ABSVAL(*b));
	a++;
	b++;
    }
}

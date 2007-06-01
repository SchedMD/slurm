#include "p4.h"
#include "p4_sys.h"
#include "p4_fort.h"

VOID p4sendr_(type,dest,msg,len,rc)
int *type, *dest, *len;
char *msg;
int *rc;
{
    p4_dprintfl(20,"in p4sendr_, type=%d, dest=%d, len=%d\n",
		*type, *dest, *len);
    *rc = p4_sendr(*type, *dest, msg, *len);
}

VOID p4sendrx_(type,dest,msg,len,data_type,rc)
int *type, *dest, *data_type, *len;
char *msg;
int *rc;
{
    p4_dprintfl(20,"in p4sendrx_, type=%d, dest=%d, dtype=%d len=%d\n",
		*type, *dest, *data_type, *len);
    *rc = p4_sendrx(*type, *dest, msg, *len, *data_type);
}

VOID p4send_(type,dest,msg,len,rc)
int *type, *dest, *len;
char *msg;
int *rc;
{
    p4_dprintfl(20,"in p4send_, type=%d, dest=%d, len=%d\n",
		*type, *dest, *len);
    *rc = p4_send(*type, *dest, msg, *len);
}

VOID p4sendx_(type,dest,msg,len,data_type,rc)
int *type, *dest, *data_type, *len;
char *msg;
int *rc;
{
    p4_dprintfl(20,"in p4sendx_, type=%d, dest=%d, dtype=%d len=%d\n",
		*type, *dest, *data_type, *len);
    *rc = p4_sendx(*type, *dest, msg, *len, *data_type);
}

VOID p4recv_(type,from,buf,buflen,msglen,rc)
int *type, *from;
char *buf;
int *buflen, *msglen, *rc;
{
    char *temp_buf;
    int temp_len;

    p4_dprintfl(20, "p4_recv_: receiving, type=%d, from=%d, buflen=%d\n",
		*type, *from, *buflen);
    temp_buf = NULL;
    *rc = p4_recv(type,from,&temp_buf,&temp_len);
    if (*rc < 0)
	p4_dprintf("p4recv_: p4_recv failed\n");
    else
    {
	if (temp_len > *buflen)
	{
	    *msglen = *buflen;
	    *rc = 1;
	}
	else
	    *msglen = temp_len;
	bcopy(temp_buf,buf,*msglen);
	p4_msg_free(temp_buf);
    }
    p4_dprintfl(20, "p4_recv_: received, len=%d\n",*msglen);
}
  
VOID p4brdcst_(type,data,len,rc)
int *type, *len;
char *data;
int *rc;
{
    *rc = p4_broadcast(*type, data, *len);
}

VOID p4brdcstx_(type,data,len,data_type,rc)
int *type, *data_type, *len;
char *data;
int *rc;
{
    *rc = p4_broadcastx(*type, data, *len, *data_type);
}

VOID p4probe_(type,from,rc)
int *type, *from, *rc;
{
    *rc = (int) p4_messages_available(type, from);
}

int p4myclid_()
{
    return p4_get_my_cluster_id();
}

int p4nclids_()
{
    return p4_num_cluster_ids();
}

VOID p4globarr_(type)
int *type;
{
    (VOID) p4_global_barrier(*type);
}

VOID p4getclmasts_(numids,ids)
int *numids,*ids;
{
    (VOID) p4_get_cluster_masters(numids,ids);
}

VOID p4getclids_(start,end)
int *start,*end;
{
    (VOID) p4_get_cluster_ids(start,end);
}

int p4myid_()
{
    return p4_get_my_id();
}

int p4clock_()
{
    return p4_clock();
}

int p4ustimer_()
{
    return p4_ustimer();
}

int p4ntotids_()
{
    return p4_num_total_ids();
}

int p4nslaves_()
{
    return p4_num_total_slaves();
}

VOID p4error_(str,val)
char *str;
int *val;
{
    (VOID) p4_error(str,*val);
}

VOID p4avlbufs_()
{
    p4_print_avail_buffs();
}

VOID p4setavlbuf_(idx,size)
int *idx, *size;
{
    p4_set_avail_buff((*idx)-1,*size);
}

VOID p4softerrs_(new,old)
int new,*old;
{
    *old = (int) p4_soft_errors(new);
}

VOID p4version_()
{
    printf("p4version %s\n",p4_version());
}

VOID p4globop_(type, x, nelem, size, op, data_type, rc)
int *type;
char *x;
int *nelem;
int *size;
int *data_type;
int *rc;
VOID (*op)();
{
int *opind;

    *rc = p4_global_op(*type, x, *nelem, *size, op, *data_type);
}

VOID p4dblsumop_(a,b,n)
char *a,*b;
int n;
{
    p4_dbl_sum_op(a,b,n);
}

VOID p4dblmultop_(a,b,n)
char *a,*b;
int n;
{
    p4_dbl_mult_op(a,b,n);
}

VOID p4dblmaxop_(a,b,n)
char *a,*b;
int n;
{
    p4_dbl_max_op(a,b,n);
}

VOID p4dblminop_(a,b,n)
char *a,*b;
int n;
{
    p4_dbl_min_op(a,b,n);
}

VOID p4dblabsmaxop_(a,b,n)
char *a,*b;
int n;
{
    p4_dbl_absmax_op(a,b,n);
}

VOID p4dblabsminop_(a,b,n)
char *a,*b;
int n;
{
    p4_dbl_absmin_op(a,b,n);
}

VOID p4fltsumop_(a,b,n)
char *a,*b;
int n;
{
    p4_flt_sum_op(a,b,n);
}

VOID p4fltmultop_(a,b,n)
char *a,*b;
int n;
{
    p4_flt_mult_op(a,b,n);
}

VOID p4fltmaxop_(a,b,n)
char *a,*b;
int n;
{
    p4_flt_max_op(a,b,n);
}

VOID p4fltminop_(a,b,n)
char *a,*b;
int n;
{
    p4_flt_min_op(a,b,n);
}

VOID p4fltabsmaxop_(a,b,n)
char *a,*b;
int n;
{
    p4_flt_absmax_op(a,b,n);
}

VOID p4fltabsminop_(a,b,n)
char *a,*b;
int n;
{
    p4_flt_absmin_op(a,b,n);
}

VOID p4intsumop_(a,b,n)
char *a,*b;
int n;
{
    p4_int_sum_op(a,b,n);
}

VOID p4intmultop_(a,b,n)
char *a,*b;
int n;
{
    p4_int_mult_op(a,b,n);
}

VOID p4intmaxop_(a,b,n)
char *a,*b;
int n;
{
    p4_int_max_op(a,b,n);
}

VOID p4intminop_(a,b,n)
char *a,*b;
int n;
{
    p4_int_min_op(a,b,n);
}

VOID p4intabsmaxop_(a,b,n)
char *a,*b;
int n;
{
    p4_int_absmax_op(a,b,n);
}

VOID p4intabsminop_(a,b,n)
char *a,*b;
int n;
{
    p4_int_absmin_op(a,b,n);
}

VOID p4flush_()
{
    printf("\n");
    fflush(stdout);
}




#include "p4.h"
#include "p4_sys.h"

P4BOOL shmem_msgs_available()
{
    int rc, qidx;
    struct p4_msg_queue *mq;

    qidx = p4_local->my_id - p4_global->low_cluster_id;
    mq = &(p4_global->shmem_msg_queues[qidx]);
    rc = (mq->first_msg != NULL);
    return (rc);
}


struct p4_msg *shmem_recv()
{
    struct p4_msg_queue *mq;
    struct p4_queued_msg *q;
    struct p4_msg *m;
    int my_qidx, from_qidx;

    my_qidx = p4_local->my_id - p4_global->low_cluster_id;
    mq = &(p4_global->shmem_msg_queues[my_qidx]);
    p4_dprintfl(60, "receiving shmem messages %d\n", my_qidx);

    p4_menter(&mq->m);
    if (mq->first_msg == NULL)
    {
	p4_mdelay(&mq->m, 0);
    }
    q = mq->first_msg;
    if (mq->first_msg == mq->last_msg)
    {
	mq->first_msg = NULL;
	mq->last_msg = NULL;
    }
    else
	mq->first_msg = mq->first_msg->next;
    p4_mcontinue(&mq->m, 0);

    from_qidx = q->qmsg->from - p4_global->low_cluster_id;
    if (q->qmsg->ack_req & P4_ACK_REQ_MASK)
    {
	p4_dprintfl(30, "sending ack to %d\n", q->qmsg->from);
	p4_unlock(&(p4_global->shmem_msg_queues[from_qidx].ack_lock));
	p4_dprintfl(30, "sent ack to %d\n", q->qmsg->from);
    }

    m = q->qmsg;
    free_quel(q);
    p4_dprintfl(60, "received from %d via shmem\n", q->qmsg->from);
    return (m);
}				/* shmem_recv */


int shmem_send(tmsg)
struct p4_msg *tmsg;
{

    struct p4_msg_queue *mq;
    int to_qidx, from_qidx;

    p4_dprintfl(20, "sending msg of type %d from %d to %d via shmem\n",tmsg->type,tmsg->from,tmsg->to);
    to_qidx = tmsg->to - p4_global->low_cluster_id;
    from_qidx = tmsg->from - p4_global->low_cluster_id;
    mq = &(p4_global->shmem_msg_queues[to_qidx]);

    p4_menter(&mq->m);
    queue_p4_message(tmsg, mq);
    p4_mcontinue(&mq->m, 0);

    if (tmsg->ack_req & P4_ACK_REQ_MASK)
    {
	p4_dprintfl(30, "waiting for ack from %d\n", tmsg->to);
	p4_lock(&(p4_global->shmem_msg_queues[from_qidx].ack_lock));
	p4_dprintfl(30, "received ack from %d\n", tmsg->to);
    }
    p4_dprintfl(10, "sent msg of type %d from %d to %d via shmem\n",tmsg->type,tmsg->from,tmsg->to);

    return (0);
}

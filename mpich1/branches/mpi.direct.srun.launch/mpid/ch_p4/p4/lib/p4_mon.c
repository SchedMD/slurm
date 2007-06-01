#include "p4.h"
#include "p4_sys.h"

int p4_moninit(m, i)
p4_monitor_t *m;
int i;
{
    int j;
    struct p4_mon_queue *q;

    p4_lock_init(&(m->mon_lock));

    if (i)
    {
	m->qs = (struct p4_mon_queue *) p4_shmalloc(sizeof(struct p4_mon_queue) * i);
	if (m->qs == NULL)
	{
	    p4_dprintf("OOPS! p4_moninit: p4_shmalloc failed ***\n");
	    return (-1);
	}
	for (j = 0; j < i; j++)
	{
	    q = m->qs + j;
	    q->count = 0;
	    p4_lock_init(&(q->delay_lock));
	    p4_lock(&(q->delay_lock));
	}
    }
    else
	m->qs = NULL;
    return (0);
}

P4VOID p4_menter(m)
p4_monitor_t *m;
{
    ALOG_LOG(p4_local->my_id,REQUEST_MONITOR_ENTRY,m,"");
    p4_lock(&(m->mon_lock));
    ALOG_LOG(p4_local->my_id,ENTER_MONITOR,m,"");
}

P4VOID p4_mexit(m)
p4_monitor_t *m;
{
    ALOG_LOG(p4_local->my_id,OPEN_DOOR,m,"");
    ALOG_LOG(p4_local->my_id,EXIT_MONITOR,m,"");
    p4_unlock(&(m->mon_lock));
}

P4VOID p4_mdelay(m, i)
p4_monitor_t *m;
int i;
{
    struct p4_mon_queue *q;

    q = m->qs + i;
    q->count++;
    ALOG_LOG(p4_local->my_id,ENTER_DELAY_QUEUE,m,"");
    ALOG_LOG(p4_local->my_id,OPEN_DOOR,m,"");
    p4_unlock(&(m->mon_lock));
    p4_lock(&(q->delay_lock));
    ALOG_LOG(p4_local->my_id,EXIT_DELAY_QUEUE,m,"");
}

P4VOID p4_mcontinue(m, i)
p4_monitor_t *m;
int i;
{
    struct p4_mon_queue *q;

    q = m->qs + i;
    if (q->count)
    {
	q->count--;
	ALOG_LOG(p4_local->my_id,SECRET_EXIT_MONITOR,m,"");
	p4_unlock(&(q->delay_lock));
    }
    else
    {
	ALOG_LOG(p4_local->my_id,OPEN_DOOR,m,"");
	ALOG_LOG(p4_local->my_id,EXIT_MONITOR,m,"");
	p4_unlock(&(m->mon_lock));
    }
}

int num_in_mon_queue(m, i)
int i;
p4_monitor_t *m;
{
    struct p4_mon_queue *q;

    q = m->qs + i;
    return (q->count);
}


/* ------------------  getsub monitor -------------------- */

int p4_getsub_init(gs)
p4_getsub_monitor_t *gs;
{

    gs->sub = 0;
    return (p4_moninit(&(gs->m), 1));
}

P4VOID p4_getsubs(gs, s, max, nprocs, stride)
p4_getsub_monitor_t *gs;
int *s, max, nprocs, stride;
{
    p4_menter(&(gs->m));
    if (gs->sub <= max)
    {
	*s = gs->sub;
	gs->sub += stride;
	p4_mexit(&(gs->m));
    }
    else
    {
	*s = -1;
	if (num_in_mon_queue(&(gs->m), 0) < nprocs - 1)
	    p4_mdelay(&(gs->m), 0);
	else
	    gs->sub = 0;
	p4_mcontinue(&(gs->m), 0);
    }
}


/* ------------------  barrier monitor -------------------- */

int p4_barrier_init(b)
p4_barrier_monitor_t *b;
{

    return (p4_moninit(&(b->m), 1));
}

P4VOID p4_barrier(b, nprocs)
p4_barrier_monitor_t *b;
int nprocs;
{
    p4_menter(&(b->m));
    if (num_in_mon_queue(&(b->m), 0) < nprocs - 1)
	p4_mdelay(&(b->m), 0);
    p4_mcontinue(&(b->m), 0);
}


/* ------------------  askfor monitor -------------------- */

int p4_askfor_init(af)
p4_askfor_monitor_t *af;
{

    af->pgdone = 0;
    af->pbdone = 0;
    /* alog assumes only one askfor per program */
    ALOG_LOG(p4_local->my_id,PBDONE,0,"");
    ALOG_LOG(p4_local->my_id,PGDONE,0,"");
    ALOG_LOG(p4_local->my_id,UPDATE_NUM_SUBPROBS,0,"");
    return (p4_moninit(&(af->m), 1));
}

int p4_askfor(af, nprocs, getprob_fxn, problem, reset_fxn)
p4_askfor_monitor_t *af;
int nprocs;
int (*getprob_fxn) (P4VOID *);
P4VOID *problem;
P4VOID(*reset_fxn) (void);
{
    int rc;

    p4_menter(&(af->m));
    if (!(af->pgdone) && af->pbdone)
    {
	if (num_in_mon_queue(&(af->m), 0) < nprocs - 1)
	{
	    p4_mdelay(&(af->m), 0);
	}
    }
    else
    {
	while (!(af->pgdone) && !(af->pbdone))
	{
	    if ((rc = (*getprob_fxn) (problem)) == 0)
	    {
		p4_mcontinue(&(af->m), 0);
		return (rc);
	    }
	    else
	    {
		if (num_in_mon_queue(&(af->m), 0) == nprocs - 1)
		{
		    af->pbdone = 1;
		    ALOG_LOG(p4_local->my_id,PBDONE,1,"");
		}
		else
		{
		    p4_mdelay(&(af->m), 0);
		}
	    }
	}
    }
    if (af->pgdone)
    {
	rc = (-1);
	p4_mcontinue(&(af->m), 0);
    }
    else
    {
	rc = af->pbdone;
	if (num_in_mon_queue(&(af->m), 0) == 0)
	{
	    (*reset_fxn) ();
	    af->pbdone = 0;
	}
	p4_mcontinue(&(af->m), 0);
    }
    return (rc);
}

P4VOID p4_update(af, putprob_fxn, problem)
p4_askfor_monitor_t *af;
int (*putprob_fxn) (P4VOID *);
P4VOID *problem;
{
    p4_menter(&(af->m));
    if (putprob_fxn(problem))
	p4_mcontinue(&(af->m), 0);
    else
	p4_mexit(&(af->m));
}

P4VOID p4_probend(af, code)
p4_askfor_monitor_t *af;
int code;
{
    p4_menter(&(af->m));
    af->pbdone = code;
    ALOG_LOG(p4_local->my_id,PBDONE,code,"");
    p4_mexit(&(af->m));
}

P4VOID p4_progend(af)
p4_askfor_monitor_t *af;
{
    p4_menter(&(af->m));
    af->pgdone = 1;
    ALOG_LOG(p4_local->my_id,PGDONE,1,"");
    p4_mcontinue(&(af->m), 0);
}

int p4_create(fxn)
int (*fxn) (void);
{
    int rc;

    p4_dprintfl(20,"creating local slave via fork\n");
    if ((rc = fork_p4()) == 0)
    {
	/* slave process */
	(*fxn) ();
	exit(0);
    }
    /* else master process */
    p4_dprintfl(10,"created local slave via fork\n");
    return (rc);
}

#include <stdio.h>
#include <math.h>
#include "p4.h"
    
    /*  NOTE: This program contains some features which are more
	general than required for this specific task.  Please
	remember that we hope to use this as a more-or-less
	prototypical use of trees in numeric algorithms.  For
	example, the definition of tree_node includes "first_child"
	and "sibling" pointers, although they are never actually
	used (and, the tree could in any event just be
	a binary tree, rather than a binary tree representing
	a general tree).  Our intent is to discuss the topics
	of "general trees", "representing general trees with
	binary trees", etc.  It is also the case that the
	use of queue_nodes is unnecessary; you could just
	add a link field to the tree_nodes and queue the
	ready tree_nodes in the pool.  
	*/
    
    /* this structure gives the format of a node in the tree
       representing the gradual subdivision of the interval.
       Each node represented a portion of the interval from
       "xl" to "xr".
       */

int queue_node();

struct tree_node {
    double xl;		/* the left bound */
    double xm;		/* the midpoint */
    double xr;		/* the right bound */
    double yl,ym,yr;	/* values of the function */
    double integral;	/* computed value of the integral */
    int status;	        /* 0->not subdivided;
			   1->one side completed
			   2->both sides completed
			   
			   another way to think of this field is
			   as a count of the number of subcalculations
			   that have been completed
			   */
    struct tree_node *parent; /* pointer to the parent node */
    struct tree_node *first_child,*sibling;
    p4_lock_t t_lock;
};

struct queue_node {
    struct queue_node *next; /* link to next node in pool or q_avail */
    struct tree_node *node;  /* when node is in pool, this field points
				to a node in the tree that has been
				queued in the "pool" of work.
				*/
};

/* variables
   
   tree     - the tree representing the current state
   of the computation
   
   pool     - the pool of problems.  Initially, only one node
   will be in the pool, and it will represent the
   entire interval
   
   q_avail  - avail-list for queue nodes
   
   t_avail  - avail-list for tree nodes
   
   numprocs - number of processes to participate in the
   calculation
   
   normdiff - the error you are willing to tolerate in a
   unit interval
   */

struct globmem {
    
    p4_lock_t TAVL;
    struct tree_node *t_avail;  /* list of available tree_nodes */
    struct tree_node *tree;     /* the tree being built */
    
    p4_lock_t QAVL;
    struct queue_node *q_avail; /* list of available queue_nodes */
    struct queue_node *pool;    /* pool of available work */
    
    int numprocs;		/* number of processes */
    double normdiff;		/* absolute amount of tolerable error */
    
    p4_askfor_monitor_t MO;
} *glob;


slave()
{
    work();
}

main(argc,argv)
int argc;
char **argv;
{
    extern slave();
    char c;
    double r;
    int i,j;
    long stime,etime;
    double func();
    struct tree_node *t_node;
    struct tree_node *alloc_tree_node();
    
    
    p4_initenv(&argc,argv);
    glob = (struct globmem *) p4_shmalloc(sizeof(struct globmem));
    
    glob->t_avail = NULL;
    glob->q_avail = NULL;
    p4_lock_init(&(glob->TAVL));
    p4_lock_init(&(glob->QAVL));
    p4_askfor_init(&(glob->MO));
    
    t_node = alloc_tree_node(); 
    glob->tree = t_node;
    
    printf("left boundary: ");
    scanf("%lf",&(t_node->xl));
    printf("right boundary: ");
    scanf("%lf",&(t_node->xr));
    
    if (t_node->xr > 15.0)
    {
	printf("right boundary too big; try 15.0\n");
	exit(0);
    }
    
    /* set up original node - the midpoint, function
       values, and first approx of integral must be
       stored in the node
       */
    t_node->xm = (t_node->xl + t_node->xr) / 2.0;
    
    t_node->yl = func(t_node->xl);
    t_node->ym = func(t_node->xm);
    t_node->yr = func(t_node->xr);
    
    t_node->integral = (t_node->xr - t_node->xl) *
	(t_node->yl +
	 t_node->yr +
	 (4 * t_node->ym)) / 6.0;
    
    t_node->status = 0;
    t_node->parent = NULL;
    p4_lock_init(&(t_node->t_lock));
    
    /* stack the single node */
    p4_update(&(glob->MO),queue_node,(P4VOID *) t_node);
    
    printf("'allowable difference' for a unit: ");
    scanf("%lf",&(glob->normdiff));
    printf("number of processes: ");
    scanf("%d",&(glob->numprocs));
    
    stime = p4_clock();		/* note time includes process creation */
    
    /* create the slave processes */
    for (i=1; i < glob->numprocs; i++) 
	p4_create(slave);
    
    /* join the slaves in processing nodes in the pool */
    work(); 
    
    etime = p4_clock();
    
    printf("integral = %lf\n",glob->tree->integral); 
    printf("time = %d milliseconds\n",(etime - stime));
    
    p4_wait_for_end();
}

int getprob(v)
P4VOID **v;
{
    int rc = 1;   /* any non-zero means NO problem found */
    struct queue_node **p;
    
    p = (struct queue_node **) v;
    if (glob->pool != NULL)
    {
	*p = glob->pool;
        glob->pool = glob->pool->next;
	rc = 0;    /* FOUND a problem */
    }
    return(rc);
}

P4VOID reset()
{
}

work()
{
    int rc;
    struct tree_node *t_node;
    struct queue_node *q_node;
    int num_done = 0;
    
    rc = p4_askfor(&(glob->MO),glob->numprocs,getprob,(P4VOID *)&q_node,reset);
    
    while (rc == 0)
    {
	num_done++;
	t_node = q_node->node;
	dealloc_queue_node(q_node);
	
	evaluate(t_node);   /* process the node */
	
	rc = p4_askfor(&(glob->MO),glob->numprocs,getprob,(P4VOID *)&q_node,reset);
    }
    p4_dprintfl(5,"exiting work, did %d\n",num_done);
}

/* evaluate processes a node, which may cause subnodes
   to be created and stacked.
   */

evaluate(n)
struct tree_node *n;
{
    double xlm;  /* midpoint of left interval */
    double xrm;  /* midpoint of right interval */
    double leftint; /* integral of left */
    double rightint; /* integral of right */
    double ylm,yrm;  /* evaluated function for the midpoints */
    struct tree_node *lch,*rch;  /* pointers to children */
    struct tree_node *p; 
    double diff;
    struct tree_node *alloc_tree_node();
    
    /* first calculate the next level of approximation to
       see whether we are close enough
       */
    xlm = (n->xl + n->xm) / 2;
    xrm = (n->xm + n->xr) / 2;
    ylm = func(xlm);
    yrm = func(xrm);
    leftint = (n->xm - n->xl) * ((n->yl + n->ym + (4 * ylm)) / 6);
    rightint = (n->xr - n->xm) * ((n->ym + n->yr + (4 * yrm)) / 6);
    
    
    diff = fabs(n->integral - (leftint + rightint));
    
    if (diff < (glob->normdiff / (n->xr - n->xl)))
    {
	
	/* keep the more accurate estimate, and process completion
	   of this node
	   */
	n->integral = leftint + rightint;
	postcomp(n);
    }
    else
    {
	/* build the left node and stack it in the
	   pool of work to do
	   */
	lch = alloc_tree_node();
	lch->xl = n->xl;
	lch->xm = xlm;
	lch->xr = n->xm;
	lch->yl = n->yl;
	lch->ym = ylm;
	lch->yr = n->ym;
	lch->integral = leftint;
	lch->status = 0;
	lch->parent = n;
	p4_lock_init(&(lch->t_lock));

	p4_update(&(glob->MO),queue_node,(P4VOID *) lch);
	
	/* we now process the right child */
	rch = alloc_tree_node();
	rch->xl = n->xm;
	rch->xm = xrm;
	rch->xr = n->xr;
	rch->yl = n->ym;
	rch->ym = yrm;
	rch->yr = n->yr;
	rch->integral = rightint;
	rch->status = 0;
	rch->parent = n;
	p4_lock_init(&(rch->t_lock));
	
	evaluate(rch);
    }
}


/* this routine handles the "completion" of a node,
   which involves storing the answer in the parent node,
   checking to see if this completes the parent, and processing
   completion of the parent (if required).  The nodes are
   returned to the avail list as they are removed from the
   leaves of the tree (except the root!)
   */
postcomp(n)
struct tree_node *n;
{
    struct tree_node *p;   /* pointer to parent */
    int stat;         /* status of the parent */
    
    if ((p = n->parent) != NULL)
    {
	p4_lock(&(p->t_lock));
	if (p->status == 0) 
	    p->integral = 0.000;
	p->integral += n->integral;
	
	stat = ++(p->status);
	p4_unlock(&(p->t_lock));
	
	dealloc_tree_node(n);
	
        if (stat == 2)  /* if parent is complete too */
	{
	    postcomp(p);
	}
    }
}

/* this is the function to integrate */
double func(x)
double x;
{
    int i;
    
    /* the attribute "static" assures that the values do not change
       between invocations of the function.  That is, a single static
       copy is allocated and referenced by all calls to the routine.
       */
    static int power = 30;
    static int firstcall = 1;
    static double factor;
    static double pi;
    double v;
    
    
    if (firstcall)
    {
	factor = 1;
	for (i=1; (i <= (power/2)); i++)
	    factor = factor * (1 - (.5 / i));
	
	pi = 4 * atan((double) 1);
	firstcall = 0;
    }
    
    return(pow(sin((double) (pi * x)),(double) power) / factor);
}

/*
  this routine allocates a tree_node from globally-shared
  memory.
  */
struct tree_node *alloc_tree_node()
{
    struct tree_node *node;
    
    p4_lock(&(glob->TAVL));
    if ((node = glob->t_avail) == NULL)
    {
	node = (struct tree_node *) p4_shmalloc(sizeof(struct tree_node));
	if (node == NULL)
        {
            printf("*** out of memory in alloc_tree_node ***\n");
            exit(3);
        }
    }
    else
    {
        glob->t_avail = node->sibling;
    }
    p4_unlock(&(glob->TAVL));
    return(node);
}

/*
  this routine frees a tree_node
  */
dealloc_tree_node(node)
struct tree_node *node;
{
    
    p4_lock(&(glob->TAVL));
    node->sibling = glob->t_avail;
    glob->t_avail = node;
    p4_unlock(&(glob->TAVL));
}

/*
  this routine allocates a queue_node from globally-shared
  memory.
  */
struct queue_node *alloc_queue_node()
{
    struct queue_node *node;
    
    p4_lock(&(glob->QAVL));
    if ((node = glob->q_avail) == NULL)
    {
	node = (struct queue_node *) p4_shmalloc(sizeof(struct queue_node));
	if (node == NULL)
        {
            printf("*** out of memory in alloc_queue_node ***\n");
            exit(3);
        }
    }
    else
    {
        glob->q_avail = node->next;
    }
    p4_unlock(&(glob->QAVL));
    return(node);
}

/*
  this routine frees a queue_node
  */
dealloc_queue_node(node)
struct queue_node *node;
{
    
    p4_lock(&(glob->QAVL));
    node->next = glob->q_avail;
    glob->q_avail = node;
    p4_unlock(&(glob->QAVL));
}

/*
  this node adds a tree_node to the pool of work.
  This involves allocating a queue_node and hooking
  it into the pool.  Because the pool is constantly
  being altered by all of the processes, this must
  be a monitor operation.
  */

int queue_node(t_node)
struct tree_node *t_node;
{
    struct queue_node *alloc_queue_node();
    struct queue_node *q_node;
    
    q_node = alloc_queue_node();
    q_node->node = t_node;
    q_node->next = glob->pool;
    glob->pool = q_node;
    return(1);			/* new problem */
}

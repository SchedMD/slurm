#include "p4.h"
#define ROWS 		200
#define COLUMNS 	200
struct globmem {
    double a[ROWS+2][COLUMNS+2];
    double b[ROWS+2][COLUMNS+2];
    int st[ROWS+2], pq[ROWS+1];
    int pqbeg, pqend, goal, nproc, rows, columns;
    p4_askfor_monitor_t MO;
    p4_barrier_monitor_t BA;
} *glob;

double avggrid();
double avgbnd();

slave()
{
    work('s');
}

phi(x,y)			/* The function on the boundary */
int x,y;
{
      return((x * x) - (y * y) + (x * y));   
}

main(argc,argv)
int argc;
char **argv;
{
    int i;
    int timestart, timeend;
    p4_usc_time_t start_ustime, end_ustime;
    double avg;

    p4_initenv(&argc,argv);

    glob = (struct globmem *) p4_shmalloc(sizeof(struct globmem));

    p4_askfor_init(&(glob->MO));
    p4_barrier_init(&(glob->BA));

    printf("enter number of processes: ");
    scanf("%d",&glob->nproc);
    printf("enter the number of rows: ");
    scanf("%d",&glob->rows);
    printf("enter the number of columns: ");
    scanf("%d",&glob->columns);
    printf("enter the number of iterations: ");
    scanf("%d",&glob->goal);

    gridinit(glob->a,glob->rows,glob->columns);
    gridinit(glob->b,glob->rows,glob->columns);
    
    glob->pqbeg = glob->pqend = 0;
    for (i=1; i <= glob->rows; i++)
        queueprob(i);

    /* initialize the status vector */
    for (i=0; i < (glob->rows+2); i++)
        glob->st[i] = 0;

    printf("\nnproc\tgoal\trows\tcolumns\n");
    printf("%d \t  %d \t  %d \t  %d \n",
	   glob->nproc,glob->goal,glob->rows,glob->columns);
    for (i=1; i <= glob->nproc-1; i++) {
        p4_create(slave);
    }
    timestart = p4_clock();
    start_ustime = p4_ustimer();
    work('m');
    end_ustime = p4_ustimer();
    timeend = p4_clock();
    printf("total time %.3f seconds\n",(timeend - timestart)/1000.0);
    printf("total time %.6f seconds\n",(end_ustime-start_ustime)/1000000.0);

/* 
    printf("the resulting grid:\n");
    if (glob->goal % 2 == 0)
        printgrid(glob->a,glob->rows,glob->columns);
    else
        printgrid(glob->b,glob->rows,glob->columns);
*/
    if (glob->goal % 2 == 0)
        avg = avggrid(glob->a,glob->rows,glob->columns);
    else
        avg = avggrid(glob->b,glob->rows,glob->columns);
    printf("average value of grid = %f\n",avg);

    p4_wait_for_end(); 
}

/* "m" is the matrix, "r" is the number of rows of data (m[1]-m[r];
   m[0] and m[r+1] are boundaries), and "c" is the number of columns
   of data.
*/

gridinit(m,r,c)
double m[ROWS+2][COLUMNS+2];
int r, c;
{
    int i, j;
    double bndavg;
    
    for (j=0; j < (c + 2); j++)
    {
        m[0][j] = phi(1,j+1);
        m[r+1][j]= phi(r+2,j+1);
    }
    for (i=1; i < (r + 2); i++)
    {
        m[i][0] = phi(i+1,1);
        m[i][c+1] = phi(i+1,c+2);
    }
    bndavg = avgbnd(m,r,c);
    printf("boundary average = %f\n",bndavg);

    /* initialize the interior of the grids to the average over the boundary*/
    for (i=1; i <= r; i++)
        for (j=1; j <= c; j++)
            /* m[i][j] = bndavg; this optimization hinders debugging */
	    m[i][j] = 0;
}

queueprob(x)
int x;
{
    glob->pq[glob->pqend] = x;
    glob->pqend = (glob->pqend + 1) % (ROWS + 1);
}

compute(p,q,r,columns)
double p[ROWS+2][COLUMNS+2];
double q[ROWS+2][COLUMNS+2];
int r;
int columns;
{
    int j;

    for (j = 1; j <= columns; j++) 
        q[r][j] = (p[r-1][j] + p[r+1][j] + p[r][j-1] + p[r][j+1]) / 4.0;
}

int putprob(r)
int r;
{
    int qprob;

    qprob = FALSE;
    glob->st[r]++;
    if (r == 1)
        glob->st[0] = glob->st[r];

    else if (r == glob->rows)
        glob->st[glob->rows+1] = glob->st[r];

    if (glob->st[r] < glob->goal)
    {
        if ((r > 1) && (glob->st[r-2] >= glob->st[r]) 
		    && (glob->st[r-1] == glob->st[r]))
	{
            queueprob(r-1);
            qprob = TRUE;
        }
        if (r < glob->rows && glob->st[r+1] == glob->st[r] 
			   && glob->st[r+1] <= glob->st[r+2])
	{
            queueprob(r+1);
            qprob = TRUE;
        }
        if (glob->st[r-1] == glob->st[r] && 
	    glob->st[r] == glob->st[r+1])
	{
            queueprob(r);
            qprob = TRUE;
        }
    }
    if (qprob)
        return(1);		/* new problem */
    else
	return(0);		/* no new problem */
}

int getprob(v)
int *v;
{
    int rc = 1;
    int *p = (int *) v;

    if (glob->pqbeg != glob->pqend) 
    {
	*p = glob->pq[glob->pqbeg];
	glob->pqbeg = (glob->pqbeg+1) % (ROWS + 1); 
	rc = 0;
    }
    return(rc);
}

P4VOID reset() 
{
}

work(who)			/* main routine for all processes */
char who;
{
    int r,rc,i;

    p4_barrier(&(glob->BA),glob->nproc);

    rc = p4_askfor(&(glob->MO),glob->nproc,getprob,(P4VOID *)&r,reset);

    while (rc == 0) {
	if ((glob->st[r] % 2) == 0)
	    compute(glob->a,glob->b,r,glob->columns);
	else 
	    compute(glob->b,glob->a,r,glob->columns);
	p4_update(&(glob->MO),putprob,(P4VOID *) r);
	/* postprob(r); */

	rc = p4_askfor(&(glob->MO),glob->nproc,getprob,(P4VOID *)&r,reset);
    }
}

printgrid(m,r,c)
double m[ROWS+2][COLUMNS+2];
int r,c;
{
    int i,j;
    for (i = 0; i < (r+2); i++)
	for (j = 0; j < (c+2); j++)
	    printf("%3d %3d %10.5f\n",i,j,m[i][j]);
}

double avggrid(m,r,c)
double m[ROWS+2][COLUMNS+2];
int r,c;
{
    int i,j;
    double avg = 0;

    for (i = 0; i < (r+2); i++)
	for (j = 0; j < (c+2); j++)
	    avg += m[i][j];
    return(avg/((r+2)*(c+2)));
}

double avgbnd(m,r,c)
double m[ROWS+2][COLUMNS+2];
int r,c;
{
    int i,j;
    double avg = 0;

    for (i = 0; i < (r+2); i++)
	    avg += m[i][0];
    for (i = 0; i < (r+2); i++)
	    avg += m[i][c+1];
    for (i = 1; i < (c+1); i++)
	    avg += m[0][i];
    for (i = 1; i < (c+1); i++)
	    avg += m[r+1][i];
    return(avg/(2*(c+2) + 2*(r+2) - 4)); /* average over boundary */
}


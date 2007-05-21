#include "p4.h"

#ifndef TRUE
#define BOOL            int
#define TRUE            1
#define FALSE           0
#endif

/*  the following numbers define a grid of 4 processes, plus one master */
#define ROWS		100
#define COLUMNS		100

#define ROWS_PER_SUB 	50
#define COLUMNS_PER_SUB 50



#define PROCS_PER_COL	(ROWS / ROWS_PER_SUB)
#define PROCS_PER_ROW	(COLUMNS / COLUMNS_PER_SUB)
#define N_PROCS		(PROCS_PER_ROW * PROCS_PER_COL)

/* message types */
#define CNTL            0
#define C_BOUNDARY	1
#define R_BOUNDARY	2
#define ANSWER      	3

#define MASTER		0    /*master proc id */

struct cntl_rec {
  	int row;
	int col;
        int upper_neighbor;
	int right_neighbor;
	int lower_neighbor;
	int left_neighbor;
	int iterations;
	double bounded_subgrid[ROWS_PER_SUB+2][COLUMNS_PER_SUB+2];
	};

struct c_boundary {
	double col[ROWS_PER_SUB];
	};		

struct r_boundary {
       	double row[COLUMNS_PER_SUB];
	};

struct answer_rec {
	double subgrid[ROWS_PER_SUB][COLUMNS_PER_SUB];
        };

double avggrid();
double avgbnd();

/* some compilers require this to be outside main (too many locals) */
double  grid[ROWS+2][COLUMNS+2];


main(argc,argv)
int argc;
char *argv[];
{

    struct cntl_rec rec1;
    struct answer_rec *rec2;

    int		process_id[N_PROCS], proc_id;
    int 	proc, i, j, f_row, f_col, ln, msg_type;
    int timestart, timeend;
    double avg;

    p4_initenv(&argc,argv);
    p4_create_procgroup();
    if (p4_get_my_id() != 0)
    {
	slave();
	exit(0);
    }

    /*  old way - RL
    gridinit(grid,ROWS+2,COLUMNS+2);
    */
    gridinit(grid,ROWS,COLUMNS);

    printf("Enter the number of iterations: ");
    scanf("%d",&(rec1.iterations));		

    timestart = p4_clock();

    for (proc=0; proc < N_PROCS; proc++)
	process_id[proc] = proc+1;

    for (proc=0; proc < N_PROCS; proc++)
    {
	rec1.row = first_row(proc);
	rec1.col = first_column(proc);
	rec1.left_neighbor = 0;
	rec1.right_neighbor = 0;
	rec1.upper_neighbor = 0;
	rec1.lower_neighbor = 0;
	if (!upper_bound(rec1.row)) {
	    i = rec1.row - ROWS_PER_SUB;
	    j = which_proc(i,rec1.col);
	    rec1.upper_neighbor = process_id[j];
	}
	if (!lower_bound(rec1.row)) {
	    i = rec1.row + ROWS_PER_SUB;
	    j = which_proc(i,rec1.col);
	    rec1.lower_neighbor = process_id[j];
	}
	if (!right_bound(rec1.col)) {
	    i = rec1.col + COLUMNS_PER_SUB;
	    j = which_proc(rec1.row,i);
	    rec1.right_neighbor = process_id[j];
	}
	if (!left_bound(rec1.col)) {
	    i = rec1.col - COLUMNS_PER_SUB;
	    j = which_proc(rec1.row,i);
	    rec1.left_neighbor = process_id[j];
	}

	for (i=(rec1.row - 1); i <= (rec1.row + ROWS_PER_SUB); i++) 
	{

	    for (j=(rec1.col - 1); j <= (rec1.col + COLUMNS_PER_SUB); j++)
	    {
		rec1.bounded_subgrid[(i-(rec1.row-1))][(j-(rec1.col-1))] =
		    grid[i][j];
	    }
	}
	ln = sizeof(struct cntl_rec);
	p4_sendr(CNTL,process_id[proc],&rec1,ln);
    }

    for (proc=0; proc < N_PROCS; proc++)
    {
	p4_dprintfl(5,"master receiving answer\n");
	
	msg_type = ANSWER;
	proc_id = -1; 
	rec2 = NULL;
	p4_recv(&msg_type,&proc_id,&rec2,&ln);
	p4_dprintfl(5,"master received answer from slave %d\n",proc_id);
	f_row = first_row(proc_id - 1);
	f_col = first_column(proc_id - 1);
	for (i=0; i < ROWS_PER_SUB; i++) 
	    for (j=0; j < COLUMNS_PER_SUB; j++)
		grid[f_row + i][f_col + j] = rec2->subgrid[i][j];
    }
    timeend = p4_clock();
    printf("total time %.3f seconds\n",(timeend - timestart)/1000.0);
    p4_wait_for_end();

    /* printgrid(grid,ROWS,COLUMNS); */
    avg = avggrid(grid,ROWS,COLUMNS);
    printf("average value of grid = %f\n",avg);
}

first_row(proc)
int proc;
{
    return(((proc / PROCS_PER_ROW) * ROWS_PER_SUB) + 1);
}

first_column(proc)
int proc;
{
    return(((proc % PROCS_PER_ROW) * COLUMNS_PER_SUB) + 1);
}

which_proc(row,column)
int row,column;
{
    return((((row - 1) / ROWS_PER_SUB) * PROCS_PER_ROW) + 
	   ((column - 1) / COLUMNS_PER_SUB));
}

/* old way - RL
gridinit(m,x,y)
double m[ROWS+2][COLUMNS+2];
int x,y;
{
    int i, j;

    for (i=0; i <= x-1; i++)
        for (j=0; j <= y-1; j++)
            m[i][j] = 0;

    for (j=0; j < y; j++)
    {
        m[0][j] = phi(1,j+1);
        m[x-1][j]= phi(x+1,j+1);
    }
    for (i=0; i < x; i++)
    {
        m[i][0] = phi(i+1,1);
        m[i][y-1] = phi(i+1,y+1);
    }
}
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

phi(x,y)
int x,y;
{
	return(x*x-y*y+x*y);
}

printgrid(grid,r,c)
double grid[ROWS+2][COLUMNS+2];
int r,c;
{
    int i,j;

    for (i = 0; i < (r+2); i++)
	for (j = 0; j < (c+2); j++)
	    printf("grid[%3d][%3d] = %10.5f\n",i,j,grid[i][j]);
}


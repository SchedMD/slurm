#include "p4.h"
    
#ifndef TRUE
#define BOOL            int
#define TRUE            1
#define FALSE           0
#endif
    
#define ROWS	         100
#define COLUMNS	         100
#define ROWS_PER_SUB     50
#define COLUMNS_PER_SUB	 50

#define MASTER		  0
    
#define PROCS_PER_COL	(ROWS / ROWS_PER_SUB)
#define PROCS_PER_ROW	(COLUMNS / COLUMNS_PER_SUB)
#define N_PROCS		(PROCS_PER_ROW * PROCS_PER_COL)

/* message types */
#define CNTL            0
#define C_BOUNDARY	1
#define R_BOUNDARY	2
#define ANSWER      	3

/* log event types */
#define SEND            102
#define RECEIVE         101
#define CREATE          100
#define WAIT_MSG        6
#define WORKING         4
#define DONE_WORK       5

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


/* some compilers require this to be outside slave (too many locals) */
double grid1[ROWS_PER_SUB+2][COLUMNS_PER_SUB+2];
double grid2[ROWS_PER_SUB+2][COLUMNS_PER_SUB+2];

slave()
{
    struct cntl_rec *rec1;
    double *current_grid, *next_grid, *temp;
    int master_id, proc_id, msg_type;
    int i, j, n, ln;
    
    master_id = MASTER;
    msg_type = CNTL;
    rec1 = NULL;
    p4_recv(&msg_type,&master_id, &rec1, &ln);
    for (i=0; i < (ROWS_PER_SUB + 2); i++) 
        for (j=0; j < (COLUMNS_PER_SUB + 2); j++)
	{
	    grid2[i][j] = rec1->bounded_subgrid[i][j];
	    grid1[i][j] = rec1->bounded_subgrid[i][j];
	};
    
    current_grid = (double *) grid1;
    next_grid = (double *) grid2;
    p4_dprintfl(5,"in slave, iterations = %d\n",rec1->iterations);
    for (n=rec1->iterations, i=0; i < n; i++)
    {
	/*
	if ((rec1->row == 1) && (rec1->col == 1))
	    printf("current subgrid at %d,%d, after %d iterations\n",
		   rec1->row,rec1->col,i);
        printsubgrid(current_grid,ROWS_PER_SUB,COLUMNS_PER_SUB);
	*/
	compute1_iter(current_grid,next_grid);
	if (!((i + 1) == n))   /* if not the last iteration */
	{
	    if (!upper_bound(rec1->row))
		send_row(next_grid, 1, rec1->upper_neighbor);
	    if (!lower_bound(rec1->row))
		send_row(next_grid, ROWS_PER_SUB, rec1->lower_neighbor);
	    if (!left_bound(rec1->col))
		send_col(next_grid, 1, rec1->left_neighbor);
	    if (!right_bound(rec1->col))
		send_col(next_grid, COLUMNS_PER_SUB, rec1->right_neighbor);
	    if (!lower_bound(rec1->row))
		receive_row(next_grid, (ROWS_PER_SUB+1),rec1->lower_neighbor);
	    if (!upper_bound(rec1->row))
		receive_row(next_grid, 0, rec1->upper_neighbor);
	    if (!right_bound(rec1->col))
		receive_col(next_grid,(COLUMNS_PER_SUB+1),rec1->right_neighbor);
	    if (!left_bound(rec1->col))
		receive_col(next_grid, 0, rec1->left_neighbor);
	}
	/* swap grids */
	temp = current_grid;
	current_grid = next_grid;
	next_grid = temp;
    }
    send_answer(current_grid, master_id);
}  

send_row(grid, row, proc_id)
double grid[ROWS_PER_SUB+2][COLUMNS_PER_SUB+2];
int row, proc_id;
{
    int x, ln, msg_type;
    struct r_boundary m1;
    
    msg_type = R_BOUNDARY;
    for (x=1; x <= COLUMNS_PER_SUB; x++)
	m1.row[x-1] = grid[row][x];
    ln = sizeof(struct r_boundary);
    p4_send(msg_type, proc_id, &m1, ln);
}

send_col(grid, col, proc_id)
double grid[ROWS_PER_SUB+2][COLUMNS_PER_SUB+2];
int col;
int proc_id;
{
    struct c_boundary m1;
    int i, ln, msg_type;
    
    msg_type = C_BOUNDARY;
    ln = sizeof(struct c_boundary);
    for (i=1; i <= ROWS_PER_SUB; i++)
	m1.col[i-1] = grid[i][col];
    p4_send(msg_type, proc_id, &m1, ln);
}

receive_row(grid, row, proc_id)
double grid[ROWS_PER_SUB+2][COLUMNS_PER_SUB+2];
int row;
int proc_id;
{
    struct r_boundary *m1;
    int sender, mln, x, msg_type;
    
    msg_type = R_BOUNDARY;
    sender   = proc_id;
    m1 = NULL;
    p4_recv(&msg_type,&sender, &m1, &mln);
    for (x=1; x <= COLUMNS_PER_SUB; x++)
	grid[row][x] = m1->row[x-1];
    p4_msg_free(m1);
}

receive_col(grid, col, proc_id)
double grid[ROWS_PER_SUB+2][COLUMNS_PER_SUB+2];
int col;
int proc_id;
{
    struct c_boundary *m1;
    int sender, mln, x, msg_type;
    
    msg_type = C_BOUNDARY;
    sender = proc_id;
    m1 = NULL;
    p4_recv(&msg_type,&sender, &m1, &mln);
    for (x=1;x <= ROWS_PER_SUB; x++)
	grid[x][col] = m1->col[x-1];
    p4_msg_free(m1);
}


send_answer(grid, master_id)
double grid[ROWS_PER_SUB+2][COLUMNS_PER_SUB+2];
int master_id;
{
    struct answer_rec rec1;
    int i, j, ln, msg_type;
    
    msg_type = ANSWER;
    for (i=1; i <= ROWS_PER_SUB; i++)
	for (j=1; j <= COLUMNS_PER_SUB; j++)
	    rec1.subgrid[i-1][j-1] = grid[i][j];
    ln = sizeof(struct answer_rec);
    p4_dprintfl(5,"sending answer\n");

    p4_sendr(msg_type,master_id, &rec1, ln);

    p4_dprintfl(5,"sent answer\n");
}

compute1_iter(current,next)
double current[ROWS_PER_SUB+2][COLUMNS_PER_SUB+2];
double next[ROWS_PER_SUB+2][COLUMNS_PER_SUB+2];
{
    int i, j;
    
    for (i=1; i <= (ROWS_PER_SUB); i++)
	for (j=1; j <= COLUMNS_PER_SUB; j++)
	    next[i][j] = (current[i-1][j] + 
			  current[i+1][j] + 
			  current[i][j-1] + 
			  current[i][j+1]) / 4.0;
}

printsubgrid(grid,r,c)
double grid[ROWS_PER_SUB+2][COLUMNS_PER_SUB+2];
int r,c;
{
    int i,j;

    for (i = 0; i < (r+2); i++)
	for (j = 0; j < (c+2); j++)
	    printf("grid[%3d][%3d] = %10.5f\n",i,j,grid[i][j]);
}

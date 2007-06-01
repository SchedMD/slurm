#define BOOL            int
#define TRUE            1
#define FALSE           0

#define ROWS		 100
#define COLUMNS		 100
#define ROWS_PER_SUB     50
#define COLUMNS_PER_SUB  50

#define PROCS_PER_COL	(ROWS / ROWS_PER_SUB)
#define PROCS_PER_ROW	(COLUMNS / COLUMNS_PER_SUB)
#define N_PROCS		(PROCS_PER_ROW * PROCS_PER_COL)

upper_bound(row)
int row;
{
    return((row == 1));
}

lower_bound(row)
int row;
{
    return((row == (ROWS - ROWS_PER_SUB + 1)));
}

right_bound(column)
int column;
{
    return((column == (COLUMNS - COLUMNS_PER_SUB + 1)));
}

left_bound(column)
int column;
{
    return((column == 1));
}

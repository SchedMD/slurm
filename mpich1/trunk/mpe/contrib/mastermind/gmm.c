#define LOGGING
#include "mpi.h"
#ifdef USE_GRAPHICS
#define MPE_GRAPHICS
#include "mpe.h"
#endif
#include <stdio.h>
#include <math.h>

/* Numeric type representing a guess: double or unsigned long */
typedef double GUESST;
/* corresponding MPI type */
#define MPI_GUESST MPI_DOUBLE
/* 1 for GUESST=unsigned, 0 for  DOUBLE */
#define GUESST_INTEGRAL 0

#if GUESS_INTEGRAL
#define MAX_GUESST 4294967295.0 /* 2^32 - 1 */
#else
#define MAX_GUESST 18014398509481984.0 /* 2^54 - 1 */
#endif

/* message tags */
#define GUESS 0
#define GUESS_LENGTH columns+2
     /* guess[0],   ..., guess[columns-1], row_num, guesses_done */
#define ACCEPTED 1
#define ACCEPTED_LENGTH 2
     /* bulls, cows */
#define NEW_INFO 2
#define NEW_INFO_LENGTH columns+3
     /* bulls, cows, guess[0],..., guess[columns-1], source*/
#define EXIT 3
#define EXIT_LENGTH 0
#define WON 4
#define WON_LENGTH 0
#define TASK 5
#define TASK_LENGTH 2
     /* task_start, task_size */
#define TASK_REQ 6
#define TASK_REQ_LENGTH 0
#define FINISHED 7
#define FINISHED_LENGTH 1
#define MAX_MSG_LENGTH NEW_INFO_LENGTH

/* further internal tags */
#define REJECTED 5
#define PROGRESS 6


#ifdef USE_GRAPHICS
/* data for graphics */
#define HDIST 35
#define VDIST 50
#define ROWS 16
#define RADIUS 10
#define SCORE_RADIUS 3
#define SCORE_VDIST 8
#define SCORE_HDIST 8
#define SCORE_ROWS 4
#define SCORE_COLS 4
#define SCORE_WIDTH SCORE_COLS*SCORE_HDIST
#define WORKER_WIDTH 10
#define WORKER_HEIGHT 10
#define WORKER_HDIST 20
#define COLOURSCALE_WIDTH 20
#define COLOURSCALE_HDIST 30
#define SUCCESS_HEIGHT 4

/* Maps slave numbers 1, ..., numprocs-1 to MPE colours */
#define WorkerColour(N) N+1 /* To exclude black which is 1 */

/* Maps peg colours 0, ..., colours-1 to MPE colours */
#define PegColour(N) N+2

#endif /* USE_GRAPHICS */

/* max size of the mastermind board */
#ifdef USE_GRAPHICS
/* limitations of the graphics: */
#define MAXCOLS  SCORE_ROWS*SCORE_COLS
#define MAXCOLOURS 14 /* black and white excluded from the 16 allowed colours*/
#else
#define MAXCOLS  20
#define MAXCOLOURS 100
#endif
#define MAXGUESSES 500

#define MAXTASKS 1000
#define MIN_TASK_SIZE 20

#define MASTER_RANK 0
#define NO_COLOUR -1 /* different from any valid peg colour */

/* global variables */
int numprocs;
int myid;
int colours, columns, numtasks;
GUESST guesses_done;
GUESST search_space_size;

int guess[MAXCOLS+2];
int secret[MAXCOLS];

/* DATA STRUCTURES */

/* the structure of the board (should better be a struct!)
   board[i][0]  [1]    [2]         ...  [columns+1]       [columns+2]
   -----------  -----  ---------        ----------------  -----------
   bulls,       cows,  guess[0],   ..., guess[columns-1], source (worker num)

*/
int board[MAXGUESSES][MAXCOLS+3];
int sources[MAXGUESSES]; /* who does the guess come from */


typedef struct task 
{
    struct task *next;
    struct task *previous;
    int guess[MAXCOLS+2];
    GUESST guess_number;
    GUESST guesses_remaining;
} TASKT;

TASKT *free_tasks, *curr_task;

#define CURR_GUESS (curr_task->guess)
#define Check_Arg(Var, Txt, Low, High) \
  if (Var > High || Var < Low)\
    {\
      if (myid == 0)\
	printf("%s: %d, should be between %d and %d. Exiting.\n",\
	       Txt, Var, Low, High);\
      MPI_Finalize();\
      return;\
    }

TASKT task_storage[MAXTASKS];

GUESST initial_tasks[MAXTASKS*2];

int next_row;
int freq_counter;
#define FREQUENCY 500

#ifdef USE_GRAPHICS
int height, width, left_col_width;
MPE_XGraph handle;
#endif

extern GUESST next_guess();

main(argc, argv)
int argc;
char *argv[];
{

  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
  MPI_Comm_rank(MPI_COMM_WORLD, &myid);


#ifdef LOGGING
  MPE_Init_log();
  MPE_Stop_log();
  if (myid == 0) {
    MPE_Describe_state(1, 2, "Send",      "green:light_gray");
    MPE_Describe_state(3, 4, "Admin",     "blue:gray3");
    MPE_Describe_state(5, 6, "Receive",   "red:vlines3");
  }
#endif


#ifndef INTERACTIVE
  if (argc < 3)
    {
      if (myid == 0)
	fprintf(stderr, "usage: %s colours columns [tasks]\n", argv[0]);
      exit(1);
    }

  colours = atoi(argv[1]);
  columns = atoi(argv[2]);

  if (argc == 3)
    numtasks = 997;
  else
    numtasks = atoi(argv[3]);

  Check_Arg(colours, "Colours", 2, MAXCOLOURS);
  Check_Arg(columns, "Columns", 2, MAXCOLS);
  Check_Arg(numtasks,"Tasks",   1, MAXTASKS);
#endif

#ifdef USE_GRAPHICS
  Check_Arg(numprocs, "Processors", 2, ROWS+1);
#endif

  if (myid == 0)
    master();
  else
    slave();


#ifdef LOGGING
  MPE_Finish_log("gmm.log");
#endif


#ifdef USE_GRAPHICS
  MPE_Close_graphics(&handle);
#endif
  MPI_Finalize();
}

slave()
{
  int done;
  GUESST skipped;
  MPI_Status status;
  int i, j, k, flag, count, colour;
  int col_to_change;
  int numslaves = numprocs-1;

  guesses_done = 0;
  next_row = 0;
  initialize_mm();


#ifdef LOGGING
  MPE_Start_log();
  MPE_Log_event(3,0,"");
#endif

  MPI_Recv(initial_tasks, MAXTASKS*2, MPI_GUESST, 
	       MASTER_RANK, TASK, MPI_COMM_WORLD, &status);

#ifdef LOGGING
  MPE_Log_event(4,0,"");
#endif


  MPI_Get_count(&status, MPI_GUESST, &count);
  count /= 2;

  /* Get initial tasks */

  for (i=0, j=0; i< count; i++, j+=2)
  {
      GUESST guessnum = initial_tasks[j];
      task_storage[i].guess_number = guessnum;
      task_storage[i].guesses_remaining = initial_tasks[j+1];
      task_storage[i].previous = &task_storage[i-1];
      task_storage[i].next = &task_storage[i+1];
	     
      for (k=columns-1; k>=0;  k--)
	{
#if GUESST_INTEGRAL
	  task_storage[i].guess[k] = guessnum % colours;
	  guessnum /= colours;
#else
	  colour = (int) fmod(guessnum, (double) colours);
	  task_storage[i].guess[k] = colour;
	  guessnum = floor(((guessnum-colour)/(double)colours)+.1);
#endif	  
	}
  }

  task_storage[count-1].next = &task_storage[0];
  task_storage[0].previous = &task_storage[count-1];
  curr_task = &task_storage[0];

#ifdef DEBUG
  for (i=0; i<count;i++)
    {
      printf("Task_storage[%d] (0x%x) = (num=%d, rem=%d, prev=0x%x, next=0x%x)\n",
	     i, &task_storage[i], task_storage[i].guess_number,
	     task_storage[i].guesses_remaining,
	     task_storage[i].previous,
	     task_storage[i].next);
    }
#endif

  init_free_task_storage(count);

#ifdef PRINTING
  trace_guess("STARTING: ", "\n");
#endif
  freq_counter = FREQUENCY;

  done = 0;


  while(!done)
  {
      if (freq_counter-- == 0)
      {
#ifdef USE_GRAPHICS
	  draw_guess(myid-1, 1, CURR_GUESS, myid);
	  draw_progress(myid-1, PROGRESS, 0);
	  MPE_Update(handle);
#endif
	  while (1)
	  {
	      MPI_Iprobe(0, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &status);
	      if (flag == 1)
	      {
		  if (status.MPI_TAG == EXIT)
		  {
		      MPI_Recv(NULL, EXIT_LENGTH, MPI_INT, MASTER_RANK,
			       EXIT, MPI_COMM_WORLD, &status);
		      done = 1;
		      break;
		  }
		  else if (status.MPI_TAG == NEW_INFO)
		  {
		      MPI_Recv(&board[next_row][0], NEW_INFO_LENGTH, MPI_INT, 
			       MASTER_RANK, NEW_INFO, MPI_COMM_WORLD, &status);
#ifdef USE_GRAPHICS
		      draw_progress(myid-1, NEW_INFO,
				    board[next_row][columns+2]);  
		      MPE_Update(handle);
#endif
#ifdef PRINTING
		      printf("%2d: NEW INFO, row num: %d\n", myid, next_row); 
#endif
		      next_row++;
		  }
		  else
		      break;
	      }
	      else
		  break;
	  } 

	  freq_counter=FREQUENCY;
      }

      if (guess_consistent(&col_to_change))
      {
#ifdef DEBUG	 
	  trace_guess("sending:  ", "\n");
#endif
	  CURR_GUESS[columns] = next_row;
	  CURR_GUESS[columns+1] = 
	    (int)(guesses_done/search_space_size*numslaves*100);
#ifdef LOGGING
  MPE_Log_event(1,0,"");
#endif
	  MPI_Send(CURR_GUESS, GUESS_LENGTH, MPI_INT, MASTER_RANK,
		   GUESS, MPI_COMM_WORLD);
#ifdef LOGGING
  MPE_Log_event(2,0,"");
  MPE_Log_event(5,0,"");
#endif
	  MPI_Recv(&board[next_row][0], MAX_MSG_LENGTH, MPI_INT, MASTER_RANK,
		   MPI_ANY_TAG, MPI_COMM_WORLD, &status);
#ifdef LOGGING
  MPE_Log_event(6,0,"");
#endif
	  switch (status.MPI_TAG)
	  {
	    case EXIT: 
	      done = 1;
	      break;
	    case WON:
	      done = 1;
#ifdef USE_GRAPHICS
	      draw_progress(myid-1, ACCEPTED, 0);  
	      MPE_Update(handle);
#endif
	      break;
	    case ACCEPTED:
#ifdef USE_GRAPHICS
	      draw_progress(myid-1, ACCEPTED, 0);  
	      MPE_Update(handle);
#endif
	      for (i=0; i<columns; i++)
		  board[next_row][i+2] = CURR_GUESS[i];
	      next_row++;
	      next_guess(columns-1);
	      (curr_task->guess_number)++;
	      guesses_done++;
	      if ((--curr_task->guesses_remaining)<=0)
		  current_chunk_done(&done);
	      break;
	    case NEW_INFO:
#ifdef USE_GRAPHICS
	      draw_progress(myid-1, REJECTED,
			    board[next_row][columns+2]);
	      MPE_Update(handle);
#endif
#ifdef PRINTING
	      printf("%2d: NEW INFO, row num: %d\n", myid, next_row);
#endif
	      next_row++;
	      
	      break;
	    default:
	      fprintf(stderr,"slave %d received invalid type %d\n", 
		      myid, status.MPI_TAG);
	      done = 1;
	  }
      }
      else
      {
#ifdef DEBUG	  
	  trace_guess("inconsis: ", ", ");
	  printf("col_to_change = %d\n", col_to_change);
#endif
	  skipped = next_guess(col_to_change);
	  if ((curr_task->guesses_remaining-=skipped) > 0)
	  {
	      guesses_done+=skipped;
	      curr_task->guess_number+=skipped;
	  }
	  else
	  {
	      guesses_done+=(curr_task->guesses_remaining+skipped);
	      current_chunk_done(&done);
	  }
      }
      if (!done)
	  curr_task = curr_task->next;
    }
  
#ifdef PRINTING
  trace_guess("LAST:     ", "\n");
#endif
  
  
#ifdef USE_GRAPHICS
  draw_guess(myid-1, 1, CURR_GUESS, myid);
  draw_progress(myid-1, PROGRESS, 0);
  MPE_Update(handle);
#endif

  count = (int)(guesses_done/search_space_size*numslaves*100);

#ifdef LOGGING
  MPE_Log_event(3,0,"");
#endif
  MPI_Send(&count, FINISHED_LENGTH, MPI_INT, MASTER_RANK,
		   FINISHED, MPI_COMM_WORLD);
#ifdef LOGGING
  MPE_Log_event(4,0,"");
#endif
}

current_chunk_done(done)
int *done;
{
  TASKT *tmp;
    if (curr_task->next == curr_task)
    {
	/* run out of work */
	*done = 1;
    }
    else
    {
	curr_task->next->previous = curr_task->previous;
	curr_task->previous->next = curr_task->next;
	tmp = curr_task->previous;
	add_to_free_list(curr_task);
	curr_task = tmp;
    }
}

master()
{
  int row_num, source, worker, bulls, cows, done_pcnt, i, j;
  int numslaves = numprocs-1;
  int slaves_active;
  int game_over = 0;
  double starttime, endtime;
  MPI_Status status;
  GUESST tsk, last_guess, task_size, task_step;
  GUESST task_info[TASK_LENGTH];

#ifdef INTERACTIVE
  while (1)
  {

      while (1)
      {
	  printf("Number of colours: ");
	  fflush(stdout);
	  i = scanf("%d", &colours);
	  while(getchar() != 10);
	  if ( i > 0 && colours > 1 && colours <= MAXCOLOURS )
	      break;
	  printf("This should be a number between 2 and %d\n", MAXCOLOURS);
      }

      while (1)
      {
	  printf("Number of columns: ");
	  fflush(stdout);
	  i = scanf("%d", &columns);
	  while(getchar() != 10);
	  if (i > 0 && columns > 1 && columns <=MAXCOLS )
	      break;
	  else
	      printf("This should be a number between 2 and %d\n", MAXCOLS);
      }

      if (pow((double)colours, (double)columns) < MAX_GUESST)
	  break;
      else
	  printf("colours ^ columns too big (should be < %f)\n", MAX_GUESST);
  } 
  while (1)
    {
      printf("Number of tasks: ");
      fflush(stdout);
      i = scanf("%d", &numtasks);
      while(getchar() != 10);
      if (i > 0 && numtasks > 1 && numtasks <=MAXTASKS )
	break;
      else
	printf("This should be a number between 2 and %d\n", MAXTASKS);
    }
#endif

  slaves_active = numslaves;
  get_secret();
  initialize_mm(0);

  /* initial distribution of tasks */

  task_size = search_space_size/numtasks;
  if (task_size < MIN_TASK_SIZE)
      task_size = MIN_TASK_SIZE;
  task_step = numslaves*task_size;

#ifdef LOGGING
  MPE_Start_log();
#endif

  for (worker = 1; worker <= numslaves; worker++)
  {
    j=0;
    for (tsk = (worker-1)*task_size; tsk<search_space_size; tsk+=task_step)
      {
	initial_tasks[j] = tsk;
	if (tsk+task_size<=search_space_size)
	  initial_tasks[j+1] = task_size;
	else
	  initial_tasks[j+1] = search_space_size-tsk;
	j+=2;
      }

#ifdef LOGGING
  MPE_Log_event(3,0,"");
#endif
    MPI_Send(initial_tasks, j, MPI_GUESST, 
	     worker, TASK, MPI_COMM_WORLD);
#ifdef LOGGING
  MPE_Log_event(4,0,"");
#endif
  }
      
  starttime = MPI_Wtime();


  while(!game_over)
    {

#ifdef LOGGING
	MPE_Log_event(5,0,"");
#endif
	MPI_Recv(guess, MAX_MSG_LENGTH, MPI_INT, MPI_ANY_SOURCE,
		       MPI_ANY_TAG, MPI_COMM_WORLD, &status);
#ifdef LOGGING
	MPE_Log_event(6,0,"");
#endif
      source = status.MPI_SOURCE;
      switch (status.MPI_TAG)
	{
	case FINISHED:
	  printf("Slave %d finished at%8.3f, %2d%%(s%d)\n", 
		 source, MPI_Wtime()-starttime, guess[0], source);
	  slaves_active--;
	  break;
	case GUESS:
	  row_num = guess[columns];
	  done_pcnt = guess[columns+1];
	  if (row_num == next_row)
	    {
	      eval_guess(guess, secret, &bulls, &cows);

	      board[next_row][0] = bulls;
	      board[next_row][1] = cows;
	      for (i=0; i<columns; i++)
		board[next_row][i+2] = guess[i];

/*	      printf("%2d: %3d. ", source, next_row); */
	      printf("%3d. ", next_row+1); 
	      print_guess("", guess);
	      printf("(%2db %2dc)%8.3fs, %2d%%(s%d)\n", 
		     bulls, cows, MPI_Wtime()-starttime, 
		     done_pcnt, source);
	      if (bulls == columns) /* game over */
		{
		  for (i = 1; i <= numslaves; i++)
		    MPI_Send(NULL, EXIT_LENGTH, MPI_INT, i,
			     (i==source?WON:EXIT), MPI_COMM_WORLD);
		  game_over = 1;
		}
	      else
		{
		  for (i = 1; i <= numslaves; i++)
		  {
#ifdef LOGGING
		      MPE_Log_event(1,0,"");
#endif
		    if (i == source)
		      MPI_Send(&board[next_row][0], ACCEPTED_LENGTH, MPI_INT, 
			       source, ACCEPTED, MPI_COMM_WORLD);
		    else
		      {
			board[row_num][columns+2] = source;
			MPI_Send(&board[row_num][0], NEW_INFO_LENGTH, 
				 MPI_INT, i, NEW_INFO, MPI_COMM_WORLD);
		      }
#ifdef LOGGING
		      MPE_Log_event(2,0,"");
#endif
		  }
		}
#ifdef USE_GRAPHICS
	      sources[next_row] = source;
	      if (next_row < ROWS)
		{
		  draw_guess(next_row, 0, guess, source);
		  draw_score(next_row, bulls, cows);
		}
	      else /* scroll */
		for (i = next_row-ROWS+1, j = 0; i <=next_row; i++, j++)
		  {
		    draw_guess(j, 0, &board[i][2], sources[i]);
		    draw_score(j, board[i][0], board[i][1]);
		  }
	      MPE_Update(handle);
#endif
	      if (++next_row >= MAXGUESSES)
		{
		  printf("Mastermind board overflow, aborting\n");
		  for (i = 1; i <= numslaves; i++)
		    MPI_Send(NULL, EXIT_LENGTH, MPI_INT, i, EXIT, 
			     MPI_COMM_WORLD);
		  game_over = 1;
		}

	    }
	  break;
	default:
	  fprintf(stderr,"master received invalid type %d\n", 
		  status.MPI_TAG);
	}
    }
  endtime = MPI_Wtime();
  printf("MM for %2d slaves, %2d colours, %2d columns: %8.3fs, %2d guesses\n", 
	 numslaves, colours, columns, endtime - starttime, next_row);

  while(slaves_active)
    {
#ifdef LOGGING
	MPE_Log_event(3,0,"");
#endif
	MPI_Recv(guess, MAX_MSG_LENGTH, MPI_INT, MPI_ANY_SOURCE,
		       MPI_ANY_TAG, MPI_COMM_WORLD, &status);
#ifdef LOGGING
	MPE_Log_event(4,0,"");
#endif
      source = status.MPI_SOURCE;
      switch (status.MPI_TAG)
	{
	case FINISHED:
	  printf("Slave %d finished at%8.3f, %2d%%(s%d)\n", 
		 source, MPI_Wtime()-starttime, guess[0], source);
	  slaves_active--;
	  break;
	case GUESS:
	  break;
	default:
	  fprintf(stderr,"master received invalid type %d\n", 
		  status.MPI_TAG);
	}
    }

#ifdef USE_GRAPHICS
  /* just testing */
  MPE_Draw_string (handle, 15, 15, MPE_BLACK, "Hello, world!" );
  /*  */

  printf("Any key to exit\n");
  scanf("%c",&i);
#endif
}

GUESST next_guess(col)
     int col;
{
  int i;
  GUESST pos = 1, cnt = 0;
  for (i = columns-1; i>=col+1; i--)
    {
      cnt += CURR_GUESS[i]*pos;   
      CURR_GUESS[i] = 0;
      pos *= colours;
    }

  for (i = col; i>=0; i--)
    if (CURR_GUESS[i] < colours-1)
      {
	CURR_GUESS[i]++;
	break;
      }
    else /* CURR_GUESS[i] == colours-1 */
      CURR_GUESS[i] = 0;

  return pos - cnt;
}
	  

eval_guess(guess, code, bulls, cows)
     int guess[], code[];
     int *bulls, *cows;
{
  int i,j;
  int tmp[MAXCOLS];

  for (i=0; i<columns; i++)
    tmp[i] = guess[i];

  *bulls = 0;
  *cows = 0;
  for (i=0; i<columns; i++)
    if (guess[i] == code[i])
      (*bulls)++;
  for (i=0; i<columns; i++)
    for (j=0; j<columns; j++)
      if (code[i] == tmp[j])
	{
	  (*cows)++;
	  tmp[j] = -1; /* nonexistent colour */
	  break;
	}
  *cows -= *bulls;
}

/*
   col is initially set to 'columns', i.e. the column after the last one
   (columns being numbered from 0 to columns-1).

   When an inconsistency with a row of the board is found, col is set to
   the column where the inconsistency is detected.

   After an inconsistency has been found, we still keep checking the board,
   so that further rows of the board can cause col to move leftwards (I
   don't know if this pays off, perhaps worth profiling).

   If all rows tested and col still points to 'columns', than the guess is
   found to be consistent with the board.

   Otherwise the guess is inconsistent and col is returned as the leftmost
   col_to_change.

  */

guess_consistent(col_to_change)
     int *col_to_change;
{
  int bulls, bullscows, col, row, peg;
  int *board_row;
  int i,j;
  int tmp[MAXCOLS];

  col = columns;

  for (row = 0; row < next_row; row++)
    {
      board_row = &board[row][2];
      bulls = board[row][0];
      bullscows = board[row][1]+bulls;

      for (i=0; i<columns; i++)
	tmp[i] = board_row[i];

      for (i=0; i < col; i++)
	{
	  if (CURR_GUESS[i] == board_row[i]) /* bull */
	    {
	      if ((bulls--) <= 0)       /* too many bulls */
	      break;
	    }
	  j = 0;
	  peg = CURR_GUESS[i];
	  while ( j < columns && peg != tmp[j])
	    j++;
	  if (j < columns )             /* bull or cow */
	    if (bullscows-- <= 0)       /* too many bulls or cows */
	      break;
	    else
	      tmp[j] = NO_COLOUR;
	  if (bullscows >= columns-i)   /* too few bulls of cows */
	    break;
	}
      col = i;
    }

  if (col == columns)
    return 1;
  
  *col_to_change = col;
  return 0;      
}

print_guess(text, guess)
     char *text;
     int guess[];
{
  int i;

  printf("%s", text);
  for (i=0; i<columns; i++)
    {
      printf("%2d ", guess[i]);
    }
}

#ifdef USE_GRAPHICS
draw_guess(row, col, guess, id)
     int row, col, id;
     int guess[];
{
  int i;
  int hpos = left_col_width*col+HDIST+WORKER_HDIST;
  int vpos = (row+2)*VDIST;
  
  MPE_Fill_rectangle(handle, hpos-(HDIST-2*RADIUS+WORKER_WIDTH), 
		     (int)(vpos-WORKER_HEIGHT/2), WORKER_WIDTH, WORKER_HEIGHT,
		     WorkerColour(id));

  for (i=0; i<columns; i++)
    {
      MPE_Fill_circle( handle, hpos, vpos, RADIUS, PegColour(guess[i]) );
      hpos += HDIST;
    }
}

draw_score(row, bulls, cows)
     int row, bulls, cows;
{
  int r,c, i;
  int vpos = (row+2)*VDIST-RADIUS+SCORE_RADIUS;
  int hpos = left_col_width-HDIST-SCORE_WIDTH;

  for (r=0; r<SCORE_ROWS; r++)
    for (c=0; c<SCORE_COLS; c++)
      {
	i = SCORE_COLS*r+c;
	if (i < bulls)
	  MPE_Fill_circle( handle, hpos+SCORE_HDIST*c, 
			  vpos+SCORE_VDIST*r, SCORE_RADIUS, MPE_BLACK);
	else if (i < bulls+cows)
	  MPE_Draw_circle( handle, hpos+SCORE_HDIST*c, 
			  vpos+SCORE_VDIST*r, SCORE_RADIUS, MPE_BLACK);
	else
	  break;
      }
}

draw_progress(row, type, source)
     int row, type, source;
{
  int hpos = left_col_width+HDIST+WORKER_HDIST-RADIUS;
  int vpos = (row+2)*VDIST+2*RADIUS;
  int length;

  length = (int)((((double) guesses_done)/search_space_size)
		 * ((columns-1)*HDIST+2*RADIUS));

  MPE_Draw_line(handle, hpos, vpos, hpos+length, vpos, MPE_BLACK);

  switch (type)
    {
    case PROGRESS:
            break;
    case ACCEPTED:
	    MPE_Draw_line(handle, hpos+length, vpos, hpos+length, 
			  vpos-2*SUCCESS_HEIGHT, WorkerColour(myid));
	    break;
    case REJECTED:
	    MPE_Draw_line(handle, hpos+length, vpos, hpos+length, 
			  vpos+SUCCESS_HEIGHT, MPE_BLACK);
    case NEW_INFO:
	    MPE_Draw_line(handle, hpos+length, vpos, hpos+length, 
			  vpos-SUCCESS_HEIGHT, WorkerColour(source));
	    break;
    }
}

#endif

get_secret()
{
  int i;

  for (i=0; i<columns; i++)
    if (i<colours)
      secret[i] = colours-1-i;
    else
      secret[i] = 0;
}

GUESST int_power(n, m)
int n,m;
{
  int i;
  GUESST pw = 1;

  for (i=0; i<m; i++)
    pw*=n;

  return pw;
}

initialize_mm()
{
  int right_col_width, colourscale_width, i;
  
  MPI_Bcast(&colours, 1, MPI_INT, 0, MPI_COMM_WORLD);
  MPI_Bcast(&columns, 1, MPI_INT, 0, MPI_COMM_WORLD);
  
  search_space_size = int_power(colours, columns);

#ifdef USE_GRAPHICS
  left_col_width = WORKER_HDIST+(columns+2)*HDIST+SCORE_WIDTH;
  right_col_width = (columns+1)*HDIST;
  colourscale_width = (colours+2)*COLOURSCALE_HDIST;
  if (right_col_width < colourscale_width)
    right_col_width = colourscale_width;
  width = left_col_width+WORKER_HDIST+right_col_width;
  height = (ROWS+2)*VDIST - VDIST/2;
  
  MPE_Open_graphics( &handle, MPI_COMM_WORLD, (char*)0, 
		    -1, -1, width, height, MPE_GRAPH_INDEPDENT);

  if (myid > 0)
    return;

  for (i=0; i<columns; i++)
    {
      MPE_Fill_circle( handle, HDIST*(i+1)+WORKER_HDIST, 
		      (int)(0.6*VDIST), RADIUS, PegColour(secret[i]) );
    }
  for (i=0; i<colours; i++)
    {
      MPE_Fill_rectangle(handle, left_col_width+HDIST+WORKER_HDIST-RADIUS
			 +i*COLOURSCALE_HDIST, (int)(0.6*VDIST)-RADIUS, 
			 COLOURSCALE_WIDTH, 2*RADIUS,
			 PegColour(i));
    }
  MPE_Draw_line(handle, 0, (int)(1.3*VDIST), width, (int)(1.3*VDIST), 
		MPE_BLACK);
  MPE_Draw_line(handle, 0, (int)(1.4*VDIST), width, (int)(1.4*VDIST), 
		MPE_BLACK);
  MPE_Draw_line(handle, (int)(left_col_width-0.3*HDIST), 0, 
		(int)(left_col_width-0.3*HDIST), height,
		MPE_BLACK);
  MPE_Draw_line(handle, (int)(left_col_width-0.4*HDIST), 0, 
		(int)(left_col_width-0.4*HDIST), height,
		MPE_BLACK);
#endif
}

trace_guess(txt1, txt2)
char *txt1, *txt2;
{
  printf("%2d: ", myid);
  print_guess(txt1, CURR_GUESS);
  printf(", guesses_done = %d%s", guesses_done, txt2);
}

init_free_task_storage(used)
int used;
{
    int i;

    if (used<MAXTASKS)
    {
	for (i=used; i < MAXTASKS-1; i++)
	{
	    task_storage[i].next = &task_storage[i+1];
	    task_storage[i+1].previous = &task_storage[i];
	}
	
	task_storage[MAXTASKS-1].next = NULL;
	task_storage[used].previous = NULL;
	free_tasks = &task_storage[used];
    }
    else
	free_tasks = NULL;
}

add_to_free_list(task)
TASKT *task;
{
    task->next = free_tasks;
    task->previous = NULL;
    free_tasks->previous = task;
    free_tasks = task;
}

#include <stdio.h>
#include <string.h>
#include "mpi.h"
#define MPE_GRAPHICS
#include "mpe.h"

#define MAXBLOCKS    2000
#define SCREEN_WIDTH 600
#define SCREEN_HEIGHT 800
#define TABLE         (SCREEN_HEIGHT-100)

typedef struct _block
{
  int col;
  int level;
  int orig;
  int over;
  int final;
} block;

block blocks[MAXBLOCKS+1];
int   num_blocks;
int   plan_length;
int   blockdim;
int   col_sep;
int   current_column;
static FILE *tty;

MPE_XGraph handle;

main(argc,argv)
     int argc;
     char *argv[];
{
  int i, width, height;
  char junk1[100];
  char junk2[100];
  char menu();

  tty = fopen( "/dev/tty", "r" );
  read_states();
  fscanf(stdin,"%s %s %d", junk1, junk2, &plan_length);
/*
  printf("%s %s %d for %d blocks\n", junk1, junk2, plan_length, num_blocks);
  dump_state();
*/
  set_sizes();

  MPI_Init(&argc, &argv);

  width = SCREEN_WIDTH;
  height = SCREEN_HEIGHT;
  MPE_Open_graphics( &handle, MPI_COMM_WORLD, (char*)0, 
		    -1, -1, width, height, MPE_GRAPH_INDEPDENT);

  for (i = 0; i < 10; i = i+2 )
    MPE_Draw_line(handle, 50, TABLE+blockdim+i, width-50, TABLE+blockdim+i,
		  MPE_BLACK);	/* draw table */

  draw_state();
  for(;;) switch( menu() ) {
    case 's':                 /* show plan */
      process_moves();
/*      gets(junk1); gets(junk2); */
      break;
    case 'q':
      MPE_Close_graphics(&handle);
      MPI_Finalize();
      exit(0);
    case 'n':                 /* next problem */
      read_states();
      fscanf(stdin,"%s %s %d", junk1, junk2, &plan_length);
/*
      printf("%s %s %d for %d blocks\n",junk1,junk2, plan_length, num_blocks);
*/
      draw_state();
      break;
  }
}

char menu()
{
    char c;

    printf("s(how),  n(ext),  q(uit):  ");  fflush(stdout);
    do
	c = getc(tty);
    while (!strchr("snq",c));
    return c;
}

set_sizes()
{
  if (num_blocks < 21)
    {
      blockdim = 16;
      col_sep  = 32;
    }
  else if (num_blocks < 101)
    {
      blockdim = 6;
      col_sep  = 8;
    }
  else if (num_blocks < 1001)
    {
      blockdim = 4;
      col_sep  = 6;
    }
  else if (num_blocks < 2001)
    {
      blockdim = 2;
      col_sep  = 3;
    }
  else
    {
      blockdim = 1;
      col_sep  = 2;
    }
}
read_states()
{
  int i, n;

  fscanf(stdin,"%d",&num_blocks);
  for (i = 1; i <= num_blocks; i++)
      fscanf(stdin,"%d",&blocks[i].orig);
  for (i = 1; i <= num_blocks; i++)
      fscanf(stdin,"%d",&blocks[i].final);
}

set_pos(n)
int n;
{
    if (blocks[n].col == 0)	/* postion not yet set */
    {
	if (blocks[n].over == 0)	/* block is on table */
	{
	    blocks[n].level = 0;
	    blocks[n].col   = current_column++;
	}
	else
	{
	    set_pos(blocks[n].over); /* set position of block underneath */
	    blocks[n].level = blocks[blocks[n].over].level + 1;
	    blocks[n].col   = blocks[blocks[n].over].col;
	}
    }
}

dump_state()
{
  int i;

  for (i = 1; i <= num_blocks; i++)
    printf("block %d at level %d in col %d over block %d, destination %d\n", i,
	   blocks[i].level, blocks[i].col, blocks[i].over, blocks[i].final);
  printf("Plan length = %d\n", plan_length);
}

draw_state()
{
  int i;

  MPE_Fill_rectangle(handle,0,0,SCREEN_WIDTH,TABLE+blockdim,MPE_WHITE);

  current_column = 1;
  for (i = 1; i <= num_blocks; i++) {
      blocks[i].over = blocks[i].orig;
      blocks[i].col = 0;
  }
  for (i = 1; i <= num_blocks; i++)
      set_pos(i);

  for (i = 1; i <= num_blocks; i++)
    MPE_Fill_rectangle(handle,
		       50 + (blocks[i].col)*col_sep,
		       TABLE - (blocks[i].level)*blockdim,
		       blockdim, blockdim, MPE_BLACK);
  MPE_Update(handle);
}

process_moves()
{
  int i, from, to;
  char mvnum[16], theto[4];

  for (i = 1; i <= plan_length; i++)
    {
      fscanf(stdin,"%s %d %s %d", mvnum, &from, theto, &to);
      move_block(from, to);
    }
}

move_block(from, to)
int from, to;
{
/*
  printf("moving %d to %d\n", from, to);
*/

  MPE_Fill_rectangle(handle,
		     50 + (blocks[from].col)*col_sep,
		     TABLE - (blocks[from].level)*blockdim,
		     blockdim, blockdim, MPE_WHITE); /* erase block */
  
  if (to == 0)			/* target is table */
    {
	if (blocks[from].final == 0) {
	    MPE_Fill_rectangle(handle,
			       50 + (current_column)*col_sep,
			       TABLE,
			       blockdim, blockdim, MPE_GREEN);
	    blocks[from].over  = 0;
	    blocks[from].level = 0;
	    blocks[from].col   = current_column++;
	}
	else {
	    blocks[from].over  = 0;
	    blocks[from].level = 0;
	    blocks[from].col = -50;
	}
    }
  else				/* target is another block */
    {
	MPE_Color color;
	if (blocks[from].col < 0)
	    color = MPE_RED;
	else
	    color = MPE_GREEN;
      MPE_Fill_rectangle(handle,
			 50 + (blocks[to].col)*col_sep,
			 TABLE - (blocks[to].level + 1)*blockdim,
			 blockdim, blockdim, color);
      blocks[from].over  = to;
      blocks[from].level = blocks[to].level + 1;
      blocks[from].col   = blocks[to].col;
    }
  MPE_Update(handle);
  /* sleep(1); */
}

/* highly incomplete, and probably not worth doing....*/
move_block_slowly(from, to)
int from, to;
{
  double step;
  double incr = .01;
  int   xfrom, yfrom, xtarget, ytarget;

  printf("moving %d to %d\n", from, to);

  xfrom = 50 + (blocks[from].col)*col_sep;
  yfrom = TABLE - (blocks[from].level)*blockdim;

  
  if (to == 0)			/* target is table */
    {
      xtarget       = 50 + (current_column++)*col_sep;
      ytarget       = TABLE;

      for (step = incr; step <= 1.0; step += incr)
	{
	  MPE_Fill_rectangle(handle,
		     (int)((step-incr)*xtarget+(1.0-(step-incr))*xfrom),
		     (int)((step-incr)*ytarget+(1.0-(step-incr))*yfrom),
		     blockdim, blockdim, MPE_WHITE); /* erase block */
	  MPE_Fill_rectangle(handle,
		     (int) (step*xtarget + (1.0-step)*xfrom),
		     (int) (step*ytarget + (1.0-step)*yfrom),
		     blockdim, blockdim, MPE_BLACK);
	  MPE_Update(handle);
	}
      MPE_Fill_rectangle(handle, xtarget, ytarget,
			 blockdim, blockdim, MPE_BLACK);
      MPE_Update(handle);
      blocks[from].over  = 0;
      blocks[from].level = 0;
      blocks[from].col   = current_column - 1;
    }
  else				/* target is another block */
    {
      MPE_Fill_rectangle(handle,
			 50 + (blocks[to].col)*col_sep,
			 TABLE - (blocks[to].level + 1)*blockdim,
			 blockdim, blockdim, MPE_BLACK);
      blocks[from].over  = to;
      blocks[from].level = blocks[to].level + 1;
      blocks[from].col   = blocks[to].col;
    }
  MPE_Update(handle);
  /* sleep(1); */
}


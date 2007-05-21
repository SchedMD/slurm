#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <stdio.h>
#include <sys/time.h>
#include <string.h>
#include "mpi.h"
#include "mpe.h"
#include "pmandel.h"
#include "lists.h"

#include "pm_genproc.h"

#ifndef DEBUG
#define DEBUG 0
#endif

FILE *debug_file;

MPI_Datatype winspecs_type, flags_type, NUM_type, rect_type;

MPE_XGraph tracking_win;

void DrawImage           ( MPE_XGraph, Winspecs *, Flags * );
void ProcessArgsFromFile ( MPE_XGraph, Winspecs *, Flags *);
int DragZoom             ( MPE_XGraph, Flags *);
void copyFlags(Flags *to, Flags *from);
void free_flags_fnames(Flags *flags);

void DefineMPITypes();
void FreeMPITypes();

int click_tol; /* Tolerance, in pixels, for detecting a simple mouse-click
                  as opposed to a click and drag.
                  This can be set from the command line with the "-tol N"
                  option. Its default value is 2.
               */

void
UpdateDisplay(MPE_XGraph graph, MPE_XGraph tracking_win, Flags *flags)
{
  int myid;

  MPI_Comm_rank(MPI_COMM_WORLD, &myid);

  if (flags->no_remote_X) {
    if (myid==0) {
      MPE_Update( graph );
      if (flags->with_tracking_win) {
        MPE_Update( tracking_win );
      }
    }
  } else {
    MPE_Update( graph );
    if (flags->with_tracking_win) {
      MPE_Update( tracking_win );
    }
  }
}

int main( argc, argv )
int argc;
char **argv;
{
  int np, myid, myWindowOpened, allWindowsOpened, masterHalt;
  Winspecs winspecs;
  Flags flags;
  MPE_XGraph graph;
  int tracking_xpos, tracking_ypos; /* Position of the second X window. */
#if DEBUG
  char fileName[50];
#endif

#ifndef SGI_MPI
  if (IsArgPresent( &argc, argv, "-h" )) {
    PrintHelp( argv[0] );
  } 
#endif

  MPI_Init( &argc, &argv );
  MPI_Comm_rank( MPI_COMM_WORLD, &myid );
  MPI_Comm_size( MPI_COMM_WORLD, &np );

  if (myid==0) {
    click_tol = 2;
    GetIntArg(&argc, argv, "-tol", &click_tol);
  }

  if (np == 1) {
      fprintf( stderr, 
	      "pmandle requires at least 2 processes (master + slaves)\n" );
      MPI_Finalize();
      return 1;
      }

  if (!myid) {
    if (!getenv( "DISPLAY" )) {
      masterHalt=1;
      MPI_Bcast( &masterHalt, 1, MPI_INT, 0, MPI_COMM_WORLD );
      printf ("DISPLAY environment variable not set.  Exiting.\n\n");
      exit( -1 );
    }
    masterHalt=0;
    MPI_Bcast( &masterHalt, 1, MPI_INT, 0, MPI_COMM_WORLD );
  } else {
    MPI_Bcast( &masterHalt, 1, MPI_INT, 0, MPI_COMM_WORLD );
    if (masterHalt) {
      MPI_Finalize();
      exit( 0 );
    }
  }


  
#if DEBUG
  sprintf( fileName, "pm_debug_%d.out", myid );
  debug_file = fopen( fileName, "w" );
/*
    debug_file = 0;
*/
  if (!debug_file) {
    fprintf( stderr, "Could not open %s, using stderr.\n", fileName );
    debug_file = stderr;
  }
  fflush( debug_file );
#endif

  DefineMPITypes();
  GetDefaultWinspecs( &winspecs );
  GetDefaultFlags( &winspecs, &flags );
  GetWinspecs( &argc, argv, &winspecs );
  GetFlags( &argc, argv, &winspecs, &flags );

  if (flags.with_tracking_win) {
    if ( (winspecs.xpos == -1) && (winspecs.ypos == -1))   {
      /* Location of windows will be determined by the window manager. */
      tracking_xpos = -1;
      tracking_ypos = -1;
    } else {
      /* Place the windows side-by-side. */
      tracking_ypos = winspecs.ypos;
      if (winspecs.xpos >= 0) {
        tracking_xpos = winspecs.xpos + (winspecs.width + 20);
      } else {
        tracking_xpos = winspecs.xpos - (winspecs.width + 20);
      }
    }
  }

  tracking_win = (MPE_XGraph) 0;
  graph        = (MPE_XGraph) 0;
  myWindowOpened = 1;

  if (flags.no_remote_X) {

    if (myid==0) {

      /* Only rank 0 opens X display windows. */
      myWindowOpened = (MPE_Open_graphics(&graph, MPI_COMM_SELF, 0,
                                          winspecs.xpos, winspecs.ypos,
                                          winspecs.width, winspecs.height, 0)
                        == MPE_SUCCESS);
      if (myWindowOpened && flags.with_tracking_win) {
        myWindowOpened = myWindowOpened &&
                         (MPE_Open_graphics(&tracking_win, MPI_COMM_SELF, 0,
                                            tracking_xpos, tracking_ypos,
                                            winspecs.width, winspecs.height, 0)
                          == MPE_SUCCESS);
      }
    }

  } else {

      /* All ranks open the X display windows. */
      myWindowOpened = (MPE_Open_graphics(&graph, MPI_COMM_WORLD, (char *)0,
					  winspecs.xpos, winspecs.ypos,
					  winspecs.width, winspecs.height, 0)
		    == MPE_SUCCESS);
    if (myWindowOpened && flags.with_tracking_win) {
      myWindowOpened = myWindowOpened &&
                       (MPE_Open_graphics(&tracking_win, MPI_COMM_WORLD, 0,
	  			          tracking_xpos, tracking_ypos,
				          winspecs.width, winspecs.height, 0)
		      == MPE_SUCCESS);
    }
  }

#if DEBUG
  fprintf( debug_file, "[%d] connected? %d\n", myid, myWindowOpened );
  fflush( debug_file );
#endif

  MPI_Allreduce( &myWindowOpened, &allWindowsOpened, 1, MPI_INT, MPI_LAND,
		 MPI_COMM_WORLD );

  if (allWindowsOpened) {
    if (myid == 0) {
	/* Check for movie file flag */
	if (IsArgPresent( &argc, argv, "-movie" )) {
	    int freq = 1;
	    GetIntArg( &argc, argv, "-freq", &freq );
	    MPE_CaptureFile( graph, "mandel_out", freq );
	    }
	}

    if (!winspecs.bw) {
      winspecs.colorArray = (MPE_Color *) malloc( winspecs.numColors * 
						  sizeof( MPE_Color ) );
      if (flags.no_remote_X) {
        if (myid==0) {
          MPE_Make_color_array( graph, winspecs.numColors,
                                winspecs.colorArray );

          if (flags.with_tracking_win) {
            MPE_Make_color_array( tracking_win, winspecs.numColors,
                                  winspecs.colorArray );
          }
        }
        MPI_Bcast(winspecs.colorArray, winspecs.numColors, MPI_INT,
              0, MPI_COMM_WORLD );
      } else {

        MPE_Make_color_array( graph, winspecs.numColors,
                              winspecs.colorArray );
        if (flags.with_tracking_win) {
          MPE_Make_color_array( tracking_win, winspecs.numColors,
                                winspecs.colorArray );
        }
      }
    }

    /* 
     * DrawImage() contains the main program loop 
     */
    DrawImage( graph, &winspecs, &flags );
    if (!myid) {
      fprintf( stderr, "Press <Return> to close window\n" );
      while (getchar()!='\n')
	  ;
    }

    if (flags.no_remote_X) {
      if (myid==0) {
        MPE_Close_graphics( &graph );
        if (flags.with_tracking_win) {
          MPE_Close_graphics( &tracking_win );
        }
      }
    } else {
      MPE_Close_graphics( &graph );
      if (flags.with_tracking_win) {
        MPE_Close_graphics( &tracking_win );
      }
    }

    if (!winspecs.bw) {
      free(winspecs.colorArray);
    }
  } else {

    if (!myid) {
      if (flags.no_remote_X) {
        fprintf( stderr, "Rank 0 could not connect to the display. "
                 "Exiting.\n\n" );
      } else {
	  fprintf( stderr, "One or more processes could not connect\n" );
	  fprintf( stderr, "to the display.  Exiting.\n\n" );
      }
    }
    if ( graph != (MPE_XGraph) 0) {
	MPE_Close_graphics( &graph );
    }
    
    if ( tracking_win != (MPE_XGraph) 0) {
	MPE_Close_graphics( &tracking_win );
    }
  }
  FreeMPITypes();
  MPI_Finalize();

  return 0;
}

/* Read the points to use from a file */
void ProcessArgsFromFile( MPE_XGraph graph, Winspecs *winspecs, 
			  Flags *oldFlags ) 
{
  Flags newFlags;
  char line[1025], *copy, *tok, **argv;
  int doOneMore, ndrawn, myid, argc;
  xpand_list_Strings *argList;
  FILE *inf;
  int x1,y1,pressed,button;
  char copies[30][50];
  int  c;

  MPI_Comm_rank( MPI_COMM_WORLD, &myid );

#if DEBUG
  fprintf( stderr, "%d going into PAFF\n", myid );
#endif

  if (myid == 0) {
    doOneMore = 1;
    if (!oldFlags->inf || strcmp( oldFlags->inf, "-" ) == 0) {
      inf = stdin;
    } else {
      inf = fopen( oldFlags->inf, "r" );
      if (!inf) {
	fprintf( stderr, "Sorry, could not open %s, skipping.\n",
		oldFlags->inf );
	doOneMore = 0;
      }
    }
    MPI_Bcast( &doOneMore, 1, MPI_INT, 0, MPI_COMM_WORLD );

#if DEBUG
    fprintf( stderr, "%d opened input file\n", myid );
#endif

    ndrawn = 0;

    while( oldFlags->loop || fgets( line, 1024, inf )) {
      if (oldFlags->loop && !fgets( line, 1024, inf )) {
	rewind( inf );
	fgets( line, 1024, inf );
      }
      if (*line!='#' && StrContainsNonWhiteSpace( line )) {
	/* skip blank lines and lines starting with # */
	
	line[strlen( line ) - 1] = 0; /* chop off trailing '\n' */
	argList = Strings_CreateList(10);
	Strings_AddItem( argList, oldFlags->inf );
	tok = strtok( line, " \t" );
	c=0;
	while (tok) {
	  strcpy( copies[c], tok );
	  Strings_AddItem( argList, copies[c]);
          c++;
	  tok = strtok( (char *)0, " \t" );
	}
	copyFlags(&newFlags, oldFlags);
	newFlags.inf = (char *)0;
	newFlags.loop = 0;
	newFlags.zoom = 0;
	argc = ListSize( argList );
	argv = ListHeadPtr( argList );
	doOneMore = 1;
	MPI_Bcast( &doOneMore, 1, MPI_INT, 0, MPI_COMM_WORLD );
	GetFlags( &argc, argv, winspecs, &newFlags );
	newFlags.inf = oldFlags->inf;   /* We use inf to decided what to
					  do, so we must retain it */
	DrawImage( graph, winspecs, &newFlags );

        ListDestroy(argList);

        fprintf(stderr, "Press any mouse button to quit.\n" );
        MPE_Iget_mouse_press(graph, &x1, &y1, &button, &pressed );
        if (pressed) {
          doOneMore = 0;
	}
        MPI_Bcast(&doOneMore, 1, MPI_INT, 0, MPI_COMM_WORLD );
        if (!doOneMore) {
          break;
        }
      }
    }

  } else {
    /* For the slave processes */
    MPI_Bcast( &doOneMore, 1, MPI_INT, 0, MPI_COMM_WORLD );
    argc = 0;
    argv = 0;
    free_flags_fnames(oldFlags);
    while (doOneMore) {
#if DEBUG
	fprintf( stderr, "%d About to do one more loop\n", myid );
#endif
      copyFlags(&newFlags, oldFlags);
      GetFlags( &argc, argv, winspecs, &newFlags );
      DrawImage( graph, winspecs, &newFlags );
      MPI_Bcast( &doOneMore, 1, MPI_INT, 0, MPI_COMM_WORLD );
      free_flags_fnames(&newFlags);
    }
  }

#if DEBUG
  fprintf( stderr, "%d exiting PAFF\n", myid );
#endif
return;
}



void DrawImage( graph, winspecs, flags )
MPE_XGraph graph;
Winspecs *winspecs;
Flags *flags;
{
  int myid, i, drawAnother;

  MPI_Comm_rank( MPI_COMM_WORLD, &myid );

#if DEBUG
  fprintf( stderr, "%d entering DrawImage (%s)\n", myid, flags->inf );
#endif

  MPI_Barrier( MPI_COMM_WORLD );
  /* helpful when starting up debuggers */

  if (flags->inf) {
#if DEBUG
      fprintf( stderr, "%d about to process args from file\n", myid );
#endif
    ProcessArgsFromFile( graph, winspecs, flags );
  } else {
    
    drawAnother = 0;
    
    /* Here is the MAIN LOOP */
    do {
#if DEBUG
	fprintf( stderr, "%d in drawing loop\n", myid );
#endif
      MPE_INIT_LOG();
      if (myid == 0) {

        /* Clear the output display */
	MPE_Fill_rectangle( graph, 0, 0, winspecs->width, winspecs->height,
			   MPE_WHITE );

        if (flags->with_tracking_win) {
	  MPE_Fill_rectangle(tracking_win,0,0,winspecs->width, winspecs->height,
		  	     MPE_WHITE );
        }

	fprintf(stderr, "Drawing region "
                "-rmin %.17lf -imin %.17lf -rmax %.17lf -imax %.17lf  max. iters:%d\n",
		NUM2DBL( flags->rmin ), NUM2DBL( flags-> imin ),
		NUM2DBL( flags->rmax ), NUM2DBL( flags-> imax ), 
                flags->maxiter);

        UpdateDisplay(graph, tracking_win, flags);
	SeparateRect_Master( graph, winspecs, flags );
      } else {
	SeparateRect_Slave( graph, winspecs, flags );
      }

      UpdateDisplay(graph, tracking_win, flags);

      MPI_Barrier( MPI_COMM_WORLD );
#if LOG
      if (!myid && flags->logfile) {
	fprintf( stderr, "Writing logfile\n" );
      }
#endif
      MPE_FINISH_LOG( flags->logfile );
      if (flags->zoom) {
	drawAnother = DragZoom( graph, flags );
      } else {
	sleep( 3 );
	MPI_Barrier( MPI_COMM_WORLD );
      }
    } while (drawAnother);
    

    
  }
  return;
}



int DragZoom( graph, flags )
MPE_XGraph graph;
Flags *flags;
{
  int x1, y1, x2, y2, i, myid, button;
  NUM zx1, zy1, zx2, zy2;
  int okay = 1;
  
  MPI_Comm_rank( MPI_COMM_WORLD, &myid );

  if (!myid) {
    fprintf(stderr, "Ready for zoom rectangle (single mouse click to quit)\n" );
    MPE_Get_drag_region( graph, 1, MPE_DRAG_SQUARE, &x1, &y1, &x2, &y2 );
    if ( (abs(x1-x2)<=click_tol) && (abs(y1-y2)<=click_tol) ) {
      okay = 0; /* Quit if the user clicked without dragging (much). */
    } else {

	if (x1>x2) {i=x1; x1=x2; x2=i;}
	if (y1>y2) {i=y1; y1=y2; y2=i;}
	Pixel2Complex( flags, x1, y1, &zx1, &zy1 );
	Pixel2Complex( flags, x2, y2, &zx2, &zy2 );
	NUM_ASSIGN( flags->rmin, zx1 );
	NUM_ASSIGN( flags->imin, zy2 );
	NUM_ASSIGN( flags->rmax, zx2 );
	NUM_ASSIGN( flags->imax, zy1 );
	/*
	   fprintf( stderr, 
	   "Zooming in on (%d,%d - %d,%d) (%lf,%lf - %lf,%lf)\n",
	   x1, y1, x2, y2, flags->rmin, flags->imin,
	   flags->rmax, flags->imax );
       */
    }
  }

  MPI_Bcast( flags, 1, flags_type, 0, MPI_COMM_WORLD );
  MPI_Bcast( &okay, 1, MPI_INT, 0, MPI_COMM_WORLD );
  return okay;
}

void
copyFlags(Flags *to, Flags *from)
{
  to->logfile = from->logfile;
  to->inf = from->inf;
  to->outf = from->outf;
  to->winspecs = from->winspecs;
  to->breakout = from->breakout;
  to->randomize = from->randomize;
  to->colReduceFactor = from->colReduceFactor;
  to->loop = from->loop;
  to->zoom = from->zoom;
  to->askNeighbor = from->askNeighbor;
  to->sendMasterComplexity = from->sendMasterComplexity;
  to->drawBlockRegion = from->drawBlockRegion;
  to->fractal = from->fractal;
  to->maxiter = from->maxiter;
  to->with_tracking_win = from->with_tracking_win;
  to->no_remote_X = from->no_remote_X;
  to->boundary_sq = from->boundary_sq;
  to->epsilon = from->epsilon;
  to->rmin = from->rmin;
  to->rmax = from->rmax;
  to->imin = from->imin;
  to->imax = from->imax;
  to->julia_r = from->julia_r;
  to->julia_i = from->julia_i;
}


void
free_flags_fnames(Flags *flags)
{
  if (flags->logfile != 0) {
    free(flags->logfile);
    flags->logfile = 0;
  }
  if (flags->inf != 0) {
    free(flags->inf);
    flags->inf = 0;
  }
  if (flags->outf != 0) {
    free(flags->outf);
    flags->outf = 0;
  }
}

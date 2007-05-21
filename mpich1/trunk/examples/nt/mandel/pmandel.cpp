#include <afx.h>
//#include "PpmPgm.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "mpi.h"
#include "pmandel.h"
#include "lists.h"
#include "args.h"
#include "pm_genproc.h"

FILE *debug_file;

MPI_Datatype winspecs_type, flags_type, NUM_type, rect_type;

/* Forward refs */
int SeparateRect_Master  (MPE_XGraph graph, Winspecs *winspecs, Flags *flags);
void SeparateRect_Slave  (MPE_XGraph graph, Winspecs *winspecs, Flags *flags);
int DrawImage            (MPE_XGraph, Winspecs *, Flags *);
int ProcessArgsFromFile (MPE_XGraph, Winspecs *, Flags *);
int DragZoom             (MPE_XGraph, Flags *);

int main(int argc, char *argv[])
{
	int np, myid, myWindowOpened, allWindowsOpened;
	Winspecs winspecs;
	Flags flags;
	MPE_XGraph graph;

	try{
	if (IsArgPresent(&argc, argv, "-h")) 
	{
		PrintHelp(argv[0]);
	}
	if (IsArgPresent(&argc, argv, "-Stretch"))
		g_bNoStretch = false;

	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &myid);
	MPI_Comm_size(MPI_COMM_WORLD, &np);
	
	if (np == 1) 
	{
		fprintf(stderr, "\nmandle requires at least 2 processes (master + slaves)\n\n");
		PrintHelp(argv[0]);
		MPI_Finalize();
		return 1;
	}
	
	DefineMPITypes();
	GetDefaultWinspecs(&winspecs);
	GetDefaultFlags(&winspecs, &flags);
	GetWinspecs(&argc, argv, &winspecs);
	GetFlags(&argc, argv, &winspecs, &flags);
	
	myWindowOpened = (
		MPE_Open_graphics(&graph, MPI_COMM_WORLD, (char *)0,
		winspecs.xpos, winspecs.ypos,
		winspecs.width, winspecs.height, !myid)
			==0);
	
	MPI_Allreduce(&myWindowOpened, &allWindowsOpened, 1, MPI_INT, MPI_LAND,
		MPI_COMM_WORLD);
	
	if (allWindowsOpened) 
	{
		//printf("all windows opened\n");fflush(stdout);
		if (!winspecs.bw) 
		{
			winspecs.colorArray = (MPE_Color *) malloc(winspecs.numColors * sizeof(MPE_Color));
			MPE_Make_color_array(graph, winspecs.numColors, winspecs.colorArray);
		}
		//printf("calling DrawImage\n");fflush(stdout);
		int ret_val;
		ret_val = DrawImage(graph, &winspecs, &flags);
		//printf("back in main\n");fflush(stdout);
		MPI_Barrier(MPI_COMM_WORLD);
		//printf(".");fflush(stdout);
		if (!myid && (ret_val == FALSE))
		{
			MPI_Status status;
			printf("done\n");fflush(stdout);
			MPI_Recv(0, 0, MPI_INT, 0, WINDOW_CLOSED, MPI_COMM_WORLD, &status);
		}
		/*
		if (!myid)
		{
			PpmPgm *ppm = new PpmPgm(graph.width, graph.height);
			//printf("copying data to ppm\n");fflush(stdout);
			for (int i=0; i<graph.width; i++)
				for (int j=0; j<graph.height; j++)
					ppm->SetPixel(i, j, graph.map[i + j*graph.width]);
			printf("writing output to file\n");fflush(stdout);
			CFile fout;
			TCHAR filename[100];
			_stprintf(filename, TEXT("\\temp\\mandel%d.ppm"), myid);
			fout.Open(filename, CFile::modeCreate | CFile::modeReadWrite);
			ppm->Write(fout);
			fout.Close();
		}
		//*/
		//printf("closing graphics\n");fflush(stdout);
		MPE_Close_graphics(&graph);
	} 
	else 
	{
		if (!myid) 
		{
			fprintf(stderr, "One or more processes could not connect\n");
			fprintf(stderr, "to the display.  Exiting.\n\n");
		}
		if (myWindowOpened)
			MPE_Close_graphics(&graph);
	}
	//printf("Finalize\n");fflush(stdout);
	MPI_Finalize();
	}catch(...)
	{
		printf("Exception thrown, exiting...\n");
	}
	return 0;
}

int ProcessArgsFromFile(MPE_XGraph graph, Winspecs *winspecs, Flags *oldFlags)
{
	Flags newFlags;
	char line[1025], *copy, *tok, **argv;
	int doOneMore, ndrawn, myid, argc;
	xpand_list_Strings *argList;
	FILE *inf;
	int ret_val = FALSE;
	
	MPI_Comm_rank(MPI_COMM_WORLD, &myid);
	
	if (myid == 0) 
	{
		if (!oldFlags->inf || strcmp(oldFlags->inf, "-") == 0) 
		{
			inf = stdin;
		} 
		else 
		{
			inf = fopen(oldFlags->inf, "r");
			if (inf == NULL) 
			{
				fprintf(stderr, "Sorry, could not open %s, skipping.\n", oldFlags->inf);fflush(stderr);
				doOneMore = 0;
				MPI_Bcast(&doOneMore, 1, MPI_INT, 0, MPI_COMM_WORLD);
			}
		}
		
		ndrawn = 0;
		
		while (inf && (oldFlags->loop || fgets(line, 1024, inf)))
		{
			if (oldFlags->loop && !fgets(line, 1024, inf)) 
			{
				rewind(inf);
				fgets(line, 1024, inf);
			}
			if (*line!='#' && StrContainsNonWhiteSpace(line)) 
			{
				/* skip blank lines and lines starting with # */
				
				line[strlen(line) - 1] = 0; /* chop off trailing '\n' */
				argList = Strings_CreateList(10);
				Strings_AddItem(argList, oldFlags->inf);
				tok = strtok(line, " \t");
				while (tok) 
				{
					copy = (char *) malloc(sizeof(char) * strlen(tok) + 1);
					strcpy(copy, tok);
					Strings_AddItem(argList, copy);
					tok = strtok((char *)0, " \t");
				}
				newFlags = *oldFlags;
				newFlags.inf = (char *)0;
				newFlags.loop = 0;
				newFlags.zoom = 0;
				argc = ListSize(argList);
				argv = ListHeadPtr(argList);
				doOneMore = 1;
				//printf("-");fflush(stdout);
				MPI_Bcast(&doOneMore, 1, MPI_INT, 0, MPI_COMM_WORLD);
				GetFlags(&argc, argv, winspecs, &newFlags);
				if (DrawImage(graph, winspecs, &newFlags))
				{
					inf = NULL;
					ret_val =  TRUE;
					doOneMore = 0;
					//printf("-");fflush(stdout);
					MPI_Bcast(&doOneMore, 1, MPI_INT, 0, MPI_COMM_WORLD);
					//break;
					return TRUE;
				}
			}
		}
		doOneMore = 0;
		//printf("-");fflush(stdout);
		MPI_Bcast(&doOneMore, 1, MPI_INT, 0, MPI_COMM_WORLD);
		
	} 
	else 
	{
		//printf("-");fflush(stdout);
		MPI_Bcast(&doOneMore, 1, MPI_INT, 0, MPI_COMM_WORLD);
		argc = 0;
		argv = 0;
		while (doOneMore) 
		{
			newFlags = *oldFlags;
			GetFlags(&argc, argv, winspecs, &newFlags);
			if (DrawImage(graph, winspecs, &newFlags))
			{
				ret_val = TRUE;
			}
			//printf("-");fflush(stdout);
			MPI_Bcast(&doOneMore, 1, MPI_INT, 0, MPI_COMM_WORLD);
		}
	}
	
	return ret_val;
}

int DrawImage(MPE_XGraph graph, Winspecs *winspecs, Flags *flags)
{
	int myid, drawAnother;
	
	MPI_Comm_rank(MPI_COMM_WORLD, &myid);
	
	MPI_Barrier(MPI_COMM_WORLD);
	/* helpful when starting up debuggers */
	
	//printf("Entering DrawImage\n");fflush(stdout);
	if (flags->inf) 
	{
		//printf("input file selected\n");fflush(stdout);
		int ret_val = ProcessArgsFromFile(graph, winspecs, flags);
		//printf("ret_val: %d\n", ret_val);fflush(stdout);
		return ret_val;
	} 
	else 
	{
		drawAnother = 0;
		
		do 
		{
			MPE_INIT_LOG();
			if (myid == 0) 
			{
				MPE_Fill_rectangle(graph, 0, 0, winspecs->width, winspecs->height, MPE_BLACK); //MPE_WHITE);
				fprintf(stderr, "Drawing region -rmin %.17lf -imin %.17lf -rmax %.17lf -imax %.17lf\n",
					NUM2DBL(flags->rmin), NUM2DBL(flags-> imin),
					NUM2DBL(flags->rmax), NUM2DBL(flags-> imax));
				fflush(stderr);
				if (SeparateRect_Master(graph, winspecs, flags))
				{
					// Window closed
					MPI_Barrier(MPI_COMM_WORLD);
					return TRUE;
				}
			} 
			else 
			{
				SeparateRect_Slave(graph, winspecs, flags);
			}
			MPI_Barrier(MPI_COMM_WORLD);
			MPE_FINISH_LOG(flags->logfile);
			if (flags->zoom) 
			{
				//drawAnother = DragZoom(graph, flags);
				drawAnother = 0;
			} 
			else 
			{
				Sleep(3000);
				MPI_Barrier(MPI_COMM_WORLD);
			}
		} while (drawAnother);
	}
	return FALSE;
}


/*
int DragZoom(MPE_XGraph graph, Flags *flags)
{
	int x1, y1, x2, y2, i, myid;
	NUM zx1, zy1, zx2, zy2;
	
	MPI_Comm_rank(MPI_COMM_WORLD, &myid);
	
	if (!myid) 
	{
		printf("Ready for zoom rectangle");
		MPE_Get_drag_region(graph, 1, MPE_DRAG_SQUARE, &x1, &y1, &x2, &y2);
		if (x1>x2) 
		{
			i = x1; x1 = x2; x2 = i;
		}
		if (y1>y2) 
		{
			i = y1; y1 = y2; y2 = i;
		}
		Pixel2Complex(flags, x1, y1, &zx1, &zy1);
		Pixel2Complex(flags, x2, y2, &zx2, &zy2);
		NUM_ASSIGN(flags->rmin, zx1);
		NUM_ASSIGN(flags->imin, zy2);
		NUM_ASSIGN(flags->rmax, zx2);
		NUM_ASSIGN(flags->imax, zy1);
	}
	
	MPI_Bcast(flags, 1, flags_type, 0, MPI_COMM_WORLD);
	return 1;
}
//*/

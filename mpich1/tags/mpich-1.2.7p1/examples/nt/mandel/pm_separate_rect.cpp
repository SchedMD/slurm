#include <stdio.h>
#include <stdlib.h>
#include "pmandel.h"
#include "pm_genproc.h"
#include <time.h>

void SplitRect(Flags *flags, rect r);

int SeparateRect_Master(MPE_XGraph graph, Winspecs *winspecs, Flags *flags)
{
	bool bWindowClosed = false;
	rect *assigList, recvRectBuf[2], tempRect;
	/* assigList - list of what rectangles have been assigned to each process */
	/* recvRectBuf - when a slave process tells the master that some rectangles
	need to be calculated, the rectangle definitions are temporarily stored
	here */
	
	int inProgress, nidle, *idleList, splitPt, np,
		mesgTag, firstToEnqueue, procNum, i;
	/* inProgress - number of rectangles currently under construction */
	/* nidle - number of idle processes */
	/* idleList - list of process ranks that are idle */
	/* splitPt - point at which to split the Mandelbrot set--can't have the
	border algorithm enclosing the whole set */
	/* np - number of processes */
	/* data - one int returned by slave processes; different uses */
	/* mesgTag - the tag of the received message */
	/* firstToEnqueue - when receiving a bunch of rectangles to enqueue, the
	master process may send some off to idle processes right away before
	queueing them.  firstToEnqueue points to the first one that is actually
	queued */
	/* procNum - rank of the process that sent the last message */
	/* randomPt - index + 1 of the last item in the queue that has not been
	placed in random order */
	
	rect_queue rect_q;
	/* queue of rectangles to calculate */
	
	MPI_Status mesgStatus;
	
	MPI_Comm_size(MPI_COMM_WORLD, &np);

	srand(clock());
    /* initialize the random number generator for the -randomize option */
	
	MPE_DESCRIBE_STATE(S_COMPUTE, E_COMPUTE,
		"Compute", "blue:gray");
	MPE_DESCRIBE_STATE(S_DRAW_BLOCK, E_DRAW_BLOCK,
		"Draw block", "yellow:gray3");
	MPE_DESCRIBE_STATE(S_DRAW_RECT, E_DRAW_RECT,
		"Draw border", "green:light_gray");
	MPE_DESCRIBE_STATE(S_WAIT_FOR_MESSAGE, E_WAIT_FOR_MESSAGE,
		"Wait for message", "red:boxes");
	MPE_DESCRIBE_STATE(S_DRAW_CHUNK, E_DRAW_CHUNK, "Draw Chunk",
		"steelBlue:2x2");
 	
	assigList  = (rect *) malloc((np) * sizeof(rect));
	idleList   = (int *)  malloc((np) * sizeof(rect));
	
	nidle = inProgress = 0;
	Q_Create(&rect_q, flags->randomize);
    /* create the queue */
	
	if (flags->imin<0 && flags->imax>0) 
	{ /* might have to split the set */

		splitPt = winspecs->height +
			NUM2INT(NUM_DIV(NUM_MULT(flags->imin, INT2NUM(winspecs->height)),
			NUM_SUB(flags->imax, flags->imin)));
		RECT_ASSIGN(tempRect, 0, winspecs->width-1, 0, splitPt-1);
		tempRect.length = RectBorderLen(&tempRect);
		Q_Enqueue(&rect_q, &tempRect);
		RECT_ASSIGN(tempRect, 0, winspecs->width-1, splitPt, winspecs->height-1);
		tempRect.length = RectBorderLen(&tempRect);
		Q_Enqueue(&rect_q, &tempRect);
	} 
	else 
	{
		RECT_ASSIGN(tempRect, 0, winspecs->width-1, 0, winspecs->height-1);
		tempRect.length = RectBorderLen(&tempRect);
		Q_Enqueue(&rect_q, &tempRect);
	}
	
	int nSlavePoints = 0;
	MPE_Point *pSlavePoints = NULL;
	MPE_Point p;
	rect r;
	int wrkOut = 0;

	while (!bWindowClosed && (inProgress || !IS_Q_EMPTY(rect_q) || wrkOut))
	{ /* while someone's working, and the q is !empty */
		
		MPE_LOG_EVENT(S_WAIT_FOR_MESSAGE, 0, 0);
		MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &mesgStatus);
		MPE_LOG_RECEIVE(mesgStatus.MPI_SOURCE, mesgStatus.MPI_TAG, 0);
		MPE_LOG_EVENT(E_WAIT_FOR_MESSAGE, 0, 0);
		
		procNum = mesgStatus.MPI_SOURCE;
		mesgTag = mesgStatus.MPI_TAG;
		
		switch (mesgTag) 
		{
		case READY_TO_START:
			inProgress++;
		case READY_FOR_MORE:
			MPI_Recv(0, 0, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG,
				MPI_COMM_WORLD, &mesgStatus);
			if (IS_Q_EMPTY(rect_q)) 
			{		      /* if the queue is empty, */
				idleList[nidle++] = procNum;  /* remember this process was left idle */
				inProgress--;
			} 
			else 
			{
				Q_Dequeue(&rect_q, &tempRect);
				wrkOut++;
				MPI_Send(&tempRect, 1, rect_type,
					procNum, ASSIGNMENT, MPI_COMM_WORLD);
				MPE_LOG_SEND(procNum, ASSIGNMENT, sizeof(rect));
				/* otherwise, assign it the next in the queue */
				assigList[procNum] = tempRect;
				/* remember which rect this process is on */
			}
			break;
		case ADD2Q:
			/* slave is posting more rectangles to be queued */
			MPI_Recv(recvRectBuf, 2, rect_type, procNum,
				ADD2Q, MPI_COMM_WORLD, &mesgStatus);
			/* get rect definitions */
			firstToEnqueue = 0;
			while (nidle && firstToEnqueue < 2) 
			{
				/* if processes are idle, */	
				nidle--;
				assigList[idleList[nidle]] = recvRectBuf[firstToEnqueue];
				/* remember which rect this process is on */
				wrkOut++;
				MPI_Send(recvRectBuf + firstToEnqueue, 1, rect_type,
					idleList[nidle], ASSIGNMENT, MPI_COMM_WORLD);
				MPE_LOG_SEND(idleList[nidle], ASSIGNMENT, sizeof(rect));
				/* give them something to do */
				inProgress++; firstToEnqueue++;
			}
			for (; firstToEnqueue<2; firstToEnqueue++) 
			{
				Q_Enqueue(&rect_q, recvRectBuf + firstToEnqueue);
			}
			break;
		case SENDING_POINTS:
			wrkOut--;
			/* slave is sending the points that it computed */
			MPI_Recv(&nSlavePoints, 1, MPI_INT, procNum, SENDING_POINTS, MPI_COMM_WORLD, &mesgStatus);
			if (pSlavePoints)
				delete pSlavePoints;
			pSlavePoints = new MPE_Point[nSlavePoints];
			MPI_Recv(pSlavePoints, nSlavePoints * sizeof(MPE_Point), MPI_BYTE, procNum, SENDING_POINTS, MPI_COMM_WORLD, &mesgStatus);
			MPE_Draw_points(graph, pSlavePoints, nSlavePoints);
			MPE_Update(graph);
			break;
		case SENDING_RECTANGLE:
			wrkOut--;
			/* slave is sending a rectangle to fill with a solid color */
			//MPI_Recv(0, 0, MPI_INT, procNum, SENDING_RECTANGLE, MPI_COMM_WORLD, &mesgStatus);
			MPI_Recv(&r, sizeof(rect), MPI_BYTE, procNum, SENDING_RECTANGLE, MPI_COMM_WORLD, &mesgStatus);
			MPI_Recv(&p, sizeof(MPE_Point), MPI_BYTE, procNum, SENDING_RECTANGLE, MPI_COMM_WORLD, &mesgStatus);
			DrawBlock(graph, &p, &r);
			MPE_Update(graph);
			break;
		case WINDOW_CLOSED:
			MPI_Recv(0, 0, MPI_INT, procNum, WINDOW_CLOSED, MPI_COMM_WORLD, &mesgStatus);
			bWindowClosed = true;
			break;
		default:
			Sleep(0);
			/* Give up time slice to minimize effects of busy wait loop */
		}
	}

	for (i = 1; i<np; i++) 
	{							 /* tell everyone to exit */
		//printf("telling %d to quit\n", i);fflush(stdout);
		MPI_Send(rect_q.r, sizeof(rect), MPI_BYTE, i, ALL_DONE, MPI_COMM_WORLD);
		MPE_LOG_SEND(i, ALL_DONE, sizeof(rect));
		
		//*
		if (bWindowClosed)
		{
			//printf("probe called\n");fflush(stdout);
			MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &mesgStatus);
			//printf("probe returned\n");fflush(stdout);
			procNum = mesgStatus.MPI_SOURCE;
			mesgTag = mesgStatus.MPI_TAG;
			
			switch (mesgTag) 
			{
			case READY_FOR_MORE:
				//printf("ready for more received\n");fflush(stdout);
				MPI_Recv(0, 0, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG,
					MPI_COMM_WORLD, &mesgStatus);
				break;
			case ADD2Q:
				//printf("add2q received.\n");fflush(stdout);
				// slave is posting more rectangles to be queued 
				MPI_Recv(recvRectBuf, 2, rect_type, procNum,
					ADD2Q, MPI_COMM_WORLD, &mesgStatus);
				break;
			case SENDING_POINTS:
				//printf("sending points received\n");fflush(stdout);
				// slave is sending the points that it computed 
				MPI_Recv(&nSlavePoints, 1, MPI_INT, procNum, SENDING_POINTS, MPI_COMM_WORLD, &mesgStatus);
				if (pSlavePoints)
					delete pSlavePoints;
				pSlavePoints = new MPE_Point[nSlavePoints];
				MPI_Recv(pSlavePoints, nSlavePoints * sizeof(MPE_Point), MPI_BYTE, procNum, SENDING_POINTS, MPI_COMM_WORLD, &mesgStatus);
				MPE_Draw_points(graph, pSlavePoints, nSlavePoints);
				MPE_Update(graph);
				break;
			case SENDING_RECTANGLE:
				//printf("sending rectangle received.\n");fflush(stdout);
				// slave is sending a rectangle to fill with a solid color 
				//MPI_Recv(0, 0, MPI_INT, procNum, SENDING_RECTANGLE, MPI_COMM_WORLD, &mesgStatus);
				MPI_Recv(&r, sizeof(rect), MPI_BYTE, procNum, SENDING_RECTANGLE, MPI_COMM_WORLD, &mesgStatus);
				MPI_Recv(&p, sizeof(MPE_Point), MPI_BYTE, procNum, SENDING_RECTANGLE, MPI_COMM_WORLD, &mesgStatus);
				DrawBlock(graph, &p, &r);
				MPE_Update(graph);
				break;
			//default:
				//printf("mesgTag: %d\n", mesgTag);fflush(stdout);
			}
		}
		//*/
		
	}
	
	if (bWindowClosed)
		return TRUE;
	else
		return FALSE;
}

void SeparateRect_Slave(MPE_XGraph graph, Winspecs *winspecs, Flags *flags)
{
	int x, y, working, isContinuous, mesgTag, myid,
		dataSize, *iterData, npoints;
	/* x, y - integer counters for the point being calculated */
	/* working - whether this process is still working or has been retired */
	/* isContinuous - whether the border just computed is one continuous color */
	/* borderData - storage for the values calculated */
	/* datPtr - pointer into borderData */
	/* mesgTag - what type of message was just received */
	
	NUM rstep, istep;
	MPE_Point *pointData;
	
	rect r;
	/* r - the rectangle being calculated */
	
	MPI_Status mesgStatus;
	
	MPI_Comm_rank(MPI_COMM_WORLD, &myid);
	
	MPI_Send(0, 0, MPI_INT, MASTER_PROC, READY_TO_START, MPI_COMM_WORLD);
	MPE_LOG_SEND(MASTER_PROC, READY_TO_START, 0);
	
	working = 1;
	NUM_ASSIGN(rstep,  NUM_DIV(NUM_SUB(flags->rmax, flags->rmin),
		INT2NUM(winspecs->width-1)));
	NUM_ASSIGN(istep,  NUM_DIV(NUM_SUB(flags->imin, flags->imax),
		INT2NUM(winspecs->height-1)));
	
	
	/* figure out how much data might be stored and allocate space for it */
	x = flags->breakout * flags->breakout;
	y = 2 * (winspecs->height + winspecs->width);
	dataSize = ((y>x) ? y : x);
	iterData = (int *) malloc(dataSize * sizeof(int));
	pointData = (MPE_Point *) malloc(dataSize * sizeof(MPE_Point));
	
	Fract_SetRegion(flags->rmin, flags->rmax, flags->imin, flags->imax, 0,
		winspecs->width-1, 0, winspecs->height-1);
	
	switch (flags->fractal) 
	{
	case MBROT:
		Mbrot_Settings(flags->boundary_sq, flags->maxiter);
		break;
	case JULIA:
		Julia_Settings(flags->boundary_sq, flags->maxiter,
			flags->julia_r, flags->julia_i);
		break;
	case NEWTON:
		Mbrot_Settings(flags->boundary_sq, flags->maxiter);
		break;
	}
	
	while (working) 
	{
		MPE_LOG_EVENT(S_WAIT_FOR_MESSAGE, 0, 0);
		MPI_Recv(&r, 1, rect_type, MASTER_PROC, MPI_ANY_TAG,
			MPI_COMM_WORLD, &mesgStatus);
		MPE_LOG_RECEIVE(MASTER_PROC, mesgStatus.MPI_TAG, sizeof(rect));
		/* get command from master process */
		MPE_LOG_EVENT(E_WAIT_FOR_MESSAGE, 0, 0);
		mesgTag = mesgStatus.MPI_TAG;
		
		switch (mesgTag) 
		{
		case ASSIGNMENT:		/* new rectangle to compute */
			if (r.b-r.t<flags->breakout || r.r-r.l<flags->breakout) 
			{
				/* if smaller than breakout, compute directly */
				
				MPE_LOG_EVENT(S_COMPUTE, 0, 0);
				ComputeChunk(flags, &r, pointData, iterData, dataSize, &npoints);
				MPE_LOG_EVENT(E_COMPUTE, 0, 0);
				
				MPI_Send(0, 0, MPI_INT, MASTER_PROC, READY_FOR_MORE,
					MPI_COMM_WORLD);
				MPE_LOG_SEND(MASTER_PROC, READY_FOR_MORE, 0);
				
				MPE_LOG_EVENT(S_DRAW_CHUNK, 0, 0);
				//MPE_Draw_points(graph, pointData, npoints);
				MPI_Send(&npoints, 1, MPI_INT, MASTER_PROC, SENDING_POINTS, MPI_COMM_WORLD);
				MPI_Send(pointData, npoints * sizeof(MPE_Point), MPI_BYTE, MASTER_PROC, SENDING_POINTS, MPI_COMM_WORLD);
				//MPE_Update(graph);
				MPE_LOG_EVENT(E_DRAW_CHUNK, 0, 0);
			} 
			else 
			{			/* otherwise, compute the boundary */
				
				MPE_LOG_EVENT(S_COMPUTE, 0, 0);
				ComputeBorder(winspecs, flags, &r, pointData,
					dataSize, &npoints, &isContinuous);
				
				MPE_LOG_EVENT(E_COMPUTE, 0, 0);
				
				if (!isContinuous) 
				{
					SplitRect(flags, r);
				}
				MPI_Send(0, 0, MPI_INT, MASTER_PROC, READY_FOR_MORE,
					MPI_COMM_WORLD);
				MPE_LOG_SEND(MASTER_PROC, READY_FOR_MORE, 0);
				
				if (isContinuous) 
				{
					MPE_LOG_EVENT(S_DRAW_BLOCK, 0, 0);
					//DrawBlock(graph, pointData, &r);
					//MPI_Send(0, 0, MPI_INT, MASTER_PROC, SENDING_RECTANGLE, MPI_COMM_WORLD);
					MPI_Send(&r, sizeof(rect), MPI_BYTE, MASTER_PROC, SENDING_RECTANGLE, MPI_COMM_WORLD);
					MPI_Send(pointData, sizeof(MPE_Point), MPI_BYTE, MASTER_PROC, SENDING_RECTANGLE, MPI_COMM_WORLD);
					MPE_LOG_EVENT(E_DRAW_BLOCK, 0, 0);
				} 
				else 
				{
					MPE_LOG_EVENT(S_DRAW_RECT, 0, 0);
					//MPE_Draw_points(graph, pointData, npoints);
					MPI_Send(&npoints, 1, MPI_INT, MASTER_PROC, SENDING_POINTS, MPI_COMM_WORLD);
					MPI_Send(pointData, npoints * sizeof(MPE_Point), MPI_BYTE, MASTER_PROC, SENDING_POINTS, MPI_COMM_WORLD);
					//MPE_Update(graph);
					MPE_LOG_EVENT(E_DRAW_RECT, 0, 0);
				}
				
			}	/* else !breakout */
			break;			      /* end if case ASSIGNMENT: */
    case ALL_DONE:
		working = 0;
		break;
		
    } /* end of switch */
  } /* end of while (working) */
}

void SplitRect(Flags *flags, rect r)
{
	int xsplit, ysplit;
	rect rectBuf[2];
	
	xsplit = (r.r-r.l)>>1;
	ysplit = (r.b-r.t)>>1;
	if (xsplit>ysplit) 
	{		
		RECT_ASSIGN(rectBuf[0], r.l + 1, r.l + xsplit, r.t + 1, r.b-1);
		RECT_ASSIGN(rectBuf[1], r.l + xsplit + 1, r.r-1, r.t + 1, r.b-1);
	} 
	else 
	{
		RECT_ASSIGN(rectBuf[0], r.l + 1, r.r-1, r.t + 1, r.t + ysplit);
		RECT_ASSIGN(rectBuf[1], r.l + 1, r.r-1, r.t + ysplit + 1, r.b-1);
	}
	rectBuf[0].length = RectBorderLen(rectBuf);
	rectBuf[1].length = RectBorderLen(rectBuf + 1);
	MPI_Send(rectBuf, 2, rect_type, MASTER_PROC,
		ADD2Q, MPI_COMM_WORLD);
	MPE_LOG_SEND(MASTER_PROC, ADD2Q, sizeof(rect) * 2);
    // send the rectangles 
}

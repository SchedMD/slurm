/*****************************************************************************\
 *  Consume CPU and I/O resources.
 *
 *  Copyright (C) 2013 Bull S. A. S.
 *               Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois.
 *  Written by Rod Schultz <rod.schultz@bull.com>
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation version 2 of the License.
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *  See the GNU General Public License for more details.
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <math.h>

#define SECOND2RUN 180
#define BURNBUFSIZ 1024*1024
#define CPUBUSY_PCT 40
#define READLOOP 10
#define WRITELOOP 20
// WRITELOOP must be greater than READLOOP

int busyloop(int burn, int nxny, double *m1, double *m2, double *m1m2)
{
	int i, j, k;
	int iters = 0;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	int loopstart = tv.tv_usec;
	int curtime = tv.tv_usec;
	int busy = 0;
	int nxny_x, ioff, joff;

	while (busy < burn) {
		iters++;
		nxny_x = 0;
		for (i=0; i<nxny; i++) {
			ioff=i*nxny;
			for (j=0; j<nxny; j++) {
				joff=j*nxny;
				m1m2[nxny_x]=0;
				for (k=0; k<nxny; k++) {
					m1m2[nxny_x] += m1[ioff+k]*m2[k+joff];
				}
				nxny_x++;
			}
		}
		gettimeofday(&tv, NULL);
		curtime = tv.tv_usec;
		if (curtime < loopstart)
			curtime += 1000000; // second rollover
		busy = (curtime-loopstart);
	}
	return busy;
}

void do_io(char *burnpath, char *burnbuf, int iosize, int nread, int nwrite)
{
	int ix;
	FILE *fd = NULL;
	fd = fopen (burnpath, "w");
	if (!fd) {
		perror ("fopen for write");
	}
	for (ix=0; ix<nwrite; ix++) {
		if (!fwrite (burnbuf, iosize, 1, fd)) {
				perror ("fwrite");
		}
	}
	fclose(fd);
	fd = fopen (burnpath, "r");
	if (!fd) {
		perror ("fopen for read");
	}
	for (ix=0; ix<nread; ix++) {
		if (!fread (burnbuf, iosize, 1, fd)) {
			perror ("fread");
		}
	}
	fclose(fd);
}

int main (int argc, char **argv)
{
	int nxny = 100; // Dimension of matrices
	int writes = 0, reads = 0;
	int actualbusy = 0, desiredbusy = 0;
	int job_id, step_id, task_id;
	int dobusy, loopstart, curtime, ms_busy, nap, irx, i,j;
	double *m1, *m2, *m1m2;
	float realpctbusy;
	char *env_str, *burnbuf;
	char burnpath[64];
	struct timeval tv;
	if (READLOOP > WRITELOOP) {
		printf("\nFATAL: Compile parameter READLOOP > WRITELOOP");
		exit(1);
	}
	if ((env_str = getenv("SLURM_JOB_ID")) == NULL) {
		fprintf(stderr, "info: getenv(SLURM_JOB_ID) failed. "
				"(Not running Slurm?)\n");
	} else {
		job_id = atoi(env_str);
		if ((env_str = getenv("SLURM_STEPID")) != NULL) {
			step_id = atoi(env_str);
		}
		if ((env_str = getenv("SLURM_PROCID")) != NULL) {
			task_id = atoi(env_str);
		}
	}

	burnbuf = malloc(sizeof(char)*BURNBUFSIZ);
	memset(burnbuf,'\0',BURNBUFSIZ);
	sprintf(burnpath,"/tmp/ioburn_%d_%d_%d",job_id,step_id,task_id);
	ms_busy = CPUBUSY_PCT*10000;
	m1 = malloc(sizeof(double)*nxny*nxny);
	m2 = malloc(sizeof(double)*nxny*nxny);
	m1m2 = malloc(sizeof(double)*nxny*nxny);
	for (i=0; i<nxny; i++) {
		for (j=0;j<nxny;j++) {
			m1[i*nxny+j]= ((double) random());
			m2[i*nxny+j]= ((double) random());
		}
	}
	// 1 second load (%busy, io burn, sleep)
	for (irx=0; irx<SECOND2RUN; irx++) {
		gettimeofday(&tv, NULL);
		desiredbusy += ms_busy;
		loopstart = tv.tv_usec;
		dobusy = desiredbusy - actualbusy;
		if (dobusy > 0)
			actualbusy += busyloop(dobusy, nxny, m1, m2, m1m2);
		do_io(burnpath, burnbuf, BURNBUFSIZ, READLOOP, WRITELOOP);
		reads += READLOOP;
		writes += WRITELOOP;
		gettimeofday(&tv, NULL);
		curtime = tv.tv_usec;
		if (curtime < loopstart)
			curtime += 1000000;
		nap = 1000000 - (curtime - loopstart);
		usleep(nap);
	}
	realpctbusy = ((float) actualbusy)*100.0/((float)(SECOND2RUN*1000000));
	printf("\ntest12.6.prog finished after %d seconds. busy=%3.1f%% "
		"Reads=%d Writes=%d SLURM_JobId=%d StepId=%d TaskId=%d\n",
		SECOND2RUN,realpctbusy,reads, writes,job_id,step_id,task_id);
	free(m1);
	free(m2);
	free(m1m2);
	free(burnbuf);
	remove(burnpath);
	return 0;
}

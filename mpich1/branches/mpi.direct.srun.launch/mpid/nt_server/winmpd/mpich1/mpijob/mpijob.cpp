#include <stdio.h>
#include <stdlib.h>
#include "mpijob.h"
#include "GetOpt.h"

void PrintOptions()
{
    printf("Usage:\n");
    printf(" mpijob -jobs [jobhost]\n");
    printf(" mpijob jobid [-full] [jobhost]\n");
    printf(" mpijob -killjob jobid [jobhost]\n");
    printf(" mpijob -clear [all, before timestamp, or jobid] [jobhost]\n");
    printf(" mpdjob -tofile filename [all, before timestamp, or jobid] [jobhost]\n");
    printf("\n timestamp = yyyy.mm.dd<hh.mm.ss>\n");
    fflush(stdout);
}

void main(int argc, char *argv[])
{
    char host[MAX_HOST_LENGTH];
    char jobid[100];
    char option[100];
    char filename[1024];
    bool bFull;
    char *phost;

    easy_socket_init();

    if (argc == 1)
    {
	PrintOptions();
	return;
    }

    if (GetOpt(argc, argv, "-jobs", host))
    {
	ListJobs(host, MPD_DEFAULT_PORT, NULL);
    } else if (GetOpt(argc, argv, "-jobs"))
    {
	ListJobs(NULL, MPD_DEFAULT_PORT, NULL);
    }
    else if (GetOpt(argc, argv, "-killjob", jobid) || GetOpt(argc, argv, "-kill", jobid) || GetOpt(argc, argv, "-k", jobid))
    {
	if (argc > 1)
	    phost = argv[1];
	else
	    phost = NULL;
	KillJob(jobid, phost, MPD_DEFAULT_PORT, NULL);
    }
    else if (GetOpt(argc, argv, "-clear", option))
    {
	if (argc > 1)
	    phost = argv[1];
	else
	    phost = NULL;
	ClearJobs(option, phost, MPD_DEFAULT_PORT, NULL);
    }
    else if (GetOpt(argc, argv, "-tofile", filename))
    {
	if (argc < 2)
	{
	    printf("Error: all, timestamp or jobid must be specified after -tofile filename\n");
	    fflush(stdout);
	    return;
	}
	strcpy(option, argv[1]);
	if (argc > 2)
	    phost = argv[2];
	else
	    phost = NULL;
	JobsToFile(filename, option, phost, MPD_DEFAULT_PORT, NULL);
    }
    else
    {
	bFull = GetOpt(argc, argv, "-full");
	if (argc > 2)
	    phost = argv[2];
	else
	    phost = NULL;
	DisplayJob(argv[1], phost, MPD_DEFAULT_PORT, NULL, bFull, false, NULL);
    }

    easy_socket_finalize();
}

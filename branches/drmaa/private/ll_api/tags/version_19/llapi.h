/* IBM_PROLOG_BEGIN_TAG                                                   */
/* This is an automatically generated prolog.                             */
/*                                                                        */
/*                                                                        */
/*                                                                        */
/* Licensed Materials - Property of IBM                                   */
/*                                                                        */
/* (C) COPYRIGHT International Business Machines Corp. 1993,2005          */
/* All Rights Reserved                                                    */
/*                                                                        */
/* US Government Users Restricted Rights - Use, duplication or            */
/* disclosure restricted by GSA ADP Schedule Contract with IBM Corp.      */
/*                                                                        */
/* IBM_PROLOG_END_TAG                                                     */
/*
 ***********************************************************************
 *
 * IBM LoadLeveler Version 2.1.0
 *
 * 5765-145 (C) COPYRIGHT IBM CORP 1993, 1998.
 * 5765-227 (C) COPYRIGHT IBM CORP 1993, 1998.
 * 5765-228 (C) COPYRIGHT IBM CORP 1993, 1998.
 * 5765-287 (C) COPYRIGHT IBM CORP 1994, 1998.
 *
 * ALL RIGHTS RESERVED.
 *
 * US GOVERNMENT USERS RESTRICTED RIGHTS - USE, DUPLICATION
 * OR DISCLOSURE RESTRICTED BY GSA ADP SCHEDULE CONTRACT WITH
 * IBM CORP.
 *
 * LICENSED MATERIALS - PROPERTY OF IBM
 ***********************************************************************
 *@(#) file, component, release, file version, date changed
 * static char sccsid[] = "@(#) src/ll/h/llapi.h, includes, comm_rcs3c2, rcs3c20539a 1.1.2.205 9/22/05";
 */
#ifndef _llapi_h__
#define _llapi_h__

#include <stdio.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/resource.h>

#if !defined(__linux__)
#include <sys/checkpnt.h>
#endif /* __linux__ */

#if defined(__linux__)

#if !defined(LL_RUSAGE64)
#define LL_RUSAGE64
struct   rusage64 {
	struct timeval ru_utime;   /* user time used */
	struct timeval ru_stime;   /* system time used */
	long long   ru_maxrss;
	long long   ru_ixrss;   /* integral shared memory size */
	long long   ru_idrss;   /* integral unshared data */
	long long   ru_isrss;   /* integral unshared stack */
	long long   ru_minflt;  /* page reclaims */
	long long   ru_majflt;  /* page faults */
	long long   ru_nswap;   /* swaps */
	long long   ru_inblock; /* block input operations */
	long long   ru_oublock; /* block output operations */
	long long   ru_msgsnd;  /* messages sent */
	long long   ru_msgrcv;  /* messages received */
	long long   ru_nsignals;   /* signals received */
	long long   ru_nvcsw;   /* voluntary context switches */
	long long   ru_nivcsw;  /* involuntary */
};
#endif /* !LL_RUSAGE64 */

#if defined(LL_LINUX_CKPT)
#include "ll_linux_ckpt.h"
#endif /* LL_LINUX_CKPT */

#endif  /* __linux__ */

#if !defined(TRUE)
#define TRUE 1
#endif /* TRUE */

#if !defined(FALSE)
#define FALSE 0
#endif /* FALSE */

#ifndef MAXLEN_HOST
#define MAXLEN_HOST 256
#endif /* MAXLEN_HOST */

#define LL_API_VERSION 330	/* use to keep track of the version */
/* of the api code is compiled with */

typedef void LL_element;

enum LL_Daemon { LL_STARTD, LL_SCHEDD, LL_CM, LL_MASTER,
				 LL_STARTER, LL_HISTORY_FILE };
enum QueryType    { JOBS, MACHINES, PERF,
					CLUSTERS, WLMSTAT, MATRIX,
					CLASSES, RESERVATIONS, MCLUSTERS,
					BLUE_GENE};
enum QueryFlags { QUERY_ALL = (1<<0), QUERY_JOBID = (1<<1),
				  QUERY_STEPID = (1<<2), QUERY_USER = (1<<3),
				  QUERY_GROUP = (1<<4), QUERY_CLASS = (1<<5),
				  QUERY_HOST = (1<<6), QUERY_PERF = (1<<7),
				  QUERY_STARTDATE = (1<<8), QUERY_ENDDATE = (1<<9),
				  QUERY_PROCID = (1<<10),
				  QUERY_RESERVATION_ID = (1<<11),
				  QUERY_LOCAL = (1<<12),
				  QUERY_BG_JOB = (1<<13),
				  QUERY_BG_BASE_PARTITION = (1<<14),
				  QUERY_BG_PARTITION = (1<<15) };

enum DataFilter { ALL_DATA, STATUS_LINE, Q_LINE };

enum JobType { SET_BATCH, SET_INTERACTIVE };

enum JobStepType { BATCH_JOB, INTERACTIVE_JOB };

enum Usage { SHARED, NOT_SHARED, SLICE_NOT_SHARED };

enum CommLevel { LOW, AVERAGE, HIGH, COMMLVL_UNSPECIFIED };

enum EventType { ERROR_EVENT=-1, STATUS_EVENT, TIMER_EVENT };

enum HoldType  { NO_HOLD, HOLDTYPE_USER, HOLDTYPE_SYSTEM, HOLDTYPE_USERSYS };

enum StepState { STATE_IDLE, STATE_PENDING, STATE_STARTING, STATE_RUNNING,
				 STATE_COMPLETE_PENDING, STATE_REJECT_PENDING, STATE_REMOVE_PENDING,
				 STATE_VACATE_PENDING, STATE_COMPLETED, STATE_REJECTED, STATE_REMOVED,
				 STATE_VACATED, STATE_CANCELED, STATE_NOTRUN, STATE_TERMINATED,
				 STATE_UNEXPANDED, STATE_SUBMISSION_ERR, STATE_HOLD, STATE_DEFERRED,
				 STATE_NOTQUEUED, STATE_PREEMPTED, STATE_PREEMPT_PENDING,
				 STATE_RESUME_PENDING };

enum SessionType{ BATCH_SESSION, INTERACTIVE_SESSION,
				  INTERACTIVE_HOSTLIST_SESSION};

enum SpawnFlags { MARK_ALL_TASKS_RUNNING = (1<<0) };

/* Ranges are hardcoded for each object's enumerations. This will enable
   future updates to be grouped with the object and not change 
   compatibility. Please add any new enums to the end of the range for each
   object */
enum LLAPI_Specification {
	LL_JobManagementInteractiveClass=0, /* char **: LL interactive class */
	LL_JobManagementListenSocket,  /* int * : socket Schedd sends info on */
	LL_JobManagementAccountNo,     /* char ** : returns LOADL_ACCOUNT_NO */
	LL_JobManagementSessionType,   /* int * : session type info */
	LL_JobManagementPrinterFILE,   /* LL_element * : Default printer */
	LL_JobManagementRestorePrinter,/* LL_element * : restore previous printer */

	/* Job object data */
	LL_JobGetFirstStep=200, /* LL_element * (Step) : first step       */
	LL_JobGetNextStep,      /* LL_element * (Step) : next step        */
	LL_JobCredential,       /* LL_element * (Credential): credentials */
	LL_JobName,             /* char ** : job name                     */
	LL_JobStepCount,        /* int *   : number of steps in job       */
	LL_JobStepType,         /* int *   : INTERACTIVE_JOB or BATCH_JOB */
	LL_JobSubmitHost,       /* char ** : host job was submitted from  */
	LL_JobSubmitTime,       /* time_t  : time job was queued          */
	LL_JobVersionNum,       /* int *   : job version number           */
	LL_JobSchedd,           /* char ** : Schedd managing the job      */
	LL_JobJobQueueKey,      /* int *   : key used to write job to spool */
	LL_JobIsRemote,         /* int *   : is job remote - 1 true       */
	/* The following data is available for remote jobs. Local jobs - NULL */
	LL_JobSchedulingCluster,/* char ** : cluster scheduling remote job*/
	LL_JobSubmittingCluster,/* char ** : original cluster job was submitted on*/
	LL_JobSubmittingUser,   /* char ** : user name job was submitted under */
	LL_JobSendingCluster,   /* char ** : cluster which sent the remote job */
	LL_JobRequestedCluster, /* char ** : cluster_list JCF keyword value */
	LL_JobLocalOutboundSchedds, /* char *** : list of local outbound schedds, last schedd in list is current outbound */
	LL_JobScheddHistory,        /* char *** : list of scheduling schedds, last schedd in list is current */
	/*  End of remote job values */

	LL_JobGetFirstClusterInputFile,  /* LL_element *(ClusterFile): first input ClusterFile object */
	LL_JobGetNextClusterInputFile,   /* LL_element *(ClusterFile): next input ClusterFile object */
	LL_JobGetFirstClusterOutputFile, /* LL_element *(ClusterFile): first output ClusterFile object */
	LL_JobGetNextClusterOutputFile,  /* LL_element *(ClusterFile): next output ClusterFile file object */
	LL_JobUsersJCF, 		     /* char **: users JCF statements */

	/* Step object data */
	LL_StepNodeCount=400,          /* int * : number of nodes in step	*/
	LL_StepGetFirstNode,           /* LL_element * (Node) : first node	*/
	LL_StepGetNextNode,            /* LL_element * (Node) : next node	*/
	LL_StepMachineCount,           /* int * : number of machines assigned 	*/
	LL_StepGetFirstMachine,        /* LL_element * (LlMachine): first machine*/
	LL_StepGetNextMachine,         /* LL_element * (LlMachine): next machine */
	LL_StepGetFirstSwitchTable,    /* LL_element * (SwitchTable): 	*/
	LL_StepGetNextSwitchTable,     /* LL_element * (SwitchTable): 	*/
	LL_StepGetMasterTask,          /* LL_element * (Task): master task on step */
	LL_StepTaskInstanceCount,      /* int * : number of task instances	*/
	LL_StepAccountNumber,          /* char ** : associated account number   */
	LL_StepAdapterUsage,           /* int * :  requested adapter usaqe 	*/
	LL_StepComment,                /* char ** : user defined comment	*/
	LL_StepCompletionCode,         /* int * : exit status 			*/
	LL_StepCompletionDate,         /* time_t * : UNIX time step was completed */
	LL_StepEnvironment,            /* char ** : environment			*/
	LL_StepErrorFile,              /* char ** : name of error file 		*/
	LL_StepExecSize,               /* int * : size of the master executable */
	LL_StepHostName,               /* char * : name of host to be scheduled*/
	LL_StepID,                     /* char ** : scheddhostname.jobid.stepid */
	LL_StepInputFile,              /* char ** : name of input file 		*/
	LL_StepImageSize,              /* int * : size of the virtual image in K */
	LL_StepImmediate,              /* int * : immediate scheduling of job 	*/
	LL_StepIwd,                    /* char ** : initial directory 		*/
	LL_StepJobClass,               /* char ** : defined class for step 	*/
	LL_StepMessages,               /* char ** : string of messages from LL  */
	LL_StepName,                   /* char ** : name assigned to step 	*/
	LL_StepNodeUsage,              /* int * : requested node usage 	*/
	LL_StepOutputFile,             /* char ** : name of output file 	*/
	LL_StepParallelMode,           /* int * : mode of the step  		*/
	LL_StepPriority,               /* int * : User priority for step 	*/
	LL_StepShell,                  /* char ** : shell to be used 		*/
	LL_StepStartDate,              /* time_t * : date step was started 	*/
	LL_StepDispatchTime,           /* time_t * : time step was started 	*/
	LL_StepState,                  /* int * : current state of the step	*/
	LL_StepStartCount,             /* int * : times step has been started 	*/
	LL_StepCpuLimitHard,           /* int * : cpu hard limit		*/
	LL_StepCpuLimitSoft,           /* int * : cpu soft limit		*/
	LL_StepCpuStepLimitHard,       /* int * : cpu hard limit for entire job */
	LL_StepCpuStepLimitSoft,       /* int * : cpu soft limit for entire job */
	LL_StepCoreLimitHard,          /* int * : core file hard limit 	*/
	LL_StepCoreLimitSoft,          /* int * : core file soft limit		*/
	LL_StepDataLimitHard,          /* int * : data hard limit 		*/
	LL_StepDataLimitSoft,          /* int * : data soft limit 		*/
	LL_StepFileLimitHard,          /* int * : file size hard limit		*/
	LL_StepFileLimitSoft,          /* int * : file size soft limit		*/
	LL_StepRssLimitHard,           /* int * : resident set size hard limit */
	LL_StepRssLimitSoft,           /* int * : resident set size soft limit */
	LL_StepStackLimitHard,         /* int * : stack size hard limit	*/
	LL_StepStackLimitSoft,         /* int * : stack size soft limit	*/
	LL_StepWallClockLimitHard,     /* int * : wall clock hard limit 	*/
	LL_StepWallClockLimitSoft,     /* int * : wall clock soft limit 	*/
	LL_StepHostList,               /* char *** : hosts in host.list file */
	LL_StepHoldType,               /* int * : hold state of step           */
	LL_StepLoadLevelerGroup,       /* char ** : LL group specified by step  */
	LL_StepGetFirstAdapterReq,     /* LL_element*(AdapterReq):first AdapterReq*/
	LL_StepGetNextAdapterReq,      /* LL_element*(AdapterReq):next AdapterReq */
	LL_StepRestart,                /* int * : restartable job? */
	LL_StepBlocking,               /* int * : blocking factor requested */
	LL_StepTaskGeometry,           /* char ** : task geometry requested */
	LL_StepTotalTasksRequested,    /* int * : total tasks requested */
	LL_StepTasksPerNodeRequested,  /* int * : tasks per node requested */
	LL_StepTotalNodesRequested,    /* char ** : total nodes requested */
	LL_StepSystemPriority,         /* int * : system priority for step 	*/
	LL_StepClassSystemPriority,    /* int * : class system priority for step 	*/
	LL_StepGroupSystemPriority,    /* int * : group system priority for step 	*/
	LL_StepUserSystemPriority,     /* int * : user system priority for step 	*/
	LL_StepQueueSystemPriority,    /* int * : queue system priority for step 	*/
	LL_StepExecutionFactor,        /* int * : execution factor for a job step */
	LL_StepImageSize64,            /* int64_t * : size of the virtual image in K */
	LL_StepCpuLimitHard64,         /* int64_t * : cpu hard limit		*/
	LL_StepCpuLimitSoft64,         /* int64_t * : cpu soft limit		*/
	LL_StepCpuStepLimitHard64,     /* int64_t*: cpu hard limit for entire job */
	LL_StepCpuStepLimitSoft64,     /* int64_t*: cpu soft limit for entire job */
	LL_StepCoreLimitHard64,        /* int64_t * : core file hard limit 	*/
	LL_StepCoreLimitSoft64,        /* int64_t * : core file soft limit	*/
	LL_StepDataLimitHard64,        /* int64_t * : data hard limit 	*/
	LL_StepDataLimitSoft64,        /* int64_t * : data soft limit 	*/
	LL_StepFileLimitHard64,        /* int64_t * : file size hard limit	*/
	LL_StepFileLimitSoft64,        /* int64_t * : file size soft limit	*/
	LL_StepRssLimitHard64,         /* int64_t*: resident set size hard limit */
	LL_StepRssLimitSoft64,         /* int64_t*: resident set size soft limit */
	LL_StepStackLimitHard64,       /* int64_t * : stack size hard limit	*/
	LL_StepStackLimitSoft64,       /* int64_t * : stack size soft limit	*/
	LL_StepWallClockLimitHard64,   /* int64_t * : wall clock hard limit*/
	LL_StepWallClockLimitSoft64,   /* int64_t * : wall clock soft limit*/
	LL_StepStepUserTime64,         /* int64_t * : step user time - microseconds */
	LL_StepStepSystemTime64,       /* int64_t*: step system time - microseconds */
	LL_StepStepMaxrss64,           /* int64_t * : step ru_maxrss */
	LL_StepStepIxrss64,            /* int64_t * : step ru_ixrss */
	LL_StepStepIdrss64,            /* int64_t * : step ru_idrss */
	LL_StepStepIsrss64,            /* int64_t * : step ru_isrss */
	LL_StepStepMinflt64,           /* int64_t * : step ru_minflt */
	LL_StepStepMajflt64,           /* int64_t * : step ru_majflt */
	LL_StepStepNswap64,            /* int64_t * : step ru_nswap */
	LL_StepStepInblock64,          /* int64_t * : step ru_inblock */
	LL_StepStepOublock64,          /* int64_t * : step ru_oublock */
	LL_StepStepMsgsnd64,           /* int64_t * : step ru_msgsnd */
	LL_StepStepMsgrcv64,           /* int64_t * : step ru_msgrcv */
	LL_StepStepNsignals64,         /* int64_t * : step ru_nsignals */
	LL_StepStepNvcsw64,            /* int64_t * : step ru_nvcsw */
	LL_StepStepNivcsw64,           /* int64_t * : step ru_nivcsw */
	LL_StepStarterUserTime64,/* int64_t*: starter user time-microseconds */
	LL_StepStarterSystemTime64,/* int64_t*: starter system time-microseconds */
	LL_StepStarterMaxrss64,        /* int64_t * : starter ru_maxrss */
	LL_StepStarterIxrss64,         /* int64_t * : starter ru_ixrss */
	LL_StepStarterIdrss64,         /* int64_t * : starter ru_idrss */
	LL_StepStarterIsrss64,         /* int64_t * : starter ru_isrss */
	LL_StepStarterMinflt64,        /* int64_t * : starter ru_minflt */
	LL_StepStarterMajflt64,        /* int64_t * : starter ru_majflt */
	LL_StepStarterNswap64,         /* int64_t * : starter ru_nswap */
	LL_StepStarterInblock64,       /* int64_t * : starter ru_inblock */
	LL_StepStarterOublock64,       /* int64_t * : starter ru_oublock */
	LL_StepStarterMsgsnd64,        /* int64_t * : starter ru_msgsnd */
	LL_StepStarterMsgrcv64,        /* int64_t * : starter ru_msgrcv */
	LL_StepStarterNsignals64,      /* int64_t * : starter ru_nsignals */
	LL_StepStarterNvcsw64,         /* int64_t * : starter ru_nvcsw */
	LL_StepStarterNivcsw64,        /* int64_t * : starter ru_nivcsw */
	LL_StepMachUsageCount,         /* int * : count of machine usages */
	LL_StepGetFirstMachUsage,      /* LL_element * (MachineUsage): first machine usage */
	LL_StepGetNextMachUsage,       /* LL_element * (MachineUsage): next machine usage */
	LL_StepCheckpointable,         /* int * : 0-job not checkpointable */
	/*         1-job is checkpointable */
	LL_StepCheckpointing,          /* Boolean * : step is being checkpointed */
	LL_StepCkptAccumTime,          /* int * : accumulated ckpt time */
	LL_StepCkptFailStartTime,      /* time_t * : time of last failed ckpt*/
	LL_StepCkptFile,               /* char * : name of ckpt file */
	LL_StepCkptGoodElapseTime,     /* time_t * : time taken to complete last good ckpt */
	LL_StepCkptGoodStartTime,      /* time_t * : time of last good ckpt */
	LL_StepCkptTimeHardLimit,      /* int * : ckpt time hard limit */
	LL_StepCkptTimeHardLimit64,    /* int64_t * : ckpt time hard limit */
	LL_StepCkptTimeSoftLimit,      /* int * : ckpt time soft limit */
	LL_StepCkptTimeSoftLimit64,    /* int64_t * : ckpt time soft limit */
	LL_StepCkptRestart,            /* int * : 0|1 job restarted from ckpt*/
	LL_StepCkptRestartSameNodes,   /* int * : 0|1 restart job on same nodes */
	LL_StepWallClockUsed,          /* int * : wallclock already used by the step */
	LL_StepLargePage,              /* char ** : Large Page requirement (= "M", "Y", or "N") */
	LL_StepMaxProtocolInstances,   /* int * : largest number of instances allowed on network stmt */
	LL_StepBulkXfer,		/* int * : 0|1 Step Requests Bulk Transfer */
	LL_StepTotalRcxtBlocks,	/* int * : total number of RCXT blocks application needs */
	LL_StepStartTime,       /* time_t *: time the starter process for the job step started  */
	LL_StepUserRcxtBlocks,	/* int * : number of user RCXT blocks application requests */
	LL_StepRequestedReservationID, /* char ** : The step's requested reservation ID */
	LL_StepReservationID,          /* char ** : The step's reservation ID */
	LL_StepPreemptable,            /* int * : 0|1 step is preemptable */
	LL_StepPreemptWaitList,      /* char *** list of steps to preempted */
	LL_StepRsetName,								/* char ** rset name */
	LL_StepCkptExecuteDirectory,   /* char ** : Executable directory for a checkpointable step */
	LL_StepAcctKey,                /* int64_t * : accounting key for step */
	LL_StepDependency,             /* char ** : Step Dependency */
	LL_StepFavoredJob,             /* int * : whether the step is favored using llfavorjob */
	LL_StepBgJobId,                /* char ** : Blue Gene ID for the step */
	LL_StepBgState,                /* int * : Blue Gene state for the step */
	LL_StepBgSizeRequested,        /* int * : size requested for Blue Gene step */
	LL_StepBgSizeAllocated,        /* int * : size allocated for Blue Gene step */
	LL_StepBgShapeRequested,       /* int ** : shape requested for Blue Gene step */
	LL_StepBgShapeAllocated,       /* int ** : shape allocated for Blue Gene step */
	LL_StepBgConnectionRequested,  /* int * : type of wiring requested for Blue Gene step */
	LL_StepBgConnectionAllocated,  /* int * : type of wiring allocated for Blue Gene step */
	LL_StepBgPartitionRequested,   /* char ** : Blue Gene partition requested for step */
	LL_StepBgPartitionAllocated,   /* char ** : Blue Gene partition allocated for step */
	LL_StepBgPartitionState,       /* int * : state of Blue Gene partition allocated for step */
	LL_StepBgErrorText,            /* char ** : error text from Blue Gene system for step */

	/* Machine object data */
	LL_MachineAdapterList=800,   /* char *** : adapters defined	*/
	LL_MachineArchitecture,      /* char ** : machine architecture	*/
	LL_MachineAvailableClassList,/* char *** : classes defined */
	LL_MachineCPUs,              /* int * : number of cpus		*/
	LL_MachineDisk,              /* int * : avail space (KB) in execute dir */
	LL_MachineFeatureList,       /* char *** : features defined	*/
	LL_MachineConfiguredClassList,/* char ***: initiators defined */
	LL_MachineKbddIdle,          /* int * : seconds kbdd is idle  	*/
	LL_MachineLoadAverage,       /* double * : load average 		*/
	LL_MachineMachineMode,       /* char ** : configured machine mode 	*/
	LL_MachineMaxTasks,          /* int * : max number of tasks allowed 	*/
	LL_MachineName,              /* char * : official hostname 		*/
	LL_MachineOperatingSystem,   /* char ** : machine operating system */
	LL_MachinePoolList,          /* int ** : list of configured pools */
	LL_MachineRealMemory,        /* int * : physical memory 		*/
	LL_MachineScheddRunningJobs, /* int * : running jobs assigned schedd */
	LL_MachineScheddState,       /* int * : schedd state			*/
	LL_MachineScheddTotalJobs,   /* int * : total jobs assigned schedd */
	LL_MachineSpeed,             /* double * : speed associated with machine */
	LL_MachineStartdState,       /* char ** : startd state		*/
	LL_MachineStartdRunningJobs, /* int * : running jobs assigned startd */
	LL_MachineStepList,          /* char *** : stepids scheduled to run*/
	LL_MachineTimeStamp,         /* time_t *: time when machine data received */
	LL_MachineVirtualMemory,     /* int * : available swap space in kilobytes */
	LL_MachinePoolListSize,      /* int * : number of configured pools   */
	LL_MachineFreeRealMemory,    /* int * : free real memory in Mbytes */
	LL_MachinePagesScanned,      /* int * : pages scanned/sec by page replacement algorithm */
	LL_MachinePagesFreed,        /* int * : pages freed/sec by page replacement algorithm */
	LL_MachinePagesPagedIn,      /* int * : pages paged in from paging space */
	LL_MachinePagesPagedOut,     /* int * : pages paged out to paging space  */
	LL_MachineGetFirstResource,  /* LL_element * (Resource):first resource */
	LL_MachineGetNextResource,   /* LL_element * (Resource):next  resource */
	LL_MachineGetFirstAdapter,   /* LL_element * (Adapter): first adapter */
	LL_MachineGetNextAdapter,    /* LL_element * (Adapter): next adapter  */
	LL_MachineDrainingClassList, /* char *** : draining class list */
	LL_MachineDrainClassList,    /* char *** : drain class list */
	LL_MachineStartExpr,         /* char ** : START expression */
	LL_MachineSuspendExpr,       /* char ** : SUSPEND expression */
	LL_MachineContinueExpr,      /* char ** : CONTINUE expression */
	LL_MachineVacateExpr,        /* char ** : VACATE expression */
	LL_MachineKillExpr,          /* char ** : KILL expression */
	LL_MachineDisk64,            /* int64_t * : avail space (KB) in execute dir */
	LL_MachineRealMemory64,      /* int64_t * : physical memory 		*/
	LL_MachineVirtualMemory64,   /* int64_t * : available swap space in Kb */
	LL_MachineFreeRealMemory64,  /* int64_t * : free real memory in Mbytes */
	LL_MachinePagesScanned64,    /* int64_t * : pages scanned/sec by page replacement algorithm */
	LL_MachinePagesFreed64,      /* int64_t * : pages freed/sec by page replacement algorithm */
	LL_MachinePagesPagedIn64,    /* int64_t*:pages paged in from paging space*/
	LL_MachinePagesPagedOut64,   /* int64_t*:pages paged out to paging space*/
	LL_MachineLargePageSize64,   /* int64_t * : Size of Large Page memory block (bytes) */
	LL_MachineLargePageCount64,  /* int64_t * : Total number of Large Page memory blocks */
	LL_MachineLargePageFree64,   /* int64_t * : Number of Large Page memory blocks on freelist */
	LL_MachineReservationPermitted, /* int * : boolean, can this machine be reserved */
	LL_MachineReservationList,   /* char *** : list of IDs of reservations using this machine */
	LL_MachinePrestartedStarters,/* int * : Prestarted Starters to be started */
	LL_MachineCPUList,           /* int ** : list of cpus */
	LL_MachineUsedCPUs,          /* int *  : used cpus on this machine */
	LL_MachineUsedCPUList,       /* int ** : list of used cpus on this machine */
	LL_MachineGetFirstMCM,       /* LL_element * (MCM): first mcm */
	LL_MachineGetNextMCM,        /* LL_element * (MCM): next mcm  */
	LL_MachineConfigTimeStamp,   /* int * : Time of last reconfig */

	/* Node object data */
	LL_NodeTaskCount=1000, /* int * : number of task instances	*/
	LL_NodeGetFirstTask,   /* LL_element * (Task) : first task	*/
	LL_NodeGetNextTask,    /* LL_element * (Task) : next task 	*/
	LL_NodeMaxInstances,   /* int * : maximum # requested nodes    */
	LL_NodeMinInstances,   /* int * : minimum # requested nodes    */
	LL_NodeRequirements,   /* char ** : defined requirements 	*/
	LL_NodeInitiatorCount, /* int * : initiator count      	*/

	LL_SwitchTableJobKey=1200,/* int * : job key			*/

	/* Task object data */
	LL_TaskTaskInstanceCount=1400,      /* int * : number of task instances*/
	LL_TaskGetFirstTaskInstance,        /* LL_element * (TaskInstance)	*/
	LL_TaskGetNextTaskInstance,         /* LL_element * (TaskInstance)	*/
	LL_TaskExecutable,                  /* char ** : executable 		*/
	LL_TaskExecutableArguments,         /* char ** : executable arguments	*/
	LL_TaskIsMaster,                    /* int * : boolean, is this the master task */
	LL_TaskGetFirstResourceRequirement, /* LL_element * (ResourceReq) */
	LL_TaskGetNextResourceRequirement,  /* LL_element * (ResourceReq) */

	/* Task instance object data */
	LL_TaskInstanceAdapterCount=1600,    /* int * : number of adapters	*/
	LL_TaskInstanceGetFirstAdapter,      /* LL_element * (Adapter)	*/
	LL_TaskInstanceGetNextAdapter,       /* LL_element * (Adapter)	*/
	LL_TaskInstanceGetFirstAdapterUsage, /* LL_element * (AdapterUsage) */
	LL_TaskInstanceGetNextAdapterUsage,  /* LL_element * (AdapterUsage) */
	LL_TaskInstanceMachineName,          /*  char ** : machine assigned   */
	LL_TaskInstanceTaskID,               /*  int * : task id	 	*/
	LL_TaskInstanceMachineAddress,       /*  char * : machine IP address */
	LL_TaskInstanceMachine,              /*  LL_element * (LlMachine): machine Object */
	LL_TaskInstanceCpuList,              /* cpus used by task */

	/* Adapter object data */
	LL_AdapterInterfaceAddress=1800,/* char ** : interface IP address*/
	LL_AdapterMode,              /* char ** : Use LL_AdapterUsageMode		*/
	LL_AdapterName,              /* char ** : Adapter name		*/
	LL_AdapterUsageWindow,       /* int * : window assigned to task 	*/
	LL_AdapterUsageProtocol,     /* char ** : Protocol used by task	*/
	LL_AdapterCommInterface=1806,     /* int * : communication interface	*/
	LL_AdapterUsageMode,         /* char ** : Used for css IP or US	*/
	LL_AdapterTotalWindowCount=1811,  /* int * : # of windows on adapter  */
	LL_AdapterAvailWindowCount,  /* int * : # of windows not in use  */
	LL_AdapterUsageAddress,      /* char ** : IP Address to use adapter  */
	LL_AdapterUsageCommunicationInterface, /* int * : comm interface  */
	LL_AdapterUsageDevice,       /* char ** : Name of adapter device being used */
	LL_AdapterUsageInstanceNumber,/* int * :Unique ID for multiple instances */
	LL_AdapterUsageNetworkId,	/* int *: Network ID of adapter being used */
	LL_AdapterWindowList,        /* int ** : Array of window numbers */
	LL_AdapterUsageWindowMemory64,/* uint64_t * : bytes used by window    */
	LL_AdapterMinWindowSize64,   /* uint64_t * : min allocatable window memory */
	LL_AdapterMaxWindowSize64,   /* uint64_t * : max allocatable window memory */
	LL_AdapterMemory64,          /* uint64_t * : Total adapter memory		*/
	LL_AdapterUsageTag,		/* char** : Tag that identifies switch table for usage */
	LL_AdapterMCMId,						 /* int * : mcm this adpter connected to */
	LL_AdapterUsageRcxtBlocks,   /* int * : number of rCxt blocks used by window */
	LL_AdapterRcxtBlocks,         /* int * : number of rCxt blocks available on adapter */

	/* Credential object data */
	LL_CredentialGid=2000,  /* int * : unix group id of submitter 	*/
	LL_CredentialGroupName, /* char ** : User group for job		*/
	LL_CredentialUid,       /* int * : unix userid of submitter 	*/
	LL_CredentialUserName,  /* char ** : login of person submitting job */

	LL_StartdPerfJobsRunning=2200,  /* All of the StartdPerf are of */
	LL_StartdPerfJobsPending,       /* int * data type		*/
	LL_StartdPerfJobsSuspended,
	LL_StartdPerfCurrentJobs,
	LL_StartdPerfTotalJobsReceived,
	LL_StartdPerfTotalJobsCompleted,
	LL_StartdPerfTotalJobsRemoved,
	LL_StartdPerfTotalJobsVacated,
	LL_StartdPerfTotalJobsRejected,
	LL_StartdPerfTotalJobsSuspended,
	LL_StartdPerfTotalConnections,
	LL_StartdPerfFailedConnections,
	LL_StartdPerfTotalOutTransactions,
	LL_StartdPerfFailedOutTransactions,
	LL_StartdPerfTotalInTransactions,
	LL_StartdPerfFailedInTransactions,

	LL_ScheddPerfJobsIdle=2400,	/* All of the ScheddPerf are of */
	LL_ScheddPerfJobsPending,	/* int * data type 		*/
	LL_ScheddPerfJobsStarting,
	LL_ScheddPerfJobsRunning,
	LL_ScheddPerfCurrentJobs,
	LL_ScheddPerfTotalJobsSubmitted,
	LL_ScheddPerfTotalJobsCompleted,
	LL_ScheddPerfTotalJobsRemoved,
	LL_ScheddPerfTotalJobsVacated,
	LL_ScheddPerfTotalJobsRejected,
	LL_ScheddPerfTotalConnections,
	LL_ScheddPerfFailedConnections,
	LL_ScheddPerfTotalOutTransactions,
	LL_ScheddPerfFailedOutTransactions,
	LL_ScheddPerfTotalInTransactions,
	LL_ScheddPerfFailedInTransactions,

	LL_VersionCheck=2600,         /* used by POE for release checking */

	/* AdapterReq object data */
	LL_AdapterReqCommLevel=2700,  /* int * : communication level      */
	LL_AdapterReqUsage,           /* int * : requested adapter usage */
	LL_AdapterReqInstances,       /* int * : requested number of instances for protocol */
	LL_AdapterReqRcxtBlks,        /* int * : requested number of user rCxt Blocks for protocol */
	LL_AdapterReqProtocol,        /* char ** : requested adapter protocol */
	LL_AdapterReqMode,            /* char ** : requested adapter mode */
	LL_AdapterReqTypeName,        /* char ** : requested adapter type */

	/* Cluster object data */
	LL_ClusterGetFirstResource=2800,  /* LL_element * (Resource): first */
	LL_ClusterGetNextResource,        /* LL_element * (Resource): next  */
	LL_ClusterSchedulingResources,    /* char ***:scheduling resources*/
	LL_ClusterDefinedResources,       /* char *** : resources defined   */
	LL_ClusterSchedulingResourceCount,/* int *: # of scheduling resources*/
	LL_ClusterDefinedResourceCount,   /* int *: number of defined resources*/
	LL_ClusterEnforcedResources,      /* char ***: enforced resources */
	LL_ClusterEnforcedResourceCount,  /* int * : # of enforced resources   */
	LL_ClusterEnforceSubmission,      /* int * : Boolean, are resources required at submission time    */
	LL_ClusterSchedulerType,          /* char **: scheduler type     */
	LL_ClusterPreemptionEnabled,      /* int * : Boolean, is the preemption function enabled */
	LL_ClusterSysPrioThreshold,       /* int *: value of SYSPRIO_THRESHOLD_TO_IGNORE_STEP */
	LL_ClusterMusterEnvironment,      /* int *: 1-Muster Environment Enabled */
	LL_ClusterClusterMetric,          /* char**: CLUSTER_METRIC string */
	LL_ClusterClusterUserMapper,      /* char**: CLUSTER_USER_MAPPER string */
	LL_ClusterClusterRemoteJobFilter, /* char**: CLUSTER_REMOTE_JOB_FILTER string */
	LL_ClusterEnforceMemory,          /* int * : Boolean, is the Absolute Memory Limit enabled */

	/* Resource object data */
	LL_ResourceName=2900,            /* char ** : Resource Name   */
	LL_ResourceInitialValue,         /* int * : # of initial resources  */
	LL_ResourceAvailableValue,       /* int * : # of available resources*/
	LL_ResourceInitialValue64,       /* int64_t * # of initial resources   */
	LL_ResourceAvailableValue64,     /* int64_t * # of available resources */

	/* ResourceReq object data */
	LL_ResourceRequirementName=3000, /* char **:job's resource requirement*/
	LL_ResourceRequirementValue,     /* int  *:# of resources requested  */
	LL_ResourceRequirementValue64,   /* int64_t*: # of resources requested */

	/* WlmStat object data */
	LL_WlmStatCpuTotalUsage=3100, /* int64_t* :WLM reported total cpu usage*/
	LL_WlmStatCpuSnapshotUsage,   /* int*     :WLM snapshot cpu usage      */
	LL_WlmStatMemoryHighWater,    /* int64_t* :WLM real memory high water  */
	LL_WlmStatMemorySnapshotUsage,/* int :WLM real memory snapshot usage*/

	/* Matrix object data */
	LL_MatrixTimeSlice = 3200,     /* int* : GANG_MATRIX_TIME_SLICE value */
	LL_MatrixColumnCount,          /* int* : # of GANG matrix columns     */
	LL_MatrixRowCount,             /* int* : # of GANG matrix rows        */
	LL_MatrixGetFirstColumn,       /* LL_element* : first column          */
	LL_MatrixGetNextColumn,        /* LL_element* : next  column          */

	/* Column object data */
	LL_ColumnMachineName = 3300,   /* char** : machine name               */
	LL_ColumnProcessorNumber,      /* int* : processor number             */
	LL_ColumnRowCount,             /* int* : # of rows in this column     */
	LL_ColumnStepNames,            /* char*** : step names                */

	/* MachineUsage object data */
	LL_MachUsageMachineName = 3400,   /* char **  */
	LL_MachUsageMachineSpeed,         /* double *  */
	LL_MachUsageDispUsageCount,       /* int    *  */
	LL_MachUsageGetFirstDispUsage,    /* LL_element * (DispUsage): first dispatch usage */
	LL_MachUsageGetNextDispUsage,     /* LL_element * (DispUsage): next  dispatch usage */

	/* DispatchUsage object data */
	LL_DispUsageEventUsageCount=3500, /* int  *  */
	LL_DispUsageGetFirstEventUsage,   /* LL_element * (EventUsage): first event usage */
	LL_DispUsageGetNextEventUsage,    /* LL_element * (EventUsage): next  event usage */
	LL_DispUsageStepUserTime64,       /* int64_t * : dispatch usage step user time - microseconds */
	LL_DispUsageStepSystemTime64,     /* int64_t * : dispatch usage step system time - microseconds */
	LL_DispUsageStepMaxrss64,         /* int64_t * : dispatch usage step ru_maxrss */
	LL_DispUsageStepIxrss64,          /* int64_t * : dispatch usage step ru_ixrss */
	LL_DispUsageStepIdrss64,          /* int64_t * : dispatch usage step ru_idrss */
	LL_DispUsageStepIsrss64,          /* int64_t * : dispatch usage step ru_isrss */
	LL_DispUsageStepMinflt64,         /* int64_t * : dispatch usage step ru_minflt */
	LL_DispUsageStepMajflt64,         /* int64_t * : dispatch usage step ru_majflt */
	LL_DispUsageStepNswap64,          /* int64_t * : dispatch usage step ru_nswap */
	LL_DispUsageStepInblock64,        /* int64_t * : dispatch usage step ru_inblock */
	LL_DispUsageStepOublock64,        /* int64_t * : dispatch usage step ru_oublock */
	LL_DispUsageStepMsgsnd64,         /* int64_t * : dispatch usage step ru_msgsnd */
	LL_DispUsageStepMsgrcv64,         /* int64_t * : dispatch usage step ru_msgrcv */
	LL_DispUsageStepNsignals64,       /* int64_t * : dispatch usage step ru_nsignals */
	LL_DispUsageStepNvcsw64,          /* int64_t * : dispatch usage step ru_nvcsw */
	LL_DispUsageStepNivcsw64,         /* int64_t * : dispatch usage step ru_nivcsw */
	LL_DispUsageStarterUserTime64,    /* int64_t * : dispatch usage starter user time - microseconds */
	LL_DispUsageStarterSystemTime64,  /* int64_t * : dispatch usage starter system time - microseconds */
	LL_DispUsageStarterMaxrss64,      /* int64_t * : dispatch usage starter ru_maxrss */
	LL_DispUsageStarterIxrss64,       /* int64_t * : dispatch usage starter ru_ixrss */
	LL_DispUsageStarterIdrss64,       /* int64_t * : dispatch usage starter ru_idrss */
	LL_DispUsageStarterIsrss64,       /* int64_t * : dispatch usage starter ru_isrss */
	LL_DispUsageStarterMinflt64,      /* int64_t * : dispatch usage starter ru_minflt */
	LL_DispUsageStarterMajflt64,      /* int64_t * : dispatch usage starter ru_majflt */
	LL_DispUsageStarterNswap64,       /* int64_t * : dispatch usage starter ru_nswap */
	LL_DispUsageStarterInblock64,     /* int64_t * : dispatch usage starter ru_inblock */
	LL_DispUsageStarterOublock64,     /* int64_t * : dispatch usage starter ru_oublock */
	LL_DispUsageStarterMsgsnd64,      /* int64_t * : dispatch usage starter ru_msgsnd */
	LL_DispUsageStarterMsgrcv64,      /* int64_t * : dispatch usage starter ru_msgrcv */
	LL_DispUsageStarterNsignals64,    /* int64_t * : dispatch usage starter ru_nsignals */
	LL_DispUsageStarterNvcsw64,       /* int64_t * : dispatch usage starter ru_nvcsw */
	LL_DispUsageStarterNivcsw64,      /* int64_t * : dispatch usage starter ru_nivcsw */

	/* EventUsage object data */
	LL_EventUsageEventID = 3600,      /* int   *  */
	LL_EventUsageEventName,           /* char  **  */
	LL_EventUsageEventTimestamp,      /* int   *  */
	LL_EventUsageStepUserTime64,      /* int64_t * : event usage step user time - microseconds */
	LL_EventUsageStepSystemTime64,    /* int64_t * : event usage step system time - microseconds */
	LL_EventUsageStepMaxrss64,        /* int64_t * : event usage step ru_maxrss */
	LL_EventUsageStepIxrss64,         /* int64_t * : event usage step ru_ixrss */
	LL_EventUsageStepIdrss64,         /* int64_t * : event usage step ru_idrss */
	LL_EventUsageStepIsrss64,         /* int64_t * : event usage step ru_isrss */
	LL_EventUsageStepMinflt64,        /* int64_t * : event usage step ru_minflt */
	LL_EventUsageStepMajflt64,        /* int64_t * : event usage step ru_majflt */
	LL_EventUsageStepNswap64,         /* int64_t * : event usage step ru_nswap */
	LL_EventUsageStepInblock64,       /* int64_t * : event usage step ru_inblock */
	LL_EventUsageStepOublock64,       /* int64_t * : event usage step ru_oublock */
	LL_EventUsageStepMsgsnd64,        /* int64_t * : event usage step ru_msgsnd */
	LL_EventUsageStepMsgrcv64,        /* int64_t * : event usage step ru_msgrcv */
	LL_EventUsageStepNsignals64,      /* int64_t * : event usage step ru_nsignals */
	LL_EventUsageStepNvcsw64,         /* int64_t * : event usage step ru_nvcsw */
	LL_EventUsageStepNivcsw64,        /* int64_t * : event usage step ru_nivcsw */
	LL_EventUsageStarterUserTime64,   /* int64_t * : event usage starter user time - microseconds */
	LL_EventUsageStarterSystemTime64, /* int64_t * : event usage starter system time - microseconds */
	LL_EventUsageStarterMaxrss64,     /* int64_t * : event usage starter ru_maxrss */
	LL_EventUsageStarterIxrss64,      /* int64_t * : event usage starter ru_ixrss */
	LL_EventUsageStarterIdrss64,      /* int64_t * : event usage starter ru_idrss */
	LL_EventUsageStarterIsrss64,      /* int64_t * : event usage starter ru_isrss */
	LL_EventUsageStarterMinflt64,     /* int64_t * : event usage starter ru_minflt */
	LL_EventUsageStarterMajflt64,     /* int64_t * : event usage starter ru_majflt */
	LL_EventUsageStarterNswap64,      /* int64_t * : event usage starter ru_nswap */
	LL_EventUsageStarterInblock64,    /* int64_t * : event usage starter ru_inblock */
	LL_EventUsageStarterOublock64,    /* int64_t * : event usage starter ru_oublock */
	LL_EventUsageStarterMsgsnd64,     /* int64_t * : event usage starter ru_msgsnd */
	LL_EventUsageStarterMsgrcv64,     /* int64_t * : event usage starter ru_msgrcv */
	LL_EventUsageStarterNsignals64,   /* int64_t * : event usage starter ru_nsignals */
	LL_EventUsageStarterNvcsw64,      /* int64_t * : event usage starter ru_nvcsw */
	LL_EventUsageStarterNivcsw64,     /* int64_t * : event usage starter ru_nivcsw */
	/* Class object data */
	LL_ClassName = 3700,         /* char ** : The name of the class */
	LL_ClassPriority,            /* int *  : The class system priority */
	LL_ClassExcludeUsers,        /* char *** : users not permitted to use the class */
	LL_ClassIncludeUsers,        /* char *** : users permitted to use the class */
	LL_ClassExcludeGroups,       /* char *** : groups not permitted to use the class */
	LL_ClassIncludeGroups,       /* char *** : groups permitted to use the class */
	LL_ClassAdmin,               /* char *** : administrators for the class */
	LL_ClassNqsClass,            /* int *    : whether the class is a NQS gateway */
	LL_ClassNqsSubmit,           /* char **  : The NQS queue to submit jobs */
	LL_ClassNqsQuery,            /* char *** : The NQS queues to query about job dispatch */
	LL_ClassMaxProcessors,       /* int *    : The maximum number of processors for a parallel job step */
	LL_ClassMaxJobs,             /* int *    : The maximum number of job steps that can run at any time */
	LL_ClassGetFirstResourceRequirement, /* LL_element * (ResourceReq) */
	LL_ClassGetNextResourceRequirement,  /* LL_element * (ResourceReq) */
	LL_ClassComment,             /* char **  : Class comment */
	LL_ClassCkptDir,             /* char **  : The directory for checkpoint files */
	LL_ClassCkptTimeHardLimit,   /* int64_t *: ckpt time hard limit */
	LL_ClassCkptTimeSoftLimit,   /* int64_t *: ckpt time soft limit */
	LL_ClassWallClockLimitHard,  /* int64_t *: wall clock hard limit */
	LL_ClassWallClockLimitSoft,  /* int64_t *: wall clock soft limit */
	LL_ClassCpuStepLimitHard,    /* int64_t *: Hard Job_cpu_limit */
	LL_ClassCpuStepLimitSoft,    /* int64_t *: Soft Job_cpu_limit */
	LL_ClassCpuLimitHard,        /* int64_t *: cpu hard limit */
	LL_ClassCpuLimitSoft,        /* int64_t *: cpu soft limit */
	LL_ClassDataLimitHard,       /* int64_t *: data hard limit */
	LL_ClassDataLimitSoft,       /* int64_t *: data soft limit */
	LL_ClassCoreLimitHard,       /* int64_t *: core file hard limit */
	LL_ClassCoreLimitSoft,       /* int64_t *: core file soft limit */
	LL_ClassFileLimitHard,       /* int64_t *: file size hard limit */
	LL_ClassFileLimitSoft,       /* int64_t *: file size soft limit */
	LL_ClassStackLimitHard,      /* int64_t *: stack size hard limit */
	LL_ClassStackLimitSoft,      /* int64_t *: stack size soft limit */
	LL_ClassRssLimitHard,        /* int64_t *: resident set size hard limit */
	LL_ClassRssLimitSoft,        /* int64_t *: resident set size soft limit */
	LL_ClassNice,                /* int *    : The nice value */
	LL_ClassFreeSlots,           /* int *    : The number of available initiators */
	LL_ClassMaximumSlots,        /* int *    : The total number of configured initiators */
	LL_ClassConstraints,         /* int *    : Whether values of Maximum and Free Slots are constrained by MAX_STARTERS and MAXJOBS */
	LL_ClassExecutionFactor,     /* int *    : The execution factor */
	LL_ClassMaxTotalTasks,       /* int *    : The value for Max_total_tasks */
	LL_ClassPreemptClass,        /* char **  : The PREEMPT_CLASS rule */
	LL_ClassStartClass,          /* char **  : The START_CLASS rule */
	LL_ClassMaxProtocolInstances,/* int *    : The maximum number of windows per protocol per task */
	/* end of Class object data */

	/* Reservation object data */
	LL_ReservationID = 3800,     /* char ** : The ID of the reservation */
	LL_ReservationStartTime,     /* time_t *: The beginning time of the reservation */
	LL_ReservationDuration,      /* int *   : Reservation duration in the unit of minutes */
	LL_ReservationMachines,      /* char ***: Machines reserved by the reservation */
	LL_ReservationJobs,          /* char ***: Job steps bound to the reservation */
	LL_ReservationModeShared,    /* int *   : RESERVATION_SHARED mode is on if 1; off if 0 */
	LL_ReservationModeRemoveOnIdle, /* int *: RESERVATION_REMOVE_ON_IDLE mode is on if 1; off if 0 */
	LL_ReservationStatus,        /* int *   : The state of the reservation */
	LL_ReservationOwner,         /* char ** : The owner of the reservation */
	LL_ReservationGroup,         /* char ** : The LoadLeveler group which owns the reservation */
	LL_ReservationCreateTime,    /* time_t *: The creation time of the reservation */
	LL_ReservationModifiedBy,    /* char ** : The userid who last modified the reservation */
	LL_ReservationModifyTime,    /* time_t *: The last modification time */
	LL_ReservationUsers,         /* char ***: The users who may run jobs in the reservation */
	LL_ReservationGroups,        /* char ***: The LoadLeveler groups whose users may run jobs in the reservation */
	/* end of Reservation object data */

	/* Multicluster object data */
	LL_MClusterName = 3900,    /* char ** : The name of the multi cluster */
	LL_MClusterInboundScheddPort, /* int * : The inbound Schedd port for the multi cluster */
	LL_MClusterLocal,          /* int * : The boolean local status for this multi cluster */
	LL_MClusterInboundHosts,   /* char ** : The inbound schedd hosts(clusters) */
	LL_MClusterOutboundHosts,  /* char ** : The outbound schedd hosts(clusters) */
	LL_MClusterIncludeUsers,   /* char ** : The include users(clusters) */
	LL_MClusterExcludeUsers,   /* char ** : The exclude users(clusters) */
	LL_MClusterIncludeGroups,  /* char ** : The include groups(clusters) */
	LL_MClusterExcludeGroups,  /* char ** : The exclude groups(clusters) */
	LL_MClusterIncludeClasses, /* char ** : The include classes(clusters) */
	LL_MClusterExcludeClasses, /* char ** : The exclude classes(clusters) */
	LL_MClusterSecureScheddPort, /* int * : The secure schedd port for the cluster */
	LL_MClusterMulticlusterSecurity, /* char ** : The security method for mutlicluster */
	LL_MClusterSslCipherList, /* char ** : The list of cipher for SSL */
	/* end of Multicluster object data */

	/* MCM object data */
	LL_MCMID = 4000,					  	/* int *  : mcm id */
	LL_MCMCPUs,					          /* int *  : total available cpus on a mcm */
	LL_MCMCPUList,					      /* int ** : list of available cpus on a mcm */
	/* end of MCM object data */

	/* Blue Gene machine data */
	LL_BgMachineBPSize = 4100,     /* int ** : Size of base partitions in c-nodes in each dimension */
	LL_BgMachineSize,              /* int ** : Size of system in base partitions in each dimension */
	LL_BgMachineSwitchCount,       /* int * : Number of switches in the system */
	LL_BgMachineWireCount,         /* int * : Number of wires in the system */
	LL_BgMachinePartitionCount,    /* int * : Number of partitions defined */
	LL_BgMachineGetFirstBP,        /* LL_element * : First element in the base partition list */
	LL_BgMachineGetNextBP,         /* LL_element * : Next element in the base partition list */
	LL_BgMachineGetFirstSwitch,    /* LL_element * : First element in the switch list */
	LL_BgMachineGetNextSwitch,     /* LL_element * : Next element in the switch list */
	LL_BgMachineGetFirstWire,      /* LL_element * : First element in the wire list */
	LL_BgMachineGetNextWire,       /* LL_element * : Next element in the wire list */
	LL_BgMachineGetFirstPartition, /* LL_element * : First element in the partition list */
	LL_BgMachineGetNextPartition,  /* LL_element * : Next element in the partition list */

	/* Blue Gene base partition data */
	LL_BgBPId = 4200,                     /* char ** : Id of base partition */
	LL_BgBPState,                  /* int * : State of the base partition */
	LL_BgBPLocation,               /* int ** : Location of base partition in system in each dimension */
	LL_BgBPSubDividedBusy,         /* int * : Flag indicates small partiton active in BP */
	LL_BgBPCurrentPartition,       /* char * : Id of assigned partition */
	LL_BgBPCurrentPartitionState,  /* int * : State of assigned partition */
	LL_BgBPNodeCardCount,          /* int * : Number of node cards defined */
	LL_BgBPGetFirstNodeCard,       /* LL_element * : First element in the node card list */
	LL_BgBPGetNextNodeCard,        /* LL_element * : Next element in the node card list */

	/* Blue Gene switch data */
	LL_BgSwitchId = 4300,          /* char ** : Id of switch */
	LL_BgSwitchBasePartitionId,    /* char ** : Id of base partition connected to switch */
	LL_BgSwitchState,              /* int * : State of the switch */
	LL_BgSwitchDimension,          /* int * : Dimension the switch is associated with */
	LL_BgSwitchConnCount,          /* int * : Number of connections in the switch */
	LL_BgSwitchGetFirstConn,       /* LL_element * : First element in the switch connection list */
	LL_BgSwitchGetNextConn,        /* LL_element * : Next element in the switch connection list */

	/* Blue Gene switch connection data */
	LL_BgPortConnToSwitchPort = 4400, /* int * : Id of to switch port */
	LL_BgPortConnFromSwitchPort,   /* int * : Id of from switch port */
	LL_BgPortConnCurrentPartition, /* char ** : Id of partition the connection is assigned to */
	LL_BgPortConnCurrentPartitionState, /* int * : State of partition the connection is assigned to */

	/* Blue Gene wire data */
	LL_BgWireId = 4500,             /* char ** : Id of the wire */
	LL_BgWireState,                 /* int * : State of the wire */
	LL_BgWireFromPortCompId,        /* char ** : Id of the source component */
	LL_BgWireFromPortId,            /* int * : Id of the source port  */
	LL_BgWireToPortCompId,          /* char ** : Id of the destination component */
	LL_BgWireToPortId,              /* int * : Id of the destination port  */
	LL_BgWireCurrentPartition,      /* char ** : Id of partiton which wire is assigned to */
	LL_BgWireCurrentPartitionState, /* int * : State of partiton which wire is assigned to */

	/* Blue Gene partition data */
	LL_BgPartitionId = 4600,        /* char ** : Partition Id */
	LL_BgPartitionState,            /* int * : Partition State */
	LL_BgPartitionBPCount,          /* int * : Number of base partitions in partition */
	LL_BgPartitionSwitchCount,      /* int * : Number of switches in partition */
	LL_BgPartitionBPList,           /* char *** : List of base partition ids assigned to partition */
	LL_BgPartitionGetFirstSwitch,   /* LL_element * : First element in the switch list */
	LL_BgPartitionGetNextSwitch,    /* LL_element * : Next element in the switch list */
	LL_BgPartitionNodeCardList,     /* char *** : List of node card ids assigned to partition */
	LL_BgPartitionConnection,       /* int * : Connection type */
	LL_BgPartitionOwner,            /* char ** : User who owns the partition */
	LL_BgPartitionMode,             /* int * : Node mode of the partition */
	LL_BgPartitionSmall,            /* int * : Partition is smaller than base partition */
	LL_BgPartitionMLoaderImage,     /* char ** : File name of machine loader image */
	LL_BgPartitionBLRTSImage,       /* char ** : File name of cnode's kernel image */
	LL_BgPartitionLinuxImage,       /* char ** : File name of I/O nodes Linux image */
	LL_BgPartitionRamDiskImage,     /* char ** : File name of ramdisk image */
	LL_BgPartitionDescription,      /* char ** : Partition description */

	/* Blue Gene node card data */
	LL_BgNodeCardId = 4700,            /* char ** : Id of the wire */
	LL_BgNodeCardState,                /* int * : State of the wire */
	LL_BgNodeCardQuarter,              /* int * : quarter of BP which node card is in */
	LL_BgNodeCardCurrentPartition,     /* char ** : Id of partiton which node card is assigned to */
	LL_BgNodeCardCurrentPartitionState, /* int * : State of partiton which node card is assigned to */

	/* ClusterFile data */
	LL_ClusterFileLocalPath = 4800, /* char **: expanded local file pathname */
	LL_ClusterFileRemotePath, /* char **: expanded remote file pathname */
	/* end of ClusterFile data */

	LL_LastGetDataSpecification
};

#define FREE_SLOTS_LIMITED_BY_MAX_STARTERS    1
#define MAXIMUM_SLOTS_LIMITED_BY_MAX_STARTERS 2
#define FREE_SLOTS_LIMITED_BY_MAX_JOBS        4

enum SummaryReportType { NUMERIC = (1<<0),
						 RESOURCE = (1<<1),
						 AVGTHROUGHPUT = (1<<2),
						 MAXTHROUGHPUT= (1<<3),
						 MINTHROUGHPUT=(1<<4),
						 THROUGHPUT=(AVGTHROUGHPUT|MAXTHROUGHPUT|MINTHROUGHPUT),
						 REPORT_ALL = (NUMERIC|RESOURCE|AVGTHROUGHPUT|MAXTHROUGHPUT|MINTHROUGHPUT),
						 REPORT_DEFAULT = (RESOURCE) };

enum SummarySectionType { USER = (1<<0),
						  SECTION_GROUP = (1<<1),
						  CLASS = (1<<2),
						  ACCOUNT = (1<<3),
						  UNIXGROUP = (1<<4),
						  DAY = (1<<5),
						  WEEK = (1<<6),
						  MONTH = (1<<7),
						  JOBID = (1<<8),
						  JOBNAME = (1<<9),
						  ALLOCATED = (1<<10),
						  SECTION_ALL = (USER|SECTION_GROUP|CLASS|ACCOUNT|UNIXGROUP|DAY|WEEK|MONTH|JOBID|JOBNAME|ALLOCATED),
						  SECTION_DEFAULT=(USER|SECTION_GROUP|CLASS|ACCOUNT),
						  TIME_MASK=(DAY|WEEK|MONTH) };


enum SummaryDisplayFormat {
	EXTENDED_FORMAT = (1<<0),
	SUMMARY_FORMAT  = (1<<1),
	QUERY_FORMAT    = (1<<2),
	GUI_FORMAT      = (1<<3) };

enum LL_control_op {
	LL_CONTROL_RECYCLE, LL_CONTROL_RECONFIG, LL_CONTROL_START,
	LL_CONTROL_STOP, LL_CONTROL_DRAIN, LL_CONTROL_DRAIN_STARTD,
	LL_CONTROL_DRAIN_SCHEDD, LL_CONTROL_PURGE_SCHEDD, LL_CONTROL_FLUSH,
	LL_CONTROL_SUSPEND, LL_CONTROL_RESUME, LL_CONTROL_RESUME_STARTD,
	LL_CONTROL_RESUME_SCHEDD, LL_CONTROL_FAVOR_JOB, LL_CONTROL_UNFAVOR_JOB,
	LL_CONTROL_FAVOR_USER, LL_CONTROL_UNFAVOR_USER,
	LL_CONTROL_HOLD_USER, LL_CONTROL_HOLD_SYSTEM, LL_CONTROL_HOLD_RELEASE,
	LL_CONTROL_PRIO_ABS, LL_CONTROL_PRIO_ADJ, LL_CONTROL_START_DRAINED };

#define LL_CONTROL_VERSION_22  22
#define LL_CONTROL_VERSION_310 310
#define LL_CONTROL_VERSION     310

/*   Structures to support API interfaces */
typedef struct {
	int		cluster;
	int		proc;
	char		*from_host;	/* name of the schedd host */
}
LL_STEP_ID;

typedef struct {
	int		nqs_flags;	/* flags for controlling NQS step submission */
	char		*nqs_submit;	/* NQS submit queue */
	char		*nqs_query;	/* NQS query queues */
	char		*umask;		/* value of umask on submitting machine */
}
LL_NQS;

typedef struct {
	int  cpu_hard_limit;            /* cpu time cannot exceed this */
	int  cpu_soft_limit;            /* value set by user that is LE hard limit */
	int  data_hard_limit;           /* data size cannot exceed this */
	int  data_soft_limit;           /* value set by user that is LE hard limit */
	int  core_hard_limit;           /* size of core file cannot exceed this */
	int  core_soft_limit;           /* value set by user that is LE hard limit */
	int  file_hard_limit;           /* file size cannot exceed this */
	int  file_soft_limit;           /* value set by user that is LE hard limit */
	int  rss_hard_limit;            /* resident set size upper limit */
	int  rss_soft_limit;            /* value set by user that is LE hard limit */
	int  stack_hard_limit;          /* stack size cannot exceed this */
	int  stack_soft_limit;          /* value set by user that is LE hard limit */
	int  hard_cpu_step_limit;       /* hard CPU limit for the whole job step */
	int  soft_cpu_step_limit;       /* soft CPU limit for the whole job step */
	int  hard_wall_clock_limit;     /* hard limit for elapsed time */
	int  soft_wall_clock_limit;     /* soft limit for elapsed time */
	int  ckpt_time_hard_limit;	  /* ckpt time cannot exceed this */
	int  ckpt_time_soft_limit;	  /* value set by user that is LE hard limit */
}
LL_LIMITS;

typedef struct {
	int64_t cpu_hard_limit;        /* cpu time cannot exceed this */
	int64_t cpu_soft_limit;        /* set by user that is LE hard limit */
	int64_t data_hard_limit;       /* data size cannot exceed this */
	int64_t data_soft_limit;       /* set by user that is LE hard limit */
	int64_t core_hard_limit;       /* size of core file cannot exceed this */
	int64_t core_soft_limit;       /* set by user that is LE hard limit */
	int64_t file_hard_limit;       /* file size cannot exceed this */
	int64_t file_soft_limit;       /* set by user that is LE hard limit */
	int64_t rss_hard_limit;        /* resident set size upper limit */
	int64_t rss_soft_limit;        /* set by user that is LE hard limit */
	int64_t stack_hard_limit;      /* stack size cannot exceed this */
	int64_t stack_soft_limit;      /* set by user that is LE hard limit */
	int64_t hard_cpu_step_limit;   /* hard CPU limit for the whole job step */
	int64_t soft_cpu_step_limit;   /* soft CPU limit for the whole job step */
	int64_t hard_wall_clock_limit; /* hard limit for elapsed time */
	int64_t soft_wall_clock_limit; /* soft limit for elapsed time */
	int64_t ckpt_time_hard_limit;  /* ckpt time cannot exceed this */
	int64_t ckpt_time_soft_limit;  /* set by user that is LE hard limit */
}
LL_LIMITS64;

typedef struct ll_event_usage {
	int  event;                       /* event identifier             */
	char *name;                       /* event name			*/
	int  time;                        /* timestamp of this event      */
	struct rusage starter_rusage;     /* usage by starter at this event */
	struct rusage step_rusage;        /* usage by user's job step at this event */
	struct ll_event_usage *next;      /* next event			*/
}
LL_EVENT_USAGE;

typedef struct ll_event_usage64 {
	int   event;                      /* event identifier */
	char  *name;                      /* event name */
	int   time;                       /* timestamp of this event */
	struct rusage64 starter_rusage64; /* usage by starter at this event */
	struct rusage64 step_rusage64;    /* usage by user's job step at this event */
	struct ll_event_usage64 *next;    /* next event */
}
LL_EVENT_USAGE64;

typedef struct ll_dispatch_usage {
	int dispatch_num;                 /* # of event usages for this dispatch */
	struct rusage starter_rusage;     /* accumulated usage by starter */
	struct rusage step_rusage;        /* accumulated usage by user's job step */
	LL_EVENT_USAGE *event_usage;      /* per event usage detail    */
	struct ll_dispatch_usage *next;   /* next dispatch	*/
}
LL_DISPATCH_USAGE;

typedef struct ll_dispatch_usage64 {
	int dispatch_num;                 /* # of event usages for this dispatch */
	struct rusage64 starter_rusage64; /* accumulated usage by starter */
	struct rusage64 step_rusage64;    /* accumulated usage by user's job step */
	LL_EVENT_USAGE64 *event_usage64;  /* per event usage detail */
	struct ll_dispatch_usage64 *next; /* next dispatch  */
}
LL_DISPATCH_USAGE64;

typedef struct ll_mach_usage {
	char *name;                        /* machine name			*/
	float machine_speed;               /* machine speed		*/
	int   dispatch_num;                /* # of dispatches for this machine */
	LL_DISPATCH_USAGE *dispatch_usage; /* per dispatch usage detail*/
	struct ll_mach_usage *next;        /* next machine			*/
}
LL_MACH_USAGE;

typedef struct ll_mach_usage64 {
	char     *name;                        /* machine name */
	float    machine_speed;                /* machine speed */
	int      dispatch_num;                 /* # of dispatches for this machine */
	LL_DISPATCH_USAGE64 *dispatch_usage64; /* per dispatch usage detail */
	struct ll_mach_usage64 *next;          /* next machine */
}
LL_MACH_USAGE64;

typedef struct {
	struct rusage starter_rusage; /* accumulated usage by starters */
	struct rusage step_rusage;	/* accumulated usage by user's step */
	LL_MACH_USAGE	*mach_usage;    /* detail usage                   */
}
LL_USAGE;

typedef struct {
	struct rusage64 starter_rusage64; /* accumulated usage by starters */
	struct rusage64 step_rusage64;    /* accumulated usage by user's step */
	LL_MACH_USAGE64 *mach_usage64;    /* detail usage */
}
LL_USAGE64;

typedef struct {
	int		x;          /* Number of compute nodes in x-direction. */
	int		y;          /* Number of compute nodes in y-direction. */
	int		z;          /* Number of compute nodes in z-direction. */
}
LL_BG_SHAPE;

typedef struct {
	/* The following are inputs needed before scheduling is performed */

	char		*step_name;	/* step name */
	char		*requirements;	/* step requirements */
	char		*preferences;	/* step preferences */
	int		prio;		/* user step priority */
	char		*dependency;	/* step step dependancy */
	char		*group_name;	/* step group name */
	char		*stepclass;	/* step class */
	int		start_date;	/* Don't start before this date */
	int		flags;		/* Special step characteristics */
	int		min_processors; /* minimum # of requested processors */
	int		max_processors; /* maximum # of requested processors */
	char		*account_no;	/* Account number associated with step */
	char		*comment;	/* users comment about the step */

	/* The following are valid after queuing has occured */

	LL_STEP_ID	id;		/* step id */
	int		q_date;		/* UNIX time step was submitted */
	int		status;		/* Running, unexpanded, completed,.. */

	/* The following are valid after scheduling has occured */

	int		num_processors; /* actual number of assigned processors */
	char		**processor_list;/* list of processors on which to run step */

	/* The following are inputs needed to actually start an executable */

	char		*cmd;		/* a.out file */
	char		*args;		/* command line args */
	char		*env;		/* environment */
	char		*in;		/* file for stdin */
	char		*out;		/* file for stdout */
	char		*err;		/* file for stderr */
	char		*iwd;		/* Initial working directory */
	char		*notify_user;	/* target to be used when sending mail */
	char		*shell;		/* shell to be used */
	char		*tracker;	/* user's step tracking exit */
	char		*tracker_arg;	/* argument to tracking exit */
	int		notification;	/* Notification options */
	int		image_size;	/* Size of the virtual image in K */
	int		exec_size;	/* size of the executable */
	LL_LIMITS	limits;		/* step resource limits */
	LL_NQS	nqs_info;	/* info for a NQS step */

	/* The following are valid after the executable has started */
	int		dispatch_time; /* UNIX time negotiator dispatched job */
	int		start_time;	/* UNIX time the starter started */
	int		unused1;	/* reserved for future use */

	/* The following are valid after the executable has completed/terminated */

	int		completion_code;/* step exit status */
	int		completion_date;/* UNIX time step was completed */
	int		start_count;	/* times step has been started */
	LL_USAGE	usage_info;	/* step usage information */

	/* Priorities set from the admin file stanzas */
	int		user_sysprio;	/* user component of system  job priority */
	int		group_sysprio;	/* group component of system job priority */
	int		class_sysprio;	/* class component of system job priority */
	int		number;   	/* user number. Substitutable in LoadL config macros */

	/* Consumable resources requested and adapter pinned memory used */
	int		cpus_requested;           /* ConsumableCpus requested */
	int		virtual_memory_requested; /* VirtualMemory requested */
	int		memory_requested;         /* Memory (real) requested */
	int		adapter_used_memory;      /* pinned memory used by adapters */

	int   adapter_req_count;   /* number of adapter_req records */
	void  **adapter_req; /* adapter requirements - step->getFirstAdapterReq() ...  */

	/* 64-bit elements and structures */
	int64_t  image_size64;                /* Size of the virtual image in K */
	int64_t  exec_size64;                 /* size of the executable */
	int64_t  virtual_memory_requested64;  /* VirtualMemory requested */
	int64_t  memory_requested64;          /* Memory (real) requested */
	LL_LIMITS64 limits64;                 /* 64-bit step resource limits */
	LL_USAGE64  usage_info64;             /* 64-bit step usage information */

	/* Checkpoint statistics  */
	int	good_ckpt_start_time;		/* Time stamp of last successful ckpt */
	int	accum_ckpt_time;		/* accumulatd time job has spent checkpointing */
	char	*ckpt_dir;			/* Checkpoint directory */
	char	*ckpt_file;			/* Checkpoint file	*/

	/* Large Page Data/Heap support */
	char   *large_page;      /* Large Page requirement */

	/* RDMA Support */
	int           bulkxfer;         /* Did step request BLOCKXFER? */
	int           rcxtblocks;       /* User rCxt blocks */

	/* Advance Reservation */
	char   *reservation_id;              /* Reservation id */
	char   *requested_reservation_id;    /* Requested reservaiton id */

	/* AIX Advanced Accounting   */
	int64_t	acct_key;		     /* Job accounting key */

	/* Blue Gene Support   */
	int     bg_req_size;		     /* Requested size of Blue Gene partition in units of compute nodes.*/
	int     bg_alloc_size;		     /* Allocated size of Blue Gene partition in units of compute nodes.*/
	LL_BG_SHAPE 	bg_req_shape;	     /* Requested shape of Blue Gene Partition in units of cubical compute nodes.*/
	LL_BG_SHAPE 	bg_alloc_shape;	     /* Allocated shape of Blue Gene Partition in units of cubical compute nodes.*/
	char 	*bg_req_connection;	     /* Requested type of wiring for the partition.*/
	char 	*bg_alloc_connection;	     /* Allocated type of wiring for the partition.*/
	char 	*bg_mode;		     /* Requested mode of the partition. */
	char 	*bg_rotate;		     /* Whether the scheduler is free to rotate the requested Blue Gene shape.*/
	char 	*bg_job_id;		     /* Id of the Blue Gene job. */
	char 	*bg_partition_id;	     /* Id of the partition allocated for the job. */
	char 	*bg_alloc_partition;	     /* Id of the partition allocated for the job. */
	char 	*bg_req_partition;	     /* The partition Id requested for the job. */
	char 	*bg_error_text;		     /* Error text in the Blue Gene  job record. */

	/* Reserved fields */
	LL_element   *reserved001;	/* Reserved */
}
LL_job_step;

typedef struct LL_job {
	int		version_num;	/* LL_JOB_VERSION */
	char		*job_name;	/* job name */
	char		*owner;		/* login of person submitting job */
	char		*groupname;	/* group name of owner's login group */
	uid_t		uid;		/* unix userid of submitter */
	gid_t		gid;		/* unix group id of submitter */
	char		*submit_host;	/* Host of job submission */
	int		steps;		/* number of steps in job */
	LL_job_step	**step_list;	/* ptr to array of ptrs to job steps */
}
LL_job;

typedef struct LL_node {
	char          *nodename;              /* Name of this node */
	int           version_num;            /* PROC_VERSION */
	int           configtimestamp;        /* Date and time of last reconfig */
	int		time_stamp;		/* Time stamp of data */
	int           virtual_memory;         /* Available swap space in kilobytes */
	int           memory;                 /* Physical memory */
	int           disk;                   /* Avail space (KB) in execute directory */
	float         loadavg;                /* Berkeley one minute load average */
	float         speed;                  /* Speed associated with the node */
	int           max_starters;           /* Max number of jobs allowed */
	int           pool;           	/* Pool number associated with node */
	int           cpus;           	/* number of CPUs */
	char          *state;         	/* Startd state */
	int           keywordidle;   		/* seconds since keyboard activity */
	int           totaljobs;      	/* total number of submitted jobs */
	char          *arch;          	/* Hardware Architecture */
	char          *opsys;         	/* Operating system */
	char          **adapter;      	/* Names of available adapters */
	char          **feature;      	/* set of all features */
	char          **job_class;    	/* Job classes allowed to run */
	char          **initiators;   	/* Initiators available */
	LL_STEP_ID    *steplist;    		/* steps allocated to run */
	int64_t       virtual_memory64;/* Available swap space in kilobytes */
	int64_t       memory64;        /* Physical memory */
	int64_t       disk64;          /* Avail space (KB) in execute directory */
}
LL_node;

/******************************************************
 Scheduling API data structures.
 ******************************************************/

typedef struct LL_get_jobs_info {
	int             version_num;
	int             numJobs;
	LL_job          **JobList;
}
LL_get_jobs_info;

typedef struct LL_get_nodes_info {
	int             version_num;
	int             numNodes;
	LL_node         **NodeList;
}
LL_get_nodes_info;

typedef struct LL_start_job_info {
	int             version_num;
	LL_STEP_ID      StepId;
	char            **nodeList;
}
LL_start_job_info;

/* This macro allows the mem field to be accessed by the name api_rcxtblocks */
/* so that when rCxt blocks are supported by the adapters the name of the    */
/* field is consistent with its meaing                                       */
#define api_rcxtblocks mem
typedef struct ll_adapter_usage {
	char * dev_name;		/* Adapter device name */
	char * protocol;		/* MPI, LAPI or MPI_LAPI */
	char * subsystem;		/* IP or US */
	int    wid;			/* window id */
	uint64_t mem;			/* adapter window memory */
}
LL_ADAPTER_USAGE;


typedef struct LL_start_job_info_ext {
	int             version_num;
	LL_STEP_ID      StepId;
	char            **nodeList;
	int adapterUsageCount;
	LL_ADAPTER_USAGE * adapterUsage;
}
LL_start_job_info_ext;

typedef struct LL_terminate_job_info {
	int             version_num;
	LL_STEP_ID      StepId;
	char            *msg;
}
LL_terminate_job_info;

/*
 *  Notification options.
 */
#define LL_NOTIFY_ALWAYS	0
#define LL_NOTIFY_COMPLETE	1
#define LL_NOTIFY_ERROR		2
#define LL_NOTIFY_NEVER		3
#define LL_NOTIFY_START		4

/*
 *  Status values.
 */
#define LL_IDLE			0
#define LL_STARTING		1
#define LL_RUNNING		2
#define LL_REMOVED		3
#define LL_COMPLETED		4
#define LL_HOLD			5
#define LL_DEFERRED		6
#define LL_SUBMISSION_ERR	7
#define LL_VACATE		8
#define LL_NOTRUN		9
#define LL_NOTQUEUED            10
#define LL_MAX_STATUS		10

/*
 *  Step flags.
 */
#define LL_CHECKPOINT		(1<<0)
#define LL_SYSTEM_HOLD		(1<<1)
#define LL_USER_HOLD		(1<<2)
#define LL_RESTART		(1<<3)
#define LL_CPU_LIMIT_USER	(1<<4)
#define LL_CORE_LIMIT_USER	(1<<5)
#define LL_DATA_LIMIT_USER	(1<<6)
#define LL_FILE_LIMIT_USER	(1<<7)
#define LL_RSS_LIMIT_USER	(1<<8)
#define LL_STACK_LIMIT_USER	(1<<9)
#define LL_NQS_STEP		(1<<10)
#define LL_STEP_PARALLEL	(1<<11)
#define LL_STEP_PVM3		(1<<12)
#define LL_IMMEDIATE		(1<<13)
#define LL_NO_ALLOCATE		(1<<14)
#define LL_INTERACTIVE		(1<<15)
#define LL_API_ACTIVE		(1<<16)
#define LL_API_SYNC_START	(1<<17)
#define LL_NODE_USAGE_NOT_SHARED (1<<18)
#define LL_RESTART_FROM_CKPT	(1<<19)
#define LL_CHECKPOINT_INTERVAL	(1<<20)
#define LL_RESTART_SAME_NODES	(1<<21)
#define LL_STEP_BLUEGENE	(1<<22)

#define LL_JOB_VERSION		(210)
#define LL_JOB_PROC_VERSION	9

/*
 * The following completion codes are
 * used when status is LL_SUBMISSION_ERR
 */

#define LL_NO_STORAGE		(1)
#define LL_BAD_STATUS		(2)
#define LL_BAD_NOTIFY		(3)
#define LL_BAD_CMD		(4)
#define LL_BAD_EXEC		(5)
#define LL_BAD_REQUIREMENTS	(6)
#define LL_BAD_PREFERENCES	(7)
#define LL_BAD_DEPENDENCY	(8)
#define LL_BAD_ACCOUNT_NO	(9)
#define LL_BAD_PRIO		(10)
#define LL_BAD_GROUP_CONFIG	(11)
#define LL_BAD_GROUP_NAME	(12)
#define LL_BAD_CLASS_CONFIG	(13)
#define LL_BAD_CLASS		(14)
#define LL_BAD_TRANSMIT		(15)

/*
 *      Values for accounting events
 */
#define	LL_LOADL_EVENT		1
#define	LL_INSTALLATION_EVENT	2

/*
 *      Values for scheduling API 
 */
#define	LL_PROC_VERSION		9

#ifndef JMCLIENT_H
/***********************************************************************
 * Resource Manager job request structure for general parallel.
 **********************************************************************/
enum JM_ADAPTER_TYPE {JM_ETHERNET,
					  JM_FDDI,
					  JM_HPS_US,
					  JM_HPS_IP,
					  JM_FCS,
					  JM_TOKENRING};
enum JM_RETURN_CODE {JM_SUCCESS,
					 JM_NOTATTEMPTED,
					 JM_INVALIDPOOL,
					 JM_INVALIDSUBPOOL,
					 JM_INVALIDNODENAME,
					 JM_EXCEEDEDCAPACITY,
					 JM_DOWNONENET,
					 JM_DOWNONSWITCH,
					 JM_INVALIDUSER,
					 JM_INVALIDADAPTER,
					 JM_PARTITIONCREATIONFAILURE,
					 JM_SWITCHFAULT,
					 JM_SYSTEMERROR};

enum JM_REQUEST_TYPE { JM_DEFAULTS=0,JM_EXPLICITMAP=1,
					   JM_ALLOCATEASMANY=2};
struct JM_NODE_INFO {
	char jm_node_name[MAXLEN_HOST];
	char jm_node_address[50];
	int jm_switch_node_number;
	int jm_pool_id;
	int jm_cpu_usage;
	int jm_adapter_usage;
	int jm_num_virtual_tasks;
	int *jm_virtual_task_ids;
	enum JM_RETURN_CODE jm_return_code;
};
struct JM_JOB_INFO {
	int jm_request_type;
	char jm_job_description[50];
	enum JM_ADAPTER_TYPE jm_adapter_type;
	int jm_css_authentication;
	int jm_min_num_nodes;
	struct JM_NODE_INFO *jm_min_node_info;
};
#endif /* JMCLIENT_H */

/***********************************************************************
 * Status codes to support general parallel.  
 **********************************************************************/
#define     PARALLEL_OK         0
#define     PARALLEL_CANT_FORK  -1
#define     PARALLEL_BAD_ENVIRONMENT -2
#define     PARALLEL_CANT_GET_HOSTNAME -3
#define     PARALLEL_CANT_RESOLVE_HOST -4
#define     PARALLEL_CANT_MAKE_SOCKET -5
#define     PARALLEL_CANT_CONNECT -6
#define     PARALLEL_CANT_PASS_SOCKET -7
#define     PARALLEL_CANT_GET_HOSTLIST -8
#define     PARALLEL_CANT_START_CMD -9
#define     PARALLEL_NO_DCE_ID     -10         /* DCE identity can not be determined. */
#define     PARALLEL_NO_DCE_CRED   -11         /* No DCE credentials. */
#define     PARALLEL_INSUFFICIENT_DCE_CRED -12 /* DCE credential lifetime less than 300 secs. */
#define     PARALLEL_64BIT_DCE_ERR -13         /* 64-bit API is not supported when DCE is enabled. */

/***********************************************************************
 * Status codes to support external scheduler.
 **********************************************************************/
#define     API_OK                   0  /* API call runs to complete */
#define     API_INVALID_INPUT        -1 /* Invalid input */
#define     API_CANT_CONNECT         -2 /* can't connect to daemon */
#define     API_CANT_MALLOC          -3	/* out of memory */
#define     API_CONFIG_ERR           -4	/* Error from init_params() */
#define     API_CANT_FIND_PROC       -5	/* can't find proc */
#define     API_CANT_TRANSMIT        -6	/* xdr error */
#define     API_CANT_AUTH            -7 /* can't authorize */
#define     API_WRNG_PROC_VERSION    -8 /* Wrong proc version */
#define     API_WRNG_PROC_STATE      -9 /* Wrong proc state */
#define     API_MACH_NOT_AVAIL      -10	/* Machine not available */
#define     API_CANT_FIND_RUNCLASS  -11 /* Can't find run class */
#define     API_REQ_NOT_MET         -12 /* Can't meet requirements */
#define     API_WRNG_MACH_NO   	    -13 /* Wrong machine number */
#define     API_LL_SCH_ON     	    -14 /* Internal scheduler on */
#define     API_MACH_DUP      	    -15 /* Duplicate machine found */
#define     API_NO_DCE_ID           -16 /* User does not have a valid dce id */
#define     API_NO_DCE_CRED         -17 /* No dce credentials */
#define     API_INSUFFICIENT_DCE_CRED -18 /* Insufficient dce credential lifetime */
#define     API_64BIT_DCE_ERR       -19   /* 64-bit API is not supported when DCE is enabled. */
#define     API_BAD_ADAPTER_USAGE   -20	/* Job Start Adapter usage info is inconsistent */
#define     API_BAD_ADAPTER_DEVICE  -21 /* JobStart adapter usage info specified an adapter not on machine */
#define     API_BAD_ADAPTER_USAGE_COUNT -22 /* Wrong number of entries in adapter usage information to JobStart */
#define     API_BAD_ADAPTER_USAGE_PATTERN -23 /* The same protocol pattern is not specified for each task in the JobStart information */
#define     API_BAD_PROTOCOL -24        /* The JobStart information includes an unrecognized protocol string */
#define     API_INCOMPATIBLE_PROTOCOL -25 /* The JobStart information adapter usage information includes protocols that cannot be specified together (eg. PVM and MPI) */
#define     API_BAD_COMMUNICATION_SUBSYSTEM -26 /* The JobStart information adapter usage information includes a communication subsystem that is not IP or US */
#define     API_NO_DCE_SUPPORT_ERR     -27  /* This version of LL does not support DCE security. */
#define     API_NO_CTSEC_SUPPORT_ERR   -28  /* This version of LL does not support CTSEC security. */
#define     API_NO_GANG_SUPPORT_ERR    -29  /* This version of LL does not support GANG scheduling. */
#define     API_NO_PVM_SUPPORT_ERR     -30  /* This version of LL does not support PVM. */
#define     API_NO_NQS_SUPPORT_ERR     -31  /* This version of LL does not support NQS. */
#define     API_STEP_NOT_IDLE     -32  			/* A specified step is not in an idle state */
#define     API_JOB_NOT_FOUND     -33  			/* A specified was not found */
#define     API_JOBQ_ERR     -34  			/* Error occured writing to job queue */
#define     API_CANT_LISTEN     -35  			/* Error occured creating listent socket */
#define     API_TIMEOUT    -36 			/* Timed out waiting for response */
#define     API_SSL_ERR    -37 			/* Timed out waiting for response */

/***********************************************************************
 * Support for Performance Monitor APIs
 **********************************************************************/

#define     LL_INVALID_PTR           -1
#define     LL_INVALID_DAEMON_ID     -2
#define     LL_DAEMON_NOT_CONFIG     -3
#define     LL_HOST_NOT_CONFIG       -4
#define     LL_CANNOT_CONTACT_DAEMON -5
#define     LL_DATA_NOT_RECEIVED     -6
#define     LL_INVALID_FIELD_ID      -7
#define     LL_CONFIG_NOT_FOUND      -8

/***********************************************************************
 * Support for ll_control API.
 **********************************************************************/
#define  LL_CONTROL_OK                 0 /* Command successfully sent to appropriate LL daemon. */
#define  LL_CONTROL_CM_ERR            -2 /* Cannot send command to central manager. */
#define  LL_CONTROL_MASTER_ERR        -3 /* Cannot send command to one of LoadL_master daemons. */
#define  LL_CONTROL_CONFIG_ERR        -4 /* Errors encountered while processing the LL admin/config files. */
#define  LL_CONTROL_XMIT_ERR          -6 /* A data transmission failure occurred. */
#define  LL_CONTROL_AUTH_ERR          -7 /* Calling program does not have LL administrator authority. */
#define  LL_CONTROL_VERSION_ERR      -19 /* An incorrect ll_control version has been specified. */
#define  LL_CONTROL_SYSTEM_ERR       -20 /* A system error occurred. */
#define  LL_CONTROL_MALLOC_ERR       -21 /* Unable to allocate memory. */
#define  LL_CONTROL_INVALID_OP_ERR   -22 /* An invalid control_op operation has been specified. */
#define  LL_CONTROL_JOB_LIST_ERR     -23 /* job_list argument contains one or more errors. */
#define  LL_CONTROL_HOST_LIST_ERR    -24 /* host_list argument contains one or more errors. */
#define  LL_CONTROL_USER_LIST_ERR    -25 /* user_list argument contains one or more errors. */
#define  LL_CONTROL_HOLD_ERR         -26 /* Incompatible arguments specified for HOLD operation. */
#define  LL_CONTROL_PRIO_ERR         -27 /* Incompatible arguments specified for PRIORITY operation. */
#define  LL_CONTROL_FAVORJOB_ERR     -28 /* Incompatible arguments specified for FAVORJOB operation. */
#define  LL_CONTROL_FAVORUSER_ERR    -29 /* Incompatible arguments specified for FAVORUSER operation. */
#define  LL_CONTROL_SYS_ERR          -30 /* An error occurred while trying to start a child process. */
#define  LL_CONTROL_START_ERR        -31 /* An error occurred while trying to start the LoadL_master daemon. */
#define  LL_CONTROL_PURGE_SCHEDD_ERR -32 /* An error occurred while executing the llpurgeschedd command. */
#define  LL_CONTROL_CLASS_ERR        -33 /* class_list argument contains incompatible information. */
#define  LL_CONTROL_TMP_ERR          -34 /* Unable to create a file in /tmp directory. */
#define  LL_CONTROL_ERR              -35 /* Miscellaneous incompatible input specifications. */
#define  LL_CONTROL_NO_DCE_ID        -36 /* DCE identity can not be determined. */
#define  LL_CONTROL_NO_DCE_CRED      -37 /* No DCE credentials. */
#define  LL_CONTROL_INSUFFICIENT_DCE_CRED -38 /* DCE credentials within 300 secs of expiration. */
#define  LL_CONTROL_64BIT_DCE_ERR    -39 /* 64-bit API not supported when DCE is enabled */
#define  LL_CONTROL_NO_DCE_SUPPORT_ERR   -40  /* This version of LL does not support DCE security. */
#define  LL_CONTROL_NO_CTSEC_SUPPORT_ERR -41  /* This version of LL does not support CTSEC security. */
#define  LL_CONTROL_NO_GANG_SUPPORT_ERR  -42  /* This version of LL does not support GANG scheduling. */
#define  LL_CONTROL_NO_PVM_SUPPORT_ERR   -43  /* This version of LL does not support PVM. */
#define  LL_CONTROL_NO_NQS_SUPPORT_ERR   -44  /* This version of LL does not support NQS. */

#if !defined(__linux__)

/***********************************************************************
 * Support for ll_ckpt API.
 **********************************************************************/
typedef enum ckpt_type {CKPT_AND_CONTINUE, CKPT_AND_TERMINATE, CKPT_AND_HOLD} CkptType_t;

typedef enum wait_option {CKPT_NO_WAIT, CKPT_WAIT} WaitOption_t;

enum CkptStart {CKPT_YES, CKPT_NO, CKPT_FAIL};

/* Structure for invoking checkpoint on a specific job step                 */
/* This structure is also used by ll_init_ckpt to return error information  */
/*     When used with ll_init_ckpt, the version should be filled in by the  */
/*     caller, an address to the cp_error_data structure should be passed,  */
/*     error data information will be filled in when rc from ll_init_ckpt   */
/*     is -7, all other values should be set to NULL;			    */
typedef struct LL_ckpt_info {
	int             version;	/* version of the api compiled with */
	char            *step_id;	/* id of step being checkpointed    */
	enum ckpt_type  ckptType;	/* action after success     */
	enum wait_option waitType;	/* identify if blocking enabled */
	int             abort_sig;	/* signal to abort ckpt, default is SIGINT*/
	cr_error_t 	*cp_error_data;  /* structure containing error info from ckpt */
	int		ckpt_rc; 	/* return code from checkpnt()	    */
	int             soft_limit;	/* ckpt soft time limit, in seconds */
	int             hard_limit;	/* ckpt hard time limit, in seconds */
}
LL_ckpt_info;

/***********************************************************************
 * Support for ll_(un)set_ckpt_callbacks APIs.
 **********************************************************************/
typedef struct {
	void (*checkpoint_callback) (void);
	void (*restart_callback) (void);
	void (*resume_callback) (void);
}
callbacks_t;

#endif  /* __linux__ */

static const int flush_ckpt_failure = 0xfcbad;

/***********************************************************************
 * Support for ll_modify API.
 **********************************************************************/
enum LL_modify_op {
	EXECUTION_FACTOR,       /* use int * for data      */
	CONSUMABLE_CPUS,        /* use int * for data      */
	CONSUMABLE_MEMORY,      /* use int64 * for data    */
	WCLIMIT_ADD_MIN,        /* use int * for data      */
	JOB_CLASS,              /* use char * for data     */
	ACCOUNT_NO,             /* use char * for data     */
	STEP_PREEMPTABLE,       /* use int * for data      */
	SYSPRIO,                /* use int * for data      */
	BG_SIZE,                /* use int * for data      */
	BG_SHAPE,               /* use char * for data     */
	BG_CONNECTION,          /* use int * for data      */
	BG_PARTITION,            /* use char * for data     */
	BG_ROTATE,              /* use int * for data      */
	MAX_MODIFY_OP
} ;

typedef struct {
	enum LL_modify_op type;
	void *data;
}
LL_modify_param;

#define MODIFY_SUCCESS	             0
#define MODIFY_INVALID_PARAM        -1 	/* Invalid param specified */
#define MODIFY_CONFIG_ERROR         -2 	/* Configuration error     */
#define MODIFY_NOT_IDLE             -3 	/* Joblist has non-idle step */
#define MODIFY_WRONG_STATE          -4 	/* Joblist has step in wrong state */
#define MODIFY_NOT_AUTH	            -5 	/* Caller not authorized     */
#define MODIFY_SYSTEM_ERROR         -6 	/* Internal system error     */
#define MODIFY_CANT_TRANSMIT        -7 	/* Communication error       */
#define MODIFY_CANT_CONNECT         -8 	/* Connection error          */
#define MODIFY_NO_DCE_SUPPORT_ERR   -9  /* No support for DCE security */
#define MODIFY_NO_CTSEC_SUPPORT_ERR -10 /* No support for CTSEC security */
#define MODIFY_NO_GANG_SUPPORT_ERR  -11 /* No support for GANG scheduling */
#define MODIFY_NO_PVM_SUPPORT_ERR   -12 /* No support for PVM */
#define MODIFY_NO_NQS_SUPPORT_ERR   -13 /* No support for NQS */
#define MODIFY_OVERLAP_RESERVATION  -14 /* Would overlap with reservation */
#define MODIFY_BAD_BG_SHAPE         -15 /* BlueGene partition shape bad */
#define MODIFY_WRONG_JOB_TYPE       -16 /* Modify operation not valid for job type*/
#define MODIFY_BAD_BG_SIZE          -17 /* BlueGene partition size request not positive */
#define MODIFY_BAD_BG_CONNECTION    -18 /* BlueGene connection request not recognized */
#define MODIFY_EMPTY_BG_PARTITION   -19 /* BlueGene requested partion name blank */


/***********************************************************************
 * Support for ll_run_scheduler API.
 **********************************************************************/
#define RUN_SCHEDULER_SUCCESS	0
#define RUN_SCHEDULER_INVALID_PARAM -1		/* Invalid param specified */
#define RUN_SCHEDULER_CONFIG_ERROR  -2		/* Configuration error     */
#define RUN_SCHEDULER_NOT_AUTH	    -3		/* Caller not authorized     */
#define RUN_SCHEDULER_SYSTEM_ERROR  -4		/* Internal system error     */
#define RUN_SCHEDULER_CANT_TRANSMIT -5		/* Communication error       */
#define RUN_SCHEDULER_CANT_CONNECT  -6		/* Connection error          */
#define RUN_SCHEDULER_NEGOTIATOR_INTERVAL_NON_ZERO  -7	/* Non-zero negotiator interval */

/***********************************************************************
 * Support for ll_cluster API.
 **********************************************************************/
typedef enum LL_cluster_op {
	CLUSTER_SET,		/* Set the multicluster environment to cluster_list */
	CLUSTER_UNSET		/* Unset the multicluster environment */
} ClusterOp_t;

typedef struct {
	ClusterOp_t action;	/* CLUSTER_SET or CLUSTER_UNSET */
	char **cluster_list;	/* NULL terminated list of cluster names */
}
LL_cluster_param;

#define CLUSTER_SUCCESS 	0  /* Success */
#define CLUSTER_SYSTEM_ERROR 	-1  /* System error */
#define CLUSTER_INVALID_CLUSTER_PARAM	-2 /* cluster_list param is not valid */
#define CLUSTER_INVALID_ACTION_PARAM	-3 /* action param is not valid */

/***********************************************************************
 * Support for Preemption
 **********************************************************************/
typedef enum LL_preempt_op {
	PREEMPT_STEP,
	RESUME_STEP,
	SYSTEM_PREEMPT_STEP
} PreemptOp_t;

/* Values for the preempt method */
typedef enum preempt_method {
	LL_PREEMPT_SUSPEND,
	LL_PREEMPT_VACATE,
	LL_PREEMPT_REMOVE,
	LL_PREEMPT_SYS_HOLD,
	LL_PREEMPT_USER_HOLD
} PreemptMethod_t;

typedef struct {
	PreemptOp_t type;
	PreemptMethod_t method;
	char **user_list;
	char **host_list;
	char **job_list;
}
LL_preempt_param;

typedef struct {
	char *cluster_name;
	char *job_id;
}
LL_move_job_param;

typedef enum LL_cluster_auth_op {
	CLUSTER_AUTH_GENKEY
} ClusterAuthOp_t;

typedef struct {
	ClusterAuthOp_t type;
}
LL_cluster_auth_param;

/***********************************************************************
 * Support for poe API's(ll_spawn_connect,ll_spawn_read and ll_spawn_write)
 **********************************************************************/
enum LL_JobManagement_RC {
	JOBMGMT_IO_COMPLETE=1, 		/* LoadLeveler I/O is  completed. */
	JOBMGMT_IO_PENDING=0, 		/* LoadLeveler I/O is pending. */
	JOBMGMT_BAD_JOBMGMT_OBJECT=-1, 	/* JobMgmtObject specified is not valid. */
	JOBMGMT_FAILED_CONNECT=-3, 	/* Can not connect to LoadLeveler Daemon. */
	JOBMGMT_SYSTEM=-5,			/* System Error. */
	JOBMGMT_NULL_EXECUTABLE=-6,	/* NULL executable specified. */
	JOBMGMT_TASKMGR_RUNNING=-7,	/* Parallel Task Manager for this job Step is alreay running on the targeted node. */
	JOBMGMT_INCOMPATABLE_NODES=-8,	/* All the nodes targeted to run the parallel job cannot support this interface. */
	JOBMGMT_BAD_MACHINE_OBJECT=-9,	/* Invalid Machine Object. */
	JOBMGMT_BAD_STEP_OBJECT=-10,	/* Invalid Step Object. */
	JOBMGMT_BAD_SEQUENCE=-11,		/* Function is called out of order. */
	JOBMGMT_BAD_FD=-12			/* Invalid Socket descriptor. */
};

/***********************************************************************
 * Support for Advance Reservation
 **********************************************************************/
typedef enum reservation_state_code {
	RESERVATION_WAITING,
	RESERVATION_SETUP,
	RESERVATION_ACTIVE,
	RESERVATION_ACTIVE_SHARED,
	RESERVATION_CANCEL,
	RESERVATION_COMPLETE
} Reservation_State_t;

typedef enum LL_reservation_mode {
	RESERVATION_DEFAULT_MODE = 0,
	RESERVATION_SHARED = (1<<0),
	RESERVATION_REMOVE_ON_IDLE = (1<<1)
} Reservation_Mode_t;

/* enums for Advance Reservation */

/* LL_reservation data identifies the type of data */
enum LL_reservation_data {
	RESERVATION_START_TIME,		/* char *			*/
	RESERVATION_ADD_START_TIME,	/* int *          */
	RESERVATION_DURATION,			/* int *          */
	RESERVATION_ADD_DURATION,		/* int *          */
	RESERVATION_BY_NODE,			/* int *          */
	RESERVATION_ADD_NUM_NODE,		/* int *          */
	RESERVATION_BY_HOSTLIST,	/* char **, NULL terminated	*/
	RESERVATION_ADD_HOSTS,		/* char **, NULL terminated	*/
	RESERVATION_DEL_HOSTS,		/* char **, NULL terminated	*/
	RESERVATION_BY_JOBSTEP,		/* char *			*/
	RESERVATION_BY_JCF,		/* char *			*/
	RESERVATION_USERLIST,		/* char **, NULL terminated	*/
	RESERVATION_ADD_USERS, 		/* char **, NULL terminated	*/
	RESERVATION_DEL_USERS, 		/* char **, NULL terminated	*/
	RESERVATION_GROUPLIST, 		/* char **, NULL terminated	*/
	RESERVATION_ADD_GROUPS,		/* char **, NULL terminated	*/
	RESERVATION_DEL_GROUPS,		/* char **, NULL terminated	*/
	RESERVATION_MODE_SHARED,		/* int *; *data = 0 : Not Shared; *data = 1: Share	*/
	RESERVATION_MODE_REMOVE_ON_IDLE,	/* int *; *data = 0 : Don't Remove; *data = 1 : Remove */
	RESERVATION_OWNER, 		/* char *			*/
	RESERVATION_GROUP		/* char *			*/
};

/* structure used by ll_change_reservation */
typedef struct {
	enum LL_reservation_data type;
	void *data;
}
LL_reservation_change_param;

/* structure used by ll_make_reservation */
typedef struct {
	char **ID;                      /* -> to output string reservation id */
	char *start_time;               /* [mm/dd[/[yy]yy] ]HH:MM format start time */
	int duration;                   /* length of reservation in minutes   */
	enum LL_reservation_data data_type; /* how nodes should be reserved   */
	void *data;                     /* -> to data specifying the nodes    */
	int mode;                       /* shared/remove_on_idle one/both/neither*/
	char **users;                   /* NULL terminated array of user ids  */
	char **groups;                  /* NULL terminated array of LL groups */
	char *group;                    /* group which owns the reservation   */
}
LL_reservation_param;

/* structure for binding jobsteps to a reservation */
typedef struct {
	char **jobsteplist;		/* host.jobid.stepid, null terminated */
	char *ID;			/* -> reservation id, NULL for unbind */
	int unbind;			/* TRUE = unbind, FALSE to bind       */
}
LL_bind_param;

/***********************************************************************
 * Status codes to support Advance Reservation
 **********************************************************************/
#define RESERVATION_OK                      0 /* Success */
#define RESERVATION_LIMIT_EXCEEDED         -1 /* Exceeds max # of reservations allowed in the LoadLeveler cluster */
#define RESERVATION_TOO_CLOSE              -2 /* Reservation is being made within the minimum advance time */
#define RESERVATION_NO_STORAGE             -3 /* The system cannot allocate memory  */
#define RESERVATION_CONFIG_ERR             -4 /* Errors encountered processing the LL admin/config files */
#define RESERVATION_CANT_TRANSMIT          -5 /* A data transmission failure occurred */
#define RESERVATION_GROUP_LIMIT_EXCEEDED   -6 /* Exceeds max # of reservations for the group */
#define RESERVATION_USER_LIMIT_EXCEEDED    -7 /* Exceeds max # of reservations for the user */
#define RESERVATION_SCHEDD_CANT_CONNECT    -8 /* Schedd cannot connect to CM */
#define RESERVATION_API_CANT_CONNECT       -9 /* API cannot connect to the Schedd or CM */
#define RESERVATION_JOB_SUBMIT_FAILED      -10 /* Submit of job command file failed */
#define RESERVATION_NO_MACHINE             -11 /* One or more machines in the hostlist are not in the LoadLeveler cluster */
#define RESERVATION_WRONG_MACHINE          -12 /* Reservations not permitted on one or more machines in the hostlist  */
#define RESERVATION_NO_RESOURCE            -13 /* Insufficient resources in the LoadLeveler cluster */
#define RESERVATION_NOT_SUPPORTED          -14 /* The scheduler in use does not support Advance Reservation  */
#define RESERVATION_NO_JOBSTEP             -15 /* The jobstep used for node selection doesn't exist */
#define RESERVATION_WRONG_JOBSTEP          -16 /* The jobstep used for node selection isn't in the right state */
#define RESERVATION_NOT_EXIST              -17 /* The reservation does not exist */
#define RESERVATION_REQUEST_DATA_NOT_VALID -18 /* Invalid input */
#define RESERVATION_NO_PERMISSION          -19 /* Permission cannot be granted */
#define RESERVATION_TOO_LONG               -20 /* Duration exceeds max allowed */
#define RESERVATION_WRONG_STATE            -21 /* Reservation state is not right for the requested operation */
#define RESERVATION_NO_DCE_CRED            -30 /* DCE is enabled, the user has no credentials */
#define RESERVATION_INSUFFICIENT_DCE_CRED  -31 /* DCE is enabled, credential lifetime < 5 minutes */

/***********************************************************************
 * Support for Blue Gene
 **********************************************************************/
typedef enum bg_bp_state_t {
	BG_BP_UP,
	BG_BP_DOWN,
	BG_BP_MISSING,
	BG_BP_ERROR,
	BG_BP_NAV
} BgBPState_t;

typedef enum bg_partition_state_t {
	BG_PARTITION_FREE,
	BG_PARTITION_CONFIGURING,
	BG_PARTITION_READY,
	BG_PARTITION_BUSY,
	BG_PARTITION_DEALLOCATING,
	BG_PARTITION_ERROR,
	BG_PARTITION_NAV
} BgPartitionState_t;

typedef enum bg_connection_t {
	MESH  = 0,
	TORUS = 1,
	BG_NAV,
	PREFER_TORUS
} BgConnection_t;

typedef enum bg_node_mode_t {
	COPROCESSOR,
	VIRTUAL_NODE
} BgNodeMode_t;

typedef enum bg_port_t {
	BG_PORT_PLUS_X,
	BG_PORT_MINUS_X,
	BG_PORT_PLUS_Y,
	BG_PORT_MINUS_Y,
	BG_PORT_PLUS_Z,
	BG_PORT_MINUS_Z,
	BG_PORT_S0,
	BG_PORT_S1,
	BG_PORT_S2,
	BG_PORT_S3,
	BG_PORT_S4,
	BG_PORT_S5,
	BG_PORT_NAV
} BgPort_t;

typedef enum bg_switch_state_t {
	BG_SWITCH_UP,
	BG_SWITCH_DOWN,
	BG_SWITCH_MISSING,
	BG_SWITCH_ERROR,
	BG_SWITCH_NAV
} BgSwitchState_t;

typedef enum bg_switch_dimension_t {
	BG_DIM_X = 0,
	BG_DIM_Y = 1,
	BG_DIM_Z = 2,
	BG_DIM_NAV
} BgSwitchDimension_t;

typedef enum bg_wire_state_t {
	BG_WIRE_UP,
	BG_WIRE_DOWN,
	BG_WIRE_MISSING,
	BG_WIRE_ERROR,
	BG_WIRE_NAV
} BgWireState_t;

typedef enum bg_node_card_state_t {
	BG_NODE_CARD_UP,
	BG_NODE_CARD_DOWN,
	BG_NODE_CARD_MISSING,
	BG_NODE_CARD_ERROR,
	BG_NODE_CARD_NAV
} BgNodeCardState_t;

typedef enum bg_quarter_t {
	BG_QUARTER_Q1 = 0,
	BG_QUARTER_Q2 = 1,
	BG_QUARTER_Q3 = 2,
	BG_QUARTER_Q4 = 3,
	BG_QUARTER_Q_NAV
} BgQuarter_t;

typedef enum bg_job_state_t {
	BG_JOB_IDLE,
	BG_JOB_STARTING,
	BG_JOB_RUNNING,
	BG_JOB_TERMINATED,
	BG_JOB_KILLED,
	BG_JOB_ERROR,
	BG_JOB_DYING,
	BG_JOB_DEBUG,
	BG_JOB_LOAD,
	BG_JOB_LOADED,
	BG_JOB_BEGIN,
	BG_JOB_ATTACH,
	BG_JOB_NAV
} BgJobState_t;

/***********************************************************************
 * Function Declaration statements
 **********************************************************************/
#ifdef __cplusplus
extern "C" {
#endif
	void llfree_job_info(LL_job *, int);
	int llinit(int);
	int llinitiate(LL_job *, int);
	int llwait(LL_job **, LL_job_step **, int);
	int ll_start_job(LL_start_job_info *);
	int ll_start_job_ext(LL_start_job_info_ext *ptr);
	int ll_terminate_job(LL_terminate_job_info *);
	int ll_get_jobs(LL_get_jobs_info *);
	int ll_free_jobs(LL_get_jobs_info *);
	int ll_get_nodes(LL_get_nodes_info *);
	int ll_free_nodes(LL_get_nodes_info *);
	int llsubmit(char *, char *, char *, LL_job *, int);
	int GetHistory(char*, int (*)(LL_job *), int);

	int ll_get_hostlist(struct JM_JOB_INFO *);
	int ll_start_host(char *, char *);

	LL_element *llpd_allocate(void);
	int ll_update(LL_element*,enum LL_Daemon);
	int ll_fetch(LL_element*,enum LLAPI_Specification, void *);

	LL_element *ll_query(enum QueryType);
	int ll_deallocate(LL_element *);
	int ll_set_request(LL_element *,enum QueryFlags,char **,enum DataFilter);
	int ll_reset_request(LL_element *);
	LL_element *ll_get_objs(LL_element *,enum LL_Daemon,char *,int *,int *);
	LL_element *ll_next_obj(LL_element *);
	int ll_free_objs(LL_element *);

	int ll_init_job(LL_element **);
	void ll_deallocate_job(LL_element *);
	int ll_parse_string(LL_element *,char *,LL_element **,int,char *,LL_element **);
	int ll_parse_file(LL_element *,char *,LL_element **,int,char *,LL_element **);
	int ll_parse_verify(LL_element *,LL_element *,LL_element **);
	int ll_request(LL_element *,LL_element *);
	int ll_spawn(LL_element *,LL_element *,LL_element *,char *);
	enum EventType ll_event(LL_element *,int,LL_element **,LL_element *);
	int ll_get_job(LL_element *,LL_element **);
	int ll_close(LL_element *);
	int ll_get_data(LL_element *,enum LLAPI_Specification, void *);
	int ll_set_data(LL_element *,enum LLAPI_Specification, void *);
	char* ll_version(void);
	int ll_control(int, enum LL_control_op, char **, char **, char **, char **, int);
	int ll_spawn_task(LL_element *,LL_element *,char *, LL_element *, int flags);

#if !defined(__linux__) || defined(LL_LINUX_CKPT)
	int ll_ckpt(LL_ckpt_info *);
#endif /* !__linux__ || LL_LINUX_CKPT */

	int ll_modify(int, LL_element **, LL_modify_param **, char **);
	int ll_run_scheduler(int, LL_element **);
	int ll_preempt(int, LL_element **, char *, enum LL_preempt_op);
	int ll_preempt_jobs(int, LL_element **, LL_preempt_param **);
	int ll_move_job(int, LL_element **, LL_move_job_param **);
	int ll_cluster_auth(int, LL_element **, LL_cluster_auth_param **);
	int ll_cluster(int, LL_element **, LL_cluster_param *);

#if !defined(__linux__) || defined(LL_LINUX_CKPT)
	enum CkptStart ll_local_ckpt_start(time_t *);
	time_t ll_local_ckpt_complete(int, time_t, int);
	void ckpt();			/* in support of old checkpoint code */
	int ll_init_ckpt(LL_ckpt_info *);
	time_t ll_ckpt_complete(LL_element *, int, cr_error_t *, time_t, int);
	int ll_set_ckpt_callbacks(callbacks_t *);
	int ll_unset_ckpt_callbacks(int);
#endif /* !__linux__ || LL_LINUX_CKPT */
	int ll_task_inst_pid_update(int*, int);

	char *ll_error(LL_element **, int);
	int ll_make_reservation(int, LL_element **, LL_reservation_param **);
	int ll_change_reservation(int, LL_element **, char **, LL_reservation_change_param **);
	int ll_bind(int, LL_element **, LL_bind_param **);
	int ll_remove_reservation(int, LL_element **, char **, char **, char **, char**);
	int ll_init_reservation_param(int, LL_element **, LL_reservation_param **);
	int ll_spawn_connect(int, LL_element *,LL_element *,LL_element *,char *, LL_element **);
	int ll_spawn_write(int, int, LL_element *,LL_element **);
	int ll_spawn_read(int, int, LL_element *,LL_element **);

#ifdef __cplusplus
}
#endif /* __cpluplus */

#endif /* _llapi_h__ */

/*
 *	Convex SPP
 *	Copyright 1995 Convex Computer Corp.
 *	$CHeader: cnxQuerySC.c 1.3 1995/11/08 12:45:25 $
 *
 *	Function:	- SPP system queries
 */

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <errno.h>
#include <sys/cnx_pattr.h>
#include <sys/cnx_types.h>
#include <sys/cnx_sysinfo.h>

/*
 * global variables
 */
int			cnx_yield = 0;		/* yield while spinning? */
int			cnx_touch = 0;		/* touch shmem pages? */
int			cnx_debug = 0;		/* print debug info? */
char			*cnx_exec = 0;		/* tool to exec */

/*
 * local functions
 */
static cnx_scid_t	getSubcomplexId();
static void		displayTopologyProblem();
static void		flag_argstr();
static void		getNumCPUsPerNode();
static void		getNumNodesCpusInSubcomplex();

/*
 *	MPID_SHMEM_getNodeId
 *
 *	Function:	- get hypernode ID
 *	Returns:	- node ID
 */
cnx_node_t
MPID_SHMEM_getNodeId()

{
	cnx_is_target_data_t			target;
	cnx_is_thread_location_info_data_t	threadLocInfo;

	cnx_sysinfo_target_thread(&target, getpid(), cnx_thread_self());
	if (cnx_sysinfo(CNX_IS_THREAD_LOCATION_INFO, &target, &threadLocInfo,
			1, CNX_IS_THREAD_LOCATION_INFO_COUNT, NULL) == -1) {
		perror("MPID_SHMEM_getNodeId (cnx_sysinfo)");
		exit(1);
	}

	return(threadLocInfo.node);
}

/*
 *	MPID_SHMEM_getSCTopology
 *
 *	Function:	- get subcomplex topology information
 *	Accepts:	- ptr hypernode ID (returned value)
 *			- ptr # nodes (returned value)
 *			- ptr total # CPUs (returned value)
 *			- array # CPUs per node (returned value)
 */
void
MPID_SHMEM_getSCTopology(myNode, numNodes, totalCPUs, numCPUs)

cnx_node_t		*myNode;
unsigned int		*numNodes;
unsigned int		*totalCPUs;
unsigned int		*numCPUs;

{
	cnx_scid_t	myScId;

	myScId = getSubcomplexId();
	*myNode = MPID_SHMEM_getNodeId();
	getNumNodesCpusInSubcomplex(myScId, numNodes, totalCPUs);
	getNumCPUsPerNode(myScId, *numNodes, numCPUs);
}

/*
 *	getSubcomplexId()
 *
 *	Function:	- get subcomplex ID
 *	Returns:	- subcomplex ID
 */
static cnx_scid_t
getSubcomplexId()

{
	struct cnx_pattributes pa;		/* proc. attributes */

	if (cnx_getpattr(getpid(), CNX_PATTR_SCID, &pa) == -1) {
		perror("cnx_getpattr");
		exit(1);
	}

	return (pa.pattr_scid);
}

/*
 *	getNumNodesCpusInSubcomplex
 *
 *	Function:	- get subcomplex #nodes and #CPUs
 *	Accepts:	- subcomplex ID
 *			- ptr #nodes (returned value)
 *			- ptr #CPUs (returned value)
 */
static void
getNumNodesCpusInSubcomplex(myScId, numNodes, totalCPUs)

cnx_scid_t		myScId;
unsigned int		*numNodes;
unsigned int		*totalCPUs;

{
	cnx_is_target_data_t		target;
	cnx_is_sc_basic_info_data_t	scBasicInfo;

	cnx_sysinfo_target_subcomplex(&target, myScId);
	if (cnx_sysinfo(CNX_IS_SC_BASIC_INFO, &target, &scBasicInfo,
			1, CNX_IS_SC_BASIC_INFO_COUNT, NULL) == -1) {
		perror("cnx_sysinfo");
		exit(1);
	}

	*numNodes = scBasicInfo.node_count;
	*totalCPUs = scBasicInfo.cpu_count;
}

/*
 *	getNumCPUsPerNode
 *
 *	Function:	- get # CPUs on each node
 *	Accepts:	- subcomplex ID
 *			- # nodes
 *			- array of CPU counts (returned values)
 */
static void
getNumCPUsPerNode(myScId, numNodes, numCPUs)

cnx_scid_t		myScId;
unsigned int		numNodes;
unsigned int		*numCPUs;

{
	cnx_is_target_data_t		target;
	cnx_is_scnode_basic_info_data_t	*scNodeBasicInfo;
	int				i;

	cnx_sysinfo_target_scnode(&target, myScId, CNX_IS_ALL_NODES);
	scNodeBasicInfo = (cnx_is_scnode_basic_info_data_t *)
		malloc(numNodes * sizeof(cnx_is_scnode_basic_info_data_t));
	if (scNodeBasicInfo == 0) {
		perror("getNumCPUsPerNode (malloc)");
		exit(1);
	}

	if (cnx_sysinfo(CNX_IS_SCNODE_BASIC_INFO, &target, scNodeBasicInfo,
		numNodes, CNX_IS_SCNODE_BASIC_INFO_COUNT, NULL) == -1) {

		perror("getNumCPUsPerNode (cnx_sysinfo)");
		exit(1);
	}

	for (i = 0; i < numNodes; ++i) {
		numCPUs[i] = scNodeBasicInfo[i].num_cpus;
	}

	free(scNodeBasicInfo);
}

/*
 *	displayTopologyProblem
 *
 *	Function:	- print information on topology problem
 *	Accepts:	- # nodes
 *			- array # CPUs per node
 *			- requested # nodes
 *			- array requested  # CPUs per node
 *			- oversubscription flag
 */
static void
displayTopologyProblem(numNodes, numCPUs, reqNumNodes, reqNumCPUs, oversub)

int			numNodes;
int			*numCPUs;
int			reqNumNodes;
int			*reqNumCPUs;
int			oversub;

{
	int		i;

	printf("Topology Mismatch Problem:\n");
	printf("subcomplex topology: ");
	for (i = 0; i < numNodes; ++i) {
		printf("%d", numCPUs[i]);
		if (i < (numNodes - 1)) printf(",");
	}

	printf("\nrequested topology: ");
	for (i = 0; i < reqNumNodes; ++i) {
		printf("%d", reqNumCPUs[i]);
		if (i < (reqNumNodes - 1)) printf(",");
	}
	printf("\n");

	if (numNodes < reqNumNodes) {
		fprintf(stderr, "node mismatch: %d requested, %d available\n",
				reqNumNodes, numNodes);
	}

	if ( ! oversub) {
		for (i = 0; i < reqNumNodes; ++i) {
			if (numCPUs[i] < reqNumCPUs[i]) {
				fprintf(stderr,
"cpu mismatch on node %d: %d requested, %d available\n",
					i, reqNumCPUs[i], numCPUs[i]);
			}
		}
	}
}

/*
 *	MPID_SHMEM_processTopologyInfo
 *
 *	Function:	- initialize topology information
 *	Accepts:	- MPI_TOPOLOGY value (or NULL)
 *			- my node ID
 *			- ptr # processes (must be already set, doesn't change)
 *			- # nodes
 *			- array of CPU counts (CPUs per node)
 *			- allow oversubscription flag
 */
void
MPID_SHMEM_processTopologyInfo(envVarBuf, mynode, np,
					numNodes, numCPUs, oversub)

char			*envVarBuf;
int			mynode;
int			*np;
int			numNodes;
int			*numCPUs;
int			oversub;

{
	char		*p;			/* favourite pointer */
	int		i;			/* favourite index */
	int		nprocs;			/* process count */
	int		badTopology;		/* bad topology flag */
	int		reqNumNodes;		/* # nodes requested */
	int		reqNumProcs;		/* # procs requested */
	int		reqNumCPUs[CNX_MAX_NODES];
						/* #CPUs/node requested */

	if ((np == 0) || (*np < 1)) {
		fprintf(stderr, "invalid number of processes\n");
		exit(1);
	}
/*
 * If a topology is given, parse it.
 */
	if (envVarBuf) {
		i = 0;

		while (*envVarBuf && (i < CNX_MAX_NODES)) {

			nprocs = (int) strtol(envVarBuf, &p, 0);

			if (nprocs < 0) {
				fprintf(stderr,
			"MPI_TOPOLOGY has a negative # CPUs (@ node %d): %d\n",
					i, nprocs);
				exit(1);
			}

			if ((p == envVarBuf) || ((*p != ',') && *p)) {
				fprintf(stderr,
"cannot parse MPI_TOPOLOGY (@ node %d): %s\n",
					i, envVarBuf);
				exit(1);
			}

			reqNumCPUs[i++] = nprocs;
			envVarBuf = p;
			if (*p) ++envVarBuf;
		}

		if ((i == CNX_MAX_NODES) && envVarBuf) {
			fprintf(stderr,
				"MPI_TOPOLOGY out of [1 - %d] range\n",
				CNX_MAX_NODES);
			exit(1);
		}

		reqNumNodes = i;

		for (reqNumProcs = 0, i = 0; i < reqNumNodes; i++)
			reqNumProcs += reqNumCPUs[i];
	}
/*
 * Otherwise create a default topology by bin-packing.
 */
	else {
		reqNumNodes = numNodes;
		nprocs = reqNumProcs = *np;
		memset((char *) reqNumCPUs, 0, sizeof(reqNumCPUs));
/*
 * Loop filling nodes round-robin style, starting from the master's.
 */
		while (nprocs > 0) {
			i = (numCPUs[mynode] < nprocs) ?
						numCPUs[mynode] : nprocs;
			reqNumCPUs[mynode] += i;
			nprocs -= i;
			mynode = (mynode + 1) % numNodes;
		}
	}
/*
 * more sanity checks
 */
	if (*np != reqNumProcs) {
		fprintf(stderr,
"process mismatch: -np %d != %d set in MPI_TOPOLOGY: please reconcile\n",
				*np, reqNumProcs);
		exit(1);
	}

	*np = reqNumProcs;
	badTopology = 0;
	if (numNodes < reqNumNodes) badTopology = 1;

	if ( ! oversub) {
		for (i = 0; i < reqNumNodes; ++i) {
			if (numCPUs[i] < reqNumCPUs[i]) {
				badTopology = 1;
				break;
			}
		}
	}

	if (badTopology) {
		displayTopologyProblem(numNodes, numCPUs, reqNumNodes,
					reqNumCPUs, oversub);
		exit(1);
	}

	for (i = 0; i < reqNumNodes; ++i) numCPUs[i] = reqNumCPUs[i];
	while (i < numNodes) numCPUs[i++] = 0;
}

/*
 *	MPID_SHMEM_setflags
 *
 *	Function:	- parse MPI_FLAGS and set flags
 *			- MPI_FLAGS: <flag>,<flag>,...
 *			- aborts if error
 *			- current valid flags:
 *				d		debug info
 *				etool		exec "tool"
 *				t		touch pages
 *				y		spin yield
 */
void
MPID_SHMEM_setflags()

{
	char		*flags;			/* MPI_FLAGS string */
	char		k;			/* favourite character */

	flags = getenv("MPI_FLAGS");
	if (flags == 0) return;

	while (k = *flags) {

		switch(k) {	
/*
 * simple flags
 */
		case 'd': cnx_debug = 1; break;
		case 't': cnx_touch = 1; break;
		case 'y': cnx_yield = 1; break;
/*
 * flags with arguments
 */
		case 'e': flag_argstr('e', &cnx_exec, &flags); break;
/*
 * invalid flags
 */
		default:
		fprintf(stderr, "MPI_FLAGS: invalid flag '%c' (0x%2x)\n",
				k, (unsigned) k & 0xFF);
		exit(1);
		}
/*
 * Must have comma or end-of-string.
 */
		++flags;
		if ((flags[0] == ',') && (flags[1] != '\0')) {
			++flags;
		} else if (*flags != '\0') {
			fprintf(stderr,
				"MPI_FLAGS: syntax error '%s'\n", flags);
			exit(1);
		}
	}
}

/*
 *	flag_argstr
 *
 *	Function:	- set the flag string argument
 *			- aborts if error
 *	Accepts:	- flag letter
 *			- ptr string (returned value)
 *			- ptr MPI flags (modified value)
 */
static void
flag_argstr(let, parg, pflags)

int			let;
char			**parg;
char			**pflags;

{
	char		*p;			/* favourite pointer */
	int		i;			/* favourite counter */

	for (p = *pflags + 1, i = 0; *p && (*p != ','); ++p, ++i);

	if (i == 0) {
		fprintf(stderr,
			"MPI_FLAGS: '%c' flag missing argument\n", let);
		exit(1);
	}

	if (*parg) free(*parg);

	*parg = malloc((unsigned) i + 1);
	if (*parg == 0) {
		perror("flag_argstr (malloc)");
		exit(errno);
	}

	memcpy(*parg, *pflags + 1, i);
	(*parg)[i] = '\0';
	*pflags += i;
}

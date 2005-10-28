/* emulate work of poe to test this shared library without poe */

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "llapi.h"

#define DEFAULT_NODES 1

#define JCL \
"#@ job_type = parallel\n"			\
"#@ environment = COPY_ALL\n"			\
"#@ requirements = (Feature == \"debug\")\n"	\
"#@ node = %d\n"					\
"#@ total_tasks = %d\n"				\
"#@ node_usage = not_shared\n"			\
"#@ network.mpi = en0,shared,ip\n"		\
"#@ class = %s\n"				\
"#@ account_no = %s\n"				\
"#@ queue\n"
	
static void	_do_io(int fd);
static void	_do_node_work(LL_element *node_elem_ptr,
			LL_element *job_init_elem_ptr, 
			LL_element *step_elem_ptr);
static void	_do_step_work(LL_element *step_elem_ptr,
			LL_element *job_init_elem_ptr);
static void	_do_task_work(LL_element *task_elem_ptr,
			LL_element *job_init_elem_ptr, 
			LL_element *step_elem_ptr);
static void	_do_taski_work(LL_element *task_inst_ptr,
			LL_element *job_init_elem_ptr, 
			LL_element *step_elem_ptr);
static void	_run_test(int nodes, int tasks);
static int	_plugin_get_syms(void *dl_fd, int n_syms, 
			const char *names[], void *ptrs[]);

struct {
	int (*ll_close)(LL_element *);
	int (*ll_deallocate)(LL_element*);
	void (*ll_deallocate_job)(LL_element *);
	enum EventType (*ll_event)(LL_element *,int,LL_element **,LL_element *);
	int (*ll_fetch)(LL_element*, enum LLAPI_Specification, void *);
	int (*ll_free_objs)(LL_element *);
	int (*ll_get_data)(LL_element *,enum LLAPI_Specification, void *);
	LL_element *(*ll_get_objs)(LL_element *,enum LL_Daemon,char *,int *,int *); 
	int (*ll_init_job)(LL_element **);
	LL_element *(*ll_query)(enum QueryType);
	int (*ll_request)(LL_element *,LL_element *);
	int (*ll_parse_string)(LL_element *,char *,LL_element **,
			int,char *,LL_element **);
	int (*ll_parse_verify)(LL_element *,LL_element *,LL_element **);
	int (*ll_set_data)(LL_element *,enum LLAPI_Specification, void *);
	int (*ll_set_request)(LL_element *,enum QueryFlags,char **,enum DataFilter);
	int (*ll_spawn_task)(LL_element *,LL_element *,char *, LL_element *, int flags);
	char *(*ll_version)(void);
} llapi_ops;

int main(int argc, char **argv)
{
	void *dl_fd;
	/* syms order must be identical to that of llapi_ops */
	const char *syms[] = {
		"ll_close",
		"ll_deallocate",
		"ll_deallocate_job",
		"ll_event",
		"ll_fetch",
		"ll_free_objs",
		"ll_get_data",
		"ll_get_objs",
		"ll_init_job",
		"ll_query",
		"ll_request",
		"ll_parse_string",
		"ll_parse_verify",
		"ll_set_data",
		"ll_set_request",
		"ll_spawn_task",
		"ll_version"
	};
	int nsyms = sizeof(syms) / sizeof(char *);
	int i, nodes, tasks;

	dl_fd = dlopen("./llapi_shr.o", RTLD_NOW);
	if (dl_fd == NULL) {
		fprintf(stderr, "dlopen error\n");
		exit(1);
	}

	i = _plugin_get_syms(dl_fd, nsyms, syms, (void *)&llapi_ops);
	if (i != nsyms) {
		fprintf(stderr, "loaded %d of %d symbols\n", i, nsyms);
		exit(1);
	}

	if (argc > 1) {
		nodes = atoi(argv[1]);
		if (nodes < 1) {
			fprintf(stderr, "Invalid node count\n");
			fprintf(stderr, "Usage: %s [nodes] [tasks]\n", 
				argv[0]);
			exit(1);
		}
	} else
		nodes = DEFAULT_NODES;

	if (argc > 2) {
		tasks = atoi(argv[2]);
		if (tasks < 1) {
			fprintf(stderr, "Invalid task count\n");
			fprintf(stderr, "Usage: %s [nodes] [tasks]\n",
				argv[0]);
			exit(1);
		}
	} else
		tasks = nodes;

	_run_test(nodes, tasks);

	dlclose(dl_fd);
	return 0;
}

static int _plugin_get_syms(void *dl_fd, int n_syms, 
		const char *names[], void *ptrs[])
{
	int i, count = 0;
	for (i=0; i<n_syms; i++) {
		ptrs[i] = dlsym(dl_fd, names[i]);
		if (ptrs[i]) count++;
	}
	return count;
}

static void _run_test(int nodes, int tasks)
{
	char step_list[1024];
	LL_element *job_init_elem_ptr = NULL;
	LL_element *job_elem_ptr = NULL;
	LL_element *error_elem_ptr = NULL;
	LL_element *step_elem_ptr = NULL;
	LL_element *switch_elem_ptr = NULL;
	LL_element *job_ptr = NULL;
	LL_element *cluster_elem_ptr = NULL;
	LL_element *cluster_query_elem_ptr = NULL;
	char *acct_no, *class, *step_id, *sched_type;
	char jobstring[1024];
	int session_type = INTERACTIVE_SESSION;
	int step_immediate = 1;
	int step_state, count, err;
	int rc = 0;

	/*
	 * Setup
	 */
	(llapi_ops.ll_fetch)(NULL, 0, NULL);
	(llapi_ops.ll_version)();
	(llapi_ops.ll_init_job)(&job_init_elem_ptr);
	rc = (llapi_ops.ll_set_data)(job_init_elem_ptr, 
				LL_JobManagementSessionType, 
				(void *)session_type);
	if (rc < 0) goto done;

	rc = (llapi_ops.ll_get_data)(job_init_elem_ptr, 
				LL_JobManagementInteractiveClass,
				&class);
	if (rc < 0) goto done;

	rc = (llapi_ops.ll_get_data)(job_init_elem_ptr, 
				LL_JobManagementAccountNo,
				&acct_no);
	if (rc < 0) goto done;

	snprintf(jobstring, sizeof(jobstring), JCL, nodes, tasks, 
		class, acct_no);
	(llapi_ops.ll_parse_string)(job_init_elem_ptr,
				    jobstring, &job_elem_ptr, 0, NULL, 
				    &error_elem_ptr);
	rc = (llapi_ops.ll_get_data)(job_init_elem_ptr,
				LL_JobGetFirstStep,
				&step_elem_ptr);
	if (rc < 0) goto done;

	rc = (llapi_ops.ll_get_data)(step_elem_ptr,
				LL_StepID, &step_id);
	if (rc < 0) goto done;

	(llapi_ops.ll_set_data)(step_elem_ptr,
				LL_StepImmediate,
				(void *)step_immediate);
	(llapi_ops.ll_parse_verify)(job_init_elem_ptr, NULL, NULL);

	/*
	 * Make job request
	 */
	(llapi_ops.ll_request)(job_init_elem_ptr, job_init_elem_ptr);

	/*
	 * Get step state info
	 */
	(llapi_ops.ll_event)(job_init_elem_ptr, 10000, &job_ptr, 
				step_list);
	(llapi_ops.ll_get_data)(job_init_elem_ptr, LL_JobGetFirstStep, 
				&step_elem_ptr);
	(llapi_ops.ll_get_data)(step_elem_ptr, LL_StepState, &step_state);
	if (step_state != STATE_RUNNING)
		goto done;

	/*
	 * Get system info
	 */
	cluster_elem_ptr = (llapi_ops.ll_query)(CLUSTERS);
	(llapi_ops.ll_set_request)(cluster_elem_ptr, MACHINES, 
				(char **)NULL, QUERY_ALL);
	cluster_query_elem_ptr = (llapi_ops.ll_get_objs)(cluster_elem_ptr,
				LL_STARTD, NULL, &count, &err);
	(llapi_ops.ll_get_data)(cluster_query_elem_ptr, 
				LL_ClusterSchedulerType, &sched_type);
	(llapi_ops.ll_free_objs)(cluster_query_elem_ptr);
	(llapi_ops.ll_deallocate)(cluster_elem_ptr);

	/*
	 * Get step info
	 */
	(llapi_ops.ll_get_data)(step_elem_ptr, LL_StepGetFirstSwitchTable,
				&switch_elem_ptr);

	/*
	 * Loop over nodes, tasks, task instances, and adapters and do I/O
	 */
	_do_step_work(step_elem_ptr, job_init_elem_ptr);

done:	if (rc)
		fprintf(stderr, "Some job error occurred\n");
	(llapi_ops.ll_close)(job_init_elem_ptr);
	(llapi_ops.ll_deallocate_job)(job_init_elem_ptr);
}

static void _do_step_work(LL_element *step_elem_ptr, 
		LL_element *job_init_elem_ptr)
{
	int rc, node_cnt, node_inx;
	LL_element *node_elem_ptr = NULL;

	rc = (llapi_ops.ll_get_data)(step_elem_ptr, LL_StepNodeCount, 
		&node_cnt);
	if (rc < 0) goto done;

	for (node_inx=0; node_inx<node_cnt; node_inx++) {
		if (node_inx) {
			rc = (llapi_ops.ll_get_data)(step_elem_ptr, 
				LL_StepGetNextNode, &node_elem_ptr);
		} else {
			rc = (llapi_ops.ll_get_data)(step_elem_ptr,
				LL_StepGetFirstNode, &node_elem_ptr);
		}
		if (rc < 0) goto done;
		_do_node_work(node_elem_ptr, job_init_elem_ptr, step_elem_ptr);
	}

done:	if (rc)
		fprintf(stderr, "Some step error occurred\n");
	return;
}

static void _do_node_work(LL_element *node_elem_ptr,
		LL_element *job_init_elem_ptr, LL_element *step_elem_ptr)
{
	int rc, task_cnt, task_inx;
	LL_element *task_elem_ptr = NULL;

	rc = (llapi_ops.ll_get_data)(node_elem_ptr, LL_NodeTaskCount, 
		&task_cnt);
	if (rc < 0) goto done;

	for (task_inx=0; task_inx<task_cnt; task_inx++) {
		if (task_inx) {
			rc = (llapi_ops.ll_get_data)(node_elem_ptr, 
				LL_NodeGetNextTask, &task_elem_ptr);
		} else {
			rc = (llapi_ops.ll_get_data)(node_elem_ptr, 
				LL_NodeGetFirstTask, &task_elem_ptr);
		}
		if (rc < 0) goto done;
		_do_task_work(task_elem_ptr, job_init_elem_ptr, step_elem_ptr);
	}

done:	if (rc)
		fprintf(stderr, "Some node error occurred\n");
	return;
}

static void _do_task_work(LL_element *task_elem_ptr,
		LL_element *job_init_elem_ptr, LL_element *step_elem_ptr)
{
	int rc, taski_cnt, taski_inx;
	LL_element *task_inst_ptr = NULL;

	rc = (llapi_ops.ll_get_data)(task_elem_ptr, LL_TaskTaskInstanceCount,
				&taski_cnt);
	if (rc < 0) goto done;

	for (taski_inx=0; taski_inx<taski_cnt; taski_inx++) {
		if (taski_inx) {
			rc = (llapi_ops.ll_get_data)(task_elem_ptr,
				LL_TaskGetFirstTaskInstance, &task_inst_ptr);
		} else {
			rc = (llapi_ops.ll_get_data)(task_elem_ptr, 
				LL_TaskGetFirstTaskInstance, &task_inst_ptr);
		}
		if (rc < 0) goto done;
		_do_taski_work(task_inst_ptr, job_init_elem_ptr, 
			step_elem_ptr);
	}

done:   if (rc)
		fprintf(stderr, "Some task error occurred\n");
	return;
}

static void _do_taski_work(LL_element *task_inst_ptr, 
		LL_element *job_init_elem_ptr, LL_element *step_elem_ptr)
{
	int rc, ti_id, ti_adapter_cnt, fd;
	char *machine_name;

	rc = (llapi_ops.ll_get_data)(task_inst_ptr, LL_TaskInstanceTaskID, 
			&ti_id);
	if (rc < 0) goto done;

	rc = (llapi_ops.ll_get_data)(task_inst_ptr, LL_TaskInstanceMachineName, 
			&machine_name);
	if (rc < 0) goto done;

	rc = (llapi_ops.ll_get_data)(task_inst_ptr, 
			LL_TaskInstanceAdapterCount, &ti_adapter_cnt);

	fd = (llapi_ops.ll_spawn_task)(job_init_elem_ptr, step_elem_ptr, 
				"/bin/hostname", task_inst_ptr, 0);
	if (fd >= 0)
		_do_io(fd);

done:	if (rc)
		fprintf(stderr, "Some task instance error occurred\n");
	return;
}

static void _do_io(int fd)
{
	int j, size;
	char buf[1024];

	while(1) {
		size = read(fd, buf, sizeof(buf));
		if (size > 0) {
			printf("read:size:%d:msg:", size);
			for (j=0; j<size; j++)
				printf("%c",buf[j]);
			printf("\n");
		} else if (size == 0) {
			printf("task:EOF\n");
			break;
		} else {
			perror("read");
			break;
		}
	}

	close(fd);
}


/* 
 * slurm.h - definitions for slurm api use
 *
 * NOTE: the job, node, and partition specifications are all of the 
 * same basic format:
 * if the first character of a line is "#" then it is a comment.
 * place all information for a single node, partition, or job on a 
 *    single line. 
 * space delimit collection of keywords and values and separate
 *    the keyword from value with an equal sign (e.g. "cpus=3"). 
 * list entries should be comma separated (e.g. "nodes=lx01,lx02").
 * 
 * see the slurm administrator guide for more details.
 */

#ifndef _HAVE_SLURM_H
#define _HAVE_SLURM_H

#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include "list.h"
#include "slurmlib.h"
#include "log.h"
#include "bitstring.h"
#include "xmalloc.h"

#define DEBUG_SYSTEM 1

#define BACKUP_INTERVAL		60
#define BACKUP_LOCATION		"/usr/local/slurm/slurm.state"
#define CONTROL_DAEMON  	"/usr/local/slurm/slurmd.control"
#define CONTROLLER_TIMEOUT 	300
#define EPILOG			""
#define HASH_BASE		10
#define HEARTBEAT_INTERVAL	60
#define INIT_PROGRAM		""
#define KILL_WAIT		30
#define	PRIORITIZE		""
#define PROLOG			""
#define SERVER_DAEMON   	"/usr/local/slurm/slurmd.server"
#define SERVER_TIMEOUT  	300
#define SLURM_CONF		"/g/g0/jette/slurm/etc/slurm.conf2"
#define TMP_FS			"/tmp"

/* NOTE: change BUILD_STRUCT_VERSION value whenever the contents of BUILD_STRUCT_FORMAT change */
#define BUILD_STRUCT_VERSION 1
#define HEAD_FORMAT "#time=%lu version=%d\n"
#define BUILD_STRUCT_FORMAT  "%s %s\n"
#define BUILD_STRUCT2_FORMAT "%s %d\n"

extern char *control_machine;	/* name of computer acting as slurm controller */
extern char *backup_controller;	/* name of computer acting as slurm backup controller */

/* NOTE: change JOB_STRUCT_VERSION value whenever the contents of "struct job_record" 
 * change with respect to the api structures  */
#define JOB_STRUCT_VERSION 1
struct job_record {
	int job_id;
	int user_id;
	int max_time;		/* -1 if unlimited */
};

/* NOTE: change NODE_STRUCT_VERSION value whenever the contents of NODE_STRUCT_FORMAT change */
#define NODE_STRUCT_VERSION 1
#define NODE_STRUCT_FORMAT "NodeName=%s Atate=%s CPUs=%d RealMemory=%d TmpDisk=%d Weight=%d Feature=%s #Partition=%s\n"
#define CONFIG_MAGIC 0xc065eded
#define NODE_MAGIC   0x0de575ed
struct config_record {
	unsigned magic;		/* magic cookie to test data integrity */
	int cpus;		/* count of cpus running on the node */
	int real_memory;	/* megabytes of real memory on the node */
	int tmp_disk;		/* megabytes of total storage in TMP_FS file system */
	int weight;		/* arbitrary priority of node for scheduling work on */
	char *feature;		/* arbitrary list of features associated with a node */
	char *nodes;		/* names of nodes in partition configuration record */
	bitstr_t *node_bitmap;	/* bitmap of nodes in configuration record */
};
extern List config_list;	/* list of config_record entries */

/* last entry must be STATE_END, keep in sync with node_state_string    	*/
/* any value less than or equal to zero is down. if a node was in state 	*/
/* STATE_BUSY and stops responding, its state becomes -(STATE_BUSY), etc.	*/
enum node_state {
	STATE_DOWN,		/* node is not responding */
	STATE_UNKNOWN,		/* node's initial state, unknown */
	STATE_IDLE,		/* node idle and available for use */
	STATE_STAGE_IN,		/* node has been allocated to a job, which has not yet begun execution */
	STATE_BUSY,		/* node allocated to a job and that job is actively running */
	STATE_STAGE_OUT,	/* node has been allocated to a job, which has completed execution */
	STATE_DRAINED,		/* node idle and not to be allocated future work */
	STATE_DRAINING,		/* node in use, but not to be allocated future work */
	STATE_END
};				/* last entry in table */
/* last entry must be "end", keep in sync with node_state */
extern char *node_state_string[];

extern time_t last_bitmap_update;	/* time of last node creation or deletion */
extern time_t last_node_update;	/* time of last update to node records */
struct node_record {
	unsigned magic;		/* magic cookie to test data integrity */
	char name[MAX_NAME_LEN];/* name of the node. a null name indicates defunct node */
	int node_state;		/* state of the node, see node_state above, negative if down */
	time_t last_response;	/* last response from the node */
	int cpus;		/* actual count of cpus running on the node */
	int real_memory;	/* actual megabytes of real memory on the node */
	int tmp_disk;		/* actual megabytes of total storage in TMP_FS file system */
	struct config_record *config_ptr;	/* configuration specification for this node */
	struct part_record *partition_ptr;	/* partition for this node */
};
extern struct node_record *node_record_table_ptr;	/* location of the node records */
extern int node_record_count;		/* count of records in the node record table */
extern int *hash_table;			/* table of hashed indicies into node_record */
extern bitstr_t *up_node_bitmap;	/* bitmap of nodes are up */
extern bitstr_t *idle_node_bitmap;	/* bitmap of nodes are idle */
extern struct config_record default_config_record;
extern struct node_record default_node_record;

/* NOTE: change PART_STRUCT_VERSION value whenever the contents of PART_STRUCT_FORMAT change */
#define PART_STRUCT_VERSION 1
#define PART_STRUCT_FORMAT "PartitionName=%s MaxNodes=%d MaxTime=%d Nodes=%s Key=%s Default=%s AllowGroups=%s Shared=%s State=%s #TotalNodes=%d TotalCPUs=%d\n"
#define PART_MAGIC 0xaefe8495
extern time_t last_part_update;	/* time of last update to part records */
struct part_record {
	unsigned magic;		/* magic cookie to test data integrity */
	char name[MAX_NAME_LEN];/* name of the partition */
	int max_time;		/* -1 if unlimited */
	int max_nodes;		/* -1 if unlimited */
	int total_nodes;	/* total number of nodes in the partition */
	int total_cpus;		/* total number of cpus in the partition */
	unsigned key:1;		/* 1 if slurm distributed key is required for use of partition */
	unsigned shared:2;	/* 1 if more than one job can execute on a node, 2 if required */
	unsigned state_up:1;	/* 1 if state is up, 0 if down */
	char *nodes;		/* names of nodes in partition */
	char *allow_groups;	/* null indicates all */
	bitstr_t *node_bitmap;	/* bitmap of nodes in partition */
};
extern List part_list;		/* list of part_record entries */
extern struct part_record default_part;	/* default configuration values */
extern char default_part_name[MAX_NAME_LEN];	/* name of default partition */
extern struct part_record *default_part_loc;	/* location of default partition */

/*
 * bitmap2node_name - given a bitmap, build a node name list representation using 
 * 	regular expressions
 * input: bitmap - bitmap pointer
 *        node_list - place to put node list
 * output: node_list - set to node list or null on error 
 *         returns 0 if no error, otherwise einval or enomem
 * NOTE: consider returning the node list as a regular expression if helpful
 * NOTE: the caller must free memory at node_list when no longer required
 */
extern int bitmap2node_name (bitstr_t *bitmap, char **node_list);

/*
 * create_config_record - create a config_record entry, append it to the config_list, 
 *	and set is values to the defaults in default_config_record.
 * input: error_code - pointer to an error code
 * output: returns pointer to the config_record
 *         error_code - set to zero if no error, errno otherwise
 * NOTE: the pointer returned is allocated memory that must be freed when no longer needed.
 */
extern struct config_record *create_config_record (int *error_code);

/* 
 * create_node_record - create a node record
 * input: error_code - location to store error value in
 *        config_point - pointer to node's configuration information
 *        node_name - name of the node
 * output: error_code - set to zero if no error, errno otherwise
 *         returns a pointer to the record or null if error
 * note the record's values are initialized to those of default_node_record, node_name and 
 *	config_point's cpus, real_memory, and tmp_disk values
 * NOTE: allocates memory that should be freed with delete_part_record
 */
extern struct node_record *create_node_record (int *error_code,
					       struct config_record
					       *config_point,
					       char *node_name);

/* 
 * create_part_record - create a partition record
 * input: error_code - location to store error value in
 * output: error_code - set to zero if no error, errno otherwise
 *         returns a pointer to the record or null if error
 * NOTE: the record's values are initialized to those of default_part
 */
extern struct part_record *create_part_record (int *error_code);

/* 
 * delete_node_record - delete record for node with specified name
 *   to avoid invalidating the bitmaps and hash table, we just clear the name 
 *   set its state to STATE_DOWN
 * input: name - name of the desired node 
 * output: return 0 on success, errno otherwise
 */
extern int delete_node_record (char *name);

/* 
 * delete_part_record - delete record for partition with specified name
 * input: name - name of the desired node 
 * output: return 0 on success, errno otherwise
 */
extern int delete_part_record (char *name);

/* 
 * dump_all_node - dump all configuration and node information to a buffer
 * input: buffer_ptr - location into which a pointer to the data is to be stored.
 *                     the data buffer is actually allocated by dump_node and the 
 *                     calling function must xfree the storage.
 *         buffer_size - location into which the size of the created buffer is in bytes
 *         update_time - dump new data only if partition records updated since time 
 *                       specified, otherwise return empty buffer
 * output: buffer_ptr - the pointer is set to the allocated buffer.
 *         buffer_size - set to size of the buffer in bytes
 *         update_time - set to time partition records last updated
 *         returns 0 if no error, errno otherwise
 * global: node_record_table_ptr - pointer to global node table
 * NOTE: the caller must xfree the buffer at *buffer_ptr when no longer required
 */
extern int  dump_all_node (char **buffer_ptr, int *buffer_size, time_t * update_time);

/* 
 * dump_all_part - dump all partition information to a buffer
 * input: buffer_ptr - location into which a pointer to the data is to be stored.
 *                     the data buffer is actually allocated by dump_part and the 
 *                     calling function must xfree the storage.
 *         buffer_size - location into which the size of the created buffer is in bytes
 *         update_time - dump new data only if partition records updated since time 
 *                       specified, otherwise return empty buffer
 * output: buffer_ptr - the pointer is set to the allocated buffer.
 *         buffer_size - set to size of the buffer in bytes
 *         update_time - set to time partition records last updated
 *         returns 0 if no error, errno otherwise
 * global: part_list - global list of partition records
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 */
extern int  dump_all_part (char **buffer_ptr, int *buffer_size, time_t * update_time);

/* 
 * dump_node - dump all configuration information about a specific node to a buffer
 * input:  dump_node_ptr - pointer to node for which information is requested
 *         out_line - buffer for node information 
 *         out_line_size - byte size of out_line
 * output: out_line - set to node information values
 *         return 0 if no error, 1 if out_line buffer too small
 * NOTE: if you make any changes here be sure to increment the value of NODE_STRUCT_VERSION
 *       and make the corresponding changes to load_node_config in api/node_info.c
 */
extern int  dump_node (struct node_record *dump_node_ptr, char *out_line, int out_line_size);

/* 
 * dump_part - dump all configuration information about a specific partition to a buffer
 * input:  dump_part_ptr - pointer to partition for which information is requested
 *         out_line - buffer for partition information 
 *         out_line_size - byte size of out_line
 * output: out_line - set to partition information values
 *         return 0 if no error, 1 if out_line buffer too small
 * NOTE: if you make any changes here be sure to increment the value of PART_STRUCT_VERSION
 *       and make the corresponding changes to load_part_config in api/partition_info.c
 */
extern int dump_part (struct part_record *part_record_point, char *out_line, int out_line_size);

/* 
 * find_node_record - find a record for node with specified name,
 * input: name - name of the desired node 
 * output: return pointer to node record or null if not found
 */
extern struct node_record *find_node_record (char *name);

/* 
 * find_part_record - find a record for partition with specified name,
 * input: name - name of the desired partition 
 * output: return pointer to node partition or null if not found
 */
extern struct part_record *find_part_record (char *name);

/* 
 * init_node_conf - initialize the node configuration values. 
 * this should be called before creating any node or configuration entries.
 * output: return value - 0 if no error, otherwise an error code
 */
extern int init_node_conf ();

/* 
 * init_part_conf - initialize the partition configuration values. 
 * this should be called before creating any partition entries.
 * output: return value - 0 if no error, otherwise an error code
 */
extern int init_part_conf ();


/* list_compare_config - compare two entry from the config list based upon weight, 
 * see list.h for documentation */
extern int list_compare_config (void *config_entry1, void *config_entry2);

/* list_delete_config - delete an entry from the configuration list, see list.h for documentation */
extern void list_delete_config (void *config_entry);

/* list_find_config - find an entry in the configuration list, see list.h for documentation 
 * key is partition name or "universal_key" for all configuration */
extern int list_find_config (void *config_entry, void *key);

/* list_delete_part - delete an entry from the partition list, see list.h for documentation */
extern void list_delete_part (void *part_entry);

/* list_find_part - find an entry in the partition list, see list.h for documentation 
 * key is partition name or "universal_key" for all partitions */
extern int list_find_part (void *part_entry, void *key);

/*
 * load_integer - parse a string for a keyword, value pair  
 * input: *destination - location into which result is stored
 *        keyword - string to search for
 *        in_line - string to search for keyword
 * output: *destination - set to value, no change if value not found, 
 *             set to 1 if keyword found without value, 
 *             set to -1 if keyword followed by "unlimited"
 *         in_line - the keyword and value (if present) are overwritten by spaces
 *         return value - 0 if no error, otherwise an error code
 * NOTE: in_line is overwritten, do not use a constant
 */
extern int load_integer (int *destination, char *keyword, char *in_line);

/*
 * load_string - parse a string for a keyword, value pair  
 * input: *destination - location into which result is stored
 *        keyword - string to search for
 *        in_line - string to search for keyword
 * output: *destination - set to value, no change if value not found, 
 *	     if *destination had previous value, that memory location is automatically freed
 *         in_line - the keyword and value (if present) are overwritten by spaces
 *         return value - 0 if no error, otherwise an error code
 * NOTE: destination must be free when no longer required
 * NOTE: if destination is non-null at function call time, it will be freed 
 * NOTE: in_line is overwritten, do not use a constant
 */
extern int load_string (char **destination, char *keyword, char *in_line);

/* node_lock - lock the node and configuration information */
extern void node_lock ();

/* node_unlock - unlock the node and configuration information */
extern void node_unlock ();

/*
 * node_name2bitmap - given a node name regular expression, build a bitmap representation
 * input: node_names - list of nodes
 *        bitmap - place to put bitmap pointer
 * output: bitmap - set to bitmap or null on error 
 *         returns 0 if no error, otherwise einval or enomem
 * NOTE: the caller must free memory at bitmap when no longer required
 */
extern int node_name2bitmap (char *node_names, bitstr_t **bitmap);

/* 
 * node_name2list - given a node name regular expression, build an 
 *	array of node names
 * input: node_names - list of nodes
 *	  node_list - location into which the list is placed
 *        node_count - location into which a node count is passed
 * output: node_list - an array of node names, each of size MAX_NAME_LEN
 *         node_count - the number of entries in node_list
 *         returns 0 if no error, otherwise EINVAL or enomem
 * NOTE: the caller must xfree memory at node_list when no longer required iff no error
 */
extern int  node_name2list (char *node_names, char **node_list, int *node_count);

/* part_lock - lock the partition information */
extern void part_lock ();

/* part_unlock - unlock the partition information */
extern void part_unlock ();

/* 
 * read_buffer - read a line from the specified buffer
 * input: buffer - pointer to read buffer, must be allocated by alloc()
 *        buffer_offset - byte offset in buffer, read location
 *        buffer_size - byte size of buffer
 *        line - pointer to location to be loaded with pointer to the line
 * output: buffer_offset - incremented by  size of size plus the value size itself
 *         line - set to pointer to the line
 *         returns 0 if no error or efault on end of buffer, einval on bad tag 
 */
extern int read_buffer (char *buffer, int *buffer_offset, int buffer_size,
			char **line);

/*
 * read_SLURM_CONF - load the slurm configuration from the specified file 
 * call init_SLURM_CONF before ever calling read_SLURM_CONF.  
 * read_SLURM_CONF can be called more than once if so desired.
 * input: file_name - name of the file containing slurm configuration information
 * output: return - 0 if no error, otherwise an error code
 */
extern int read_SLURM_CONF (char *file_name);

/* 
 * report_leftover - report any un-parsed (non-whitespace) characters on the
 * configuration input line.
 * input: in_line - what is left of the configuration input line.
 *        line_num - line number of the configuration file.
 * output: none
 */
extern void report_leftover (char *in_line, int line_num);

/* 
 * update_node - update the configuration data for one or more nodes
 * input: node_names - node names, may contain regular expression
 *        spec - the updates to the node's specification 
 * output:  return - 0 if no error, otherwise an error code
 */
extern int update_node (char *node_names, char *spec);

/* 
 * update_part - update a partition's configuration data
 * input: partition_name - partition's name
 *        spec - the updates to the partition's specification 
 * output:  return - 0 if no error, otherwise an error code
 * NOTE: the contents of spec are overwritten by white space
 */
extern int update_part (char *partition_name, char *spec);

/*
 * validate_node_specs - validate the node's specifications as valid, 
 *   if not set state to down, in any case update last_response
 * input: node_name - name of the node
 *        cpus - number of cpus measured
 *        real_memory - mega_bytes of real_memory measured
 *        tmp_disk - mega_bytes of tmp_disk measured
 * output: returns 0 if no error, enoent if no such node, einval if values too low
 */
extern int validate_node_specs (char *node_name,
				int cpus, int real_memory, int tmp_disk);

/* 
 * write_buffer - write the specified line to the specified buffer, 
 *               enlarging the buffer as needed
 * input: buffer - pointer to write buffer, must be allocated by alloc()
 *        buffer_offset - byte offset in buffer, write location
 *        buffer_size - byte size of buffer
 *        line - pointer to data to be writen
 * output: buffer - value is written here, buffer may be relocated by realloc()
 *         buffer_offset - incremented by value_size
 *         returns 0 if no error or errno otherwise 
 */
extern int write_buffer (char **buffer, int *buffer_offset, int *buffer_size,
			 char *line);

#endif /* !_HAVE_SLURM_H */

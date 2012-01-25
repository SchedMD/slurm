/* This should be the same as IBM's nrt.h file (or close anyway) */

#ifndef _NRT_INCLUDED
#define _NRT_INCLUDED

#include <stdint.h>
#define MAX_SPIGOTS		4
#define NRT_MAX_DEVICENAME_SIZE	6
#define NRT_VERSION		100

#define NRT_SUCCESS		0
#define NRT_ALREADY_LOADED	15
#define NRT_BAD_VERSION		10
#define NRT_EADAPTER		4
#define NRT_EADAPTYPE		9
#define NRT_EAGAIN		11
#define NRT_EINVAL		1
#define NRT_EIO			7
#define NRT_EMEM		6
#define NRT_EPERM		2
#define NRT_ESYSTEM		5
#define NRT_NO_RDMA_AVAIL	8
#define NRT_PNSDAPI		2
#define NRT_UNKNOWN_ADAPTER	13
#define NRT_WRONG_WINDOW_STATE	12

typedef struct {
	uint32_t	node_number;			// to form unique ID
	uint8_t		num_spigots;			//Ports for IB, spigots for HCPE
	uint8_t		padding1[3];			//future use
	uint16_t	lid[MAX_SPIGOTS];		//(IB only) Logical ID of each spigot
	uint64_t 	network_id[MAX_SPIGOTS];	//Network ID for each adapter spigot
	uint8_t		lmc[MAX_SPIGOTS];		//(IB only) Logical mask of each spigot
	uint8_t		spigot_id[MAX_SPIGOTS];		//Port/Spigot IDs
	uint16_t	window_count;			//Count of window in window_list
	uint8_t 	padding2[6];			//future use
	uint16_t	*window_list;			//array of available windows
	uint64_t	rcontext_block_count;		//available rcxt blocks
} adap_resources_t;


typedef enum { NRT_WIN_UNAVAILABLE,	//Initialization in progress
		NRT_WIN_INVALID,	// Not a usable window
		NRT_WIN_AVAILABLE,	// Ready for NRT load
		NRT_WIN_RESERVED,	//Window reserved, NRT not loaded
					//	(for POE-PMD)
		NRT_WIN_READY,		//NRT loaded
		NRT_WIN_RUNNING		//Window is running
} win_state_t;

typedef struct {
	pid_t		client_pid;		// Pid of process that loaded
	uid_t 		uid;			// Uid using the window
	uint16_t 	window_id;		// Window being reported
	uint16_t	bulk_transfer;		// Is this lead using RDMA?
	uint32_t	rcontext_blocks;	// rcontexts per window
	win_state_t 	state;			// Window state
	uint8_t		padding[4];
} nrt_status_t;

typedef enum { LEAVE_IN_USE, KILL } clean_option_t;


int nrt_adapter_resources(int version, char *adapter_device_string,
			          uint16_t adapter_type,
			          adap_resources_t *adapter_infor_OUT);

int nrt_clean_window (int version, char *adapter_or_string,
			uint16_t adapter_type,
			clean_option_t  leave_inuse_or_kill,
			ushort window_id);


typedef struct {
	uint16_t 	task_id;
	uint16_t 	win_id;
	uint32_t 	node_number;
	char 		device_name[NRT_MAX_DEVICENAME_SIZE];
	uint16_t 	base_lid;
	uint8_t 	port_id;
	uint8_t		lmc;
	uint8_t 	port_status;  //ignored
	uint8_t 	padding[3];
} nrt_creator_ib_per_task_input_t;

typedef struct {
	uint16_t 	task_id;
/* FIXME: We have no idea what this should contain */
} nrt_creator_hpce_per_task_input_t;

typedef union {
	nrt_creator_hpce_per_task_input_t hpce_per_task;
	nrt_creator_ib_per_task_input_t        ib_per_task;
} nrt_creator_per_task_input_t;

int nrt_load_table_rdma (int version, char *adapter_or_string,
			uint16_t adapter_type,
			uint64_t network_id, uid_t uid, pid_t pid,
			ushort job_key, char *job_description,
			uint use_bulk_transfer,
			uint bulk_transfer_resources, int table_size,
			nrt_creator_per_task_input_t *per_task_input);


int nrt_rdma_jobs(int version, char *adapter_device_string,
		      uint16_t adapter_type, uint16_t *job_count,
		      uint16_t **job_keys);


int nrt_status_adapter (int version, char *adapter_device_string,
			uint16_t adapter_type, uint16_t *window_count,
			nrt_status_t ** status_array);


int nrt_upload_window 	(int version, char * adapter_device_string,
				uint16_t adapter_type, ushort job_key,
				ushort window_id);




int nrt_version(void);
#endif

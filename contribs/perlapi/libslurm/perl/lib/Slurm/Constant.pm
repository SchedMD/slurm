package Slurm::Constant;
use strict;
use warnings;
use Carp;

my %const;
my $got = 0;

no warnings 'portable';

sub _get_constants {
    seek(DATA, 0, 0);
    local $/=''; # paragraph mode
    local $_;
    while(<DATA>) {
	next unless /^=item\s+\*\s+(\S+)\s+(\S+)\s*$/;
	my ($name, $val) = ($1,$2);
	if ($val =~ /^0x/) {
	    $val = hex($val);
	} else {
	    $val = int($val);
	}
	$const{$name} = sub { $val };
    }
    $got = 1;
}

sub import {
    my $pkg = shift;
    my $callpkg = caller(0);
    croak "Please use `use Slurm qw(:constant)' instead of `use Slurm::Constant'."
	unless $callpkg eq "Slurm";
    _get_constants() unless $got;
    {
	no strict "refs";
	my ($sym, $sub);
	while (($sym, $sub) = each(%const)) {
	    *{$callpkg . "::$sym"} = $sub;
	}
    }
}

sub import2 {
    my $pkg = shift;
    my $callpkg = caller(0);
    croak "Please use `use Slurm qw(:constant)' instead of `use Slurm::Constant'."
	unless $callpkg eq "Slurm";
    my $main = caller(1);
    _get_constants() unless $got;
    {
	no strict "refs";
	my ($sym, $sub);
	while (($sym, $sub) = each(%const)) {
	    *{$main . "::$sym"} = $sub;
	}
    }
}

1;

__DATA__

=head1 NAME

Slurm::Constant - Constants for use with Slurm

=head1 SYNOPSIS

 use Slurm qw(:constant);

 if ($rc != SLURM_SUCCESS {
         print STDERR "action failed!\n";
 }

=head1 DESCRIPTION

This package export constants for use with Slurm. This includes enumerations and defined macros. The constants will be exported to package Slurm and the package which "use Slurm qw(:constant);".

=head1 EXPORTED CONSTANTS

=head2 DEFINED MACROS

=head3 Misc values

=over 2

=item * TRUE               1

=item * FALSE              0

=item * INFINITE           0xffffffff

=item * INFINITE64         0xffffffffffffffff

=item * NO_VAL             0xfffffffe

=item * NO_VAL64           0xfffffffffffffffe

=item * MAX_TASKS_PER_NODE 128

=item * SLURM_BATCH_SCRIPT 0xfffffffe

=back

=head3 Job state flags

=over 2

=item * JOB_STATE_BASE   0x000000ff

=item * JOB_STATE_FLAGS  0xffffff00

=item * JOB_COMPLETING   0x00008000

=item * JOB_CONFIGURING  0x00004000

=item * JOB_RESIZING     0x00002000

=item * JOB_SIGNALING    0x00400000

=item * READY_JOB_FATAL  -2

=item * READY_JOB_ERROR  -1

=item * READY_NODE_STATE 0x01

=item * READY_JOB_STATE  0x02

=back

=head3 Job mail notification

=over 2

=item * MAIL_JOB_BEGIN    0x0001

=item * MAIL_JOB_END      0x0002

=item * MAIL_JOB_FAIL     0x0004

=item * MAIL_JOB_REQUEUE  0x0008

=item * MAIL_INVALID_DEPEND 0x0400

=back

=head3 Offset for job's nice value

=over 2

=item * NICE_OFFSET             0x80000000

=back

=head3 Partition state flags

=over 2

=item * PARTITION_SUBMIT        0x01

=item * PARTITION_SCHED         0x02

=item * PARTITION_DOWN          0x01

=item * PARTITION_UP            0x03

=item * PARTITION_DRAIN         0x02

=item * PARTITION_INACTIVE      0x00

=back

=head3 Open stdout/stderr mode

=over 2

=item * OPEN_MODE_APPEND        1

=item * OPEN_MODE_TRUNCATE      2

=back

=head3 Node state flags

=over 2

=item * NODE_STATE_BASE       0x000f

=item * NODE_STATE_FLAGS      0xfff0

=item * NODE_STATE_NET        0x0010

=item * NODE_STATE_RES        0x0020

=item * NODE_STATE_UNDRAIN    0x0040

=item * NODE_STATE_CLOUD      0x0080

=item * NODE_RESUME           0x0100

=item * NODE_STATE_DRAIN      0x0200

=item * NODE_STATE_COMPLETING 0x0400

=item * NODE_STATE_NO_RESPOND 0x0800

=item * NODE_STATE_POWERED_DOWN 0x1000

=item * NODE_STATE_FAIL       0x2000

=item * NODE_STATE_POWER_UP   0x4000

=item * NODE_STATE_MAINT      0x8000

=back

=head3 Size of the credential signature

=over 2

=item * SLURM_SSL_SIGNATURE_LENGTH 128

=back

=head3 show_flags of slurm_get_/slurm_load_ function calls

=over 2

=item * SHOW_ALL        0x0001

=item * SHOW_DETAIL     0x0002

=back

=head3 Consumerable resources parameters

=over 2

=item * CR_CPU                     0x0001

=item * CR_SOCKET                  0x0002

=item * CR_CORE                    0x0004

=item * CR_MEMORY                  0x0010

=item * CR_ONE_TASK_PER_CORE       0x0100

=item * CR_CORE_DEFAULT_DIST_BLOCK 0x1000

=item * MEM_PER_CPU                0x8000000000000000

=item * SHARED_FORCE               0x8000

=back

=head3 Private data values

=over 2

=item * PRIVATE_DATA_JOBS         0x0001

=item * PRIVATE_DATA_NODES        0x0002

=item * PRIVATE_DATA_PARTITIONS   0x0004

=item * PRIVATE_DATA_USAGE        0x0008

=item * PRIVATE_DATA_USERS        0x0010

=item * PRIVATE_DATA_ACCOUNTS     0x0020

=item * PRIVATE_DATA_RESERVATIONS 0x0040

=back

=head3 Priority reset period

=over 2

=item * PRIORITY_RESET_NONE       0x0000        

=item * PRIORITY_RESET_NOW        0x0001        

=item * PRIORITY_RESET_DAILY      0x0002        

=item * PRIORITY_RESET_WEEKLY     0x0003        

=item * PRIORITY_RESET_MONTHLY    0x0004        

=item * PRIORITY_RESET_QUARTERLY  0x0005        

=item * PRIORITY_RESET_YEARLY     0x0006        

=back

=head3 Process priority propagation

=over 2

=item * PROP_PRIO_OFF             0x0000        

=item * PROP_PRIO_ON              0x0001        

=item * PROP_PRIO_NICER           0x0002        

=back

=head3 Partition state information

=over 2

=item * PART_FLAG_DEFAULT         0x0001

=item * PART_FLAG_HIDDEN          0x0002

=item * PART_FLAG_NO_ROOT         0x0004

=item * PART_FLAG_ROOT_ONLY       0x0008

=item * PART_FLAG_DEFAULT_CLR     0x0100

=item * PART_FLAG_HIDDEN_CLR      0x0200

=item * PART_FLAG_NO_ROOT_CLR     0x0400

=item * PART_FLAG_ROOT_ONLY_CLR   0x0800

=back

=head3 Reservation flags

=over 2

=item * RESERVE_FLAG_MAINT        0x00000001

=item * RESERVE_FLAG_NO_MAINT     0x00000002

=item * RESERVE_FLAG_DAILY        0x00000004

=item * RESERVE_FLAG_NO_DAILY     0x00000008

=item * RESERVE_FLAG_WEEKLY       0x00000010

=item * RESERVE_FLAG_NO_WEEKLY    0x00000020

=item * RESERVE_FLAG_IGN_JOBS     0x00000040

=item * RESERVE_FLAG_NO_IGN_JOB   0x00000080

=item * RESERVE_FLAG_OVERLAP      0x00004000

=item * RESERVE_FLAG_SPEC_NODES   0x00008000

=item * RESERVE_FLAG_HOURLY       0x00010000

=item * RESERVE_FLAG_NO_HOURLY    0x00020000

=back

=head3 Log debug flags

=over 2

=item * DEBUG_FLAG_SELECT_TYPE     0x00000001

=item * DEBUG_FLAG_STEPS           0x00000002

=item * DEBUG_FLAG_TRIGGERS        0x00000004

=item * DEBUG_FLAG_CPU_BIND        0x00000008

=item * DEBUG_FLAG_WIKI            0x00000010

=item * DEBUG_FLAG_NO_CONF_HASH    0x00000020

=item * DEBUG_FLAG_GRES            0x00000040

=item * DEBUG_FLAG_BG_PICK         0x00000080

=item * DEBUG_FLAG_BG_WIRES        0x00000100

=item * DEBUG_FLAG_BG_ALGO         0x00000200

=item * DEBUG_FLAG_BG_ALGO_DEEP    0x00000400

=item * DEBUG_FLAG_PRIO            0x00000800

=item * DEBUG_FLAG_BACKFILL        0x00001000

=item * DEBUG_FLAG_GANG            0x00002000

=item * DEBUG_FLAG_RESERVATION     0x00004000

=back

=head3 Preempt mode

=over 2

=item * PREEMPT_MODE_OFF         0x0000

=item * PREEMPT_MODE_SUSPEND     0x0001

=item * PREEMPT_MODE_REQUEUE     0x0002

=item * PREEMPT_MODE_CANCEL      0x0008

=item * PREEMPT_MODE_GANG        0x8000

=back

=head3 Trigger type

=over 2

=item * TRIGGER_RES_TYPE_JOB   	     0x0001

=item * TRIGGER_RES_TYPE_NODE  	     0x0002

=item * TRIGGER_RES_TYPE_SLURMCTLD   0x0003

=item * TRIGGER_RES_TYPE_SLURMDBD    0x0004

=item * TRIGGER_RES_TYPE_DATABASE    0x0005

=item * TRIGGER_TYPE_UP                 0x00000001

=item * TRIGGER_TYPE_DOWN               0x00000002

=item * TRIGGER_TYPE_FAIL               0x00000004

=item * TRIGGER_TYPE_TIME               0x00000008

=item * TRIGGER_TYPE_FINI               0x00000010

=item * TRIGGER_TYPE_RECONFIG           0x00000020

=item * TRIGGER_TYPE_BLOCK_ERR          0x00000040

=item * TRIGGER_TYPE_IDLE               0x00000080

=item * TRIGGER_TYPE_DRAINED            0x00000100

=item * TRIGGER_TYPE_PRI_CTLD_FAIL      0x00000200

=item * TRIGGER_TYPE_PRI_CTLD_RES_OP    0x00000400

=item * TRIGGER_TYPE_PRI_CTLD_RES_CTRL  0x00000800

=item * TRIGGER_TYPE_PRI_CTLD_ACCT_FULL 0x00001000

=item * TRIGGER_TYPE_BU_CTLD_FAIL       0x00002000

=item * TRIGGER_TYPE_BU_CTLD_RES_OP     0x00004000

=item * TRIGGER_TYPE_BU_CTLD_AS_CTRL    0x00008000

=item * TRIGGER_TYPE_PRI_DBD_FAIL       0x00010000

=item * TRIGGER_TYPE_PRI_DBD_RES_OP     0x00020000

=item * TRIGGER_TYPE_PRI_DB_FAIL        0x00040000

=item * TRIGGER_TYPE_PRI_DB_RES_OP      0x00080000

=back


=head2 Enumerations

=head3 Job states

=over 2

=item * JOB_PENDING        0        

=item * JOB_RUNNING        1        

=item * JOB_SUSPENDED      2        

=item * JOB_COMPLETE       3        

=item * JOB_CANCELLED      4        

=item * JOB_FAILED         5        

=item * JOB_TIMEOUT        6        

=item * JOB_NODE_FAIL      7        

=item * JOB_PREEMPTED      8

=item * JOB_BOOT_FAIL      9

=item * JOB_END           10

=back

=head3 Job state reason

=over 2

=item * WAIT_NO_REASON               0        

=item * WAIT_PRIORITY                1        

=item * WAIT_DEPENDENCY              2

=item * WAIT_RESOURCES               3        

=item * WAIT_PART_NODE_LIMIT         4

=item * WAIT_PART_TIME_LIMIT         5

=item * WAIT_PART_DOWN               6

=item * WAIT_PART_INACTIVE           7

=item * WAIT_HELD                    8

=item * WAIT_TIME                    9

=item * WAIT_LICENSES                10

=item * WAIT_ASSOC_JOB_LIMIT         11

=item * WAIT_ASSOC_RESOURCE_LIMIT    12

=item * WAIT_ASSOC_TIME_LIMIT        13

=item * WAIT_RESERVATION             14

=item * WAIT_NODE_NOT_AVAIL          15

=item * WAIT_HELD_USER               16

=item * WAIT_TBD2                    17

=item * FAIL_DOWN_PARTITION          18

=item * FAIL_DOWN_NODE               19

=item * FAIL_BAD_CONSTRAINTS         20

=item * FAIL_SYSTEM                  21

=item * FAIL_LAUNCH                  22

=item * FAIL_EXIT_CODE               23

=item * FAIL_TIMEOUT                 24

=item * FAIL_INACTIVE_LIMIT          25

=item * FAIL_ACCOUNT                 26

=item * FAIL_QOS                     27

=item * WAIT_QOS_THRES               28

=back

=head3 Job account types

=over 2

=item * JOB_START        0

=item * JOB_STEP         1

=item * JOB_SUSPEND      2

=item * JOB_TERMINATED   3

=back

=head3 Job Condition Flags

=over 2

=item * JOBCOND_FLAG_DUP         0x00000001

=item * JOBCOND_FLAG_NO_STEP     0x00000002

=item * JOBCOND_FLAG_NO_TRUNC    0x00000004

=item * JOBCOND_FLAG_RUNAWAY     0x00000008

=item * JOBCOND_FLAG_WHOLE_HETJOB 0x00000010

=item * JOBCOND_FLAG_NO_WHOLE_HETJOB 0x00000020

=back

=head3 Select jobdata type

=over 2

=item * SELECT_JOBDATA_GEOMETRY           0

=item * SELECT_JOBDATA_ROTATE             1

=item * SELECT_JOBDATA_CONN_TYPE          2

=item * SELECT_JOBDATA_BLOCK_ID           3

=item * SELECT_JOBDATA_NODES              4

=item * SELECT_JOBDATA_IONODES            5

=item * SELECT_JOBDATA_NODE_CNT           6

=item * SELECT_JOBDATA_ALTERED            7

=item * SELECT_JOBDATA_BLRTS_IMAGE        8

=item * SELECT_JOBDATA_LINUX_IMAGE        9

=item * SELECT_JOBDATA_MLOADER_IMAGE      10

=item * SELECT_JOBDATA_RAMDISK_IMAGE      11

=item * SELECT_JOBDATA_REBOOT             12

=item * SELECT_JOBDATA_RESV_ID            13

=item * SELECT_JOBDATA_PTR                14

=back

=head3 Select nodedata type

=over 2

=item * SELECT_NODEDATA_SUBCNT            2

=item * SELECT_NODEDATA_PTR               5

=back

=head3 Select print mode

=over 2

=item * SELECT_PRINT_HEAD                0

=item * SELECT_PRINT_DATA                1

=item * SELECT_PRINT_MIXED               2

=item * SELECT_PRINT_MIXED_SHORT         3

=item * SELECT_PRINT_BG_ID               4

=item * SELECT_PRINT_NODES               5

=item * SELECT_PRINT_CONNECTION          6

=item * SELECT_PRINT_ROTATE              7

=item * SELECT_PRINT_GEOMETRY            8

=item * SELECT_PRINT_START               9

=item * SELECT_PRINT_BLRTS_IMAGE         10

=item * SELECT_PRINT_LINUX_IMAGE         11

=item * SELECT_PRINT_MLOADER_IMAGE       12

=item * SELECT_PRINT_RAMDISK_IMAGE       13

=item * SELECT_PRINT_REBOOT              14

=item * SELECT_PRINT_RESV_ID             15

=back

=head3 Select node cnt

=over 2

=item * SELECT_GET_NODE_SCALING             0

=item * SELECT_GET_NODE_CPU_CNT             1

=item * SELECT_GET_BP_CPU_CNT               2

=item * SELECT_APPLY_NODE_MIN_OFFSET        3

=item * SELECT_APPLY_NODE_MAX_OFFSET        4

=item * SELECT_SET_NODE_CNT                 5

=item * SELECT_SET_BP_CNT                   6

=back

=head3 Jobacct data type

=over 2

=item * JOBACCT_DATA_TOTAL               0

=item * JOBACCT_DATA_PIPE                1

=item * JOBACCT_DATA_RUSAGE              2

=item * JOBACCT_DATA_MAX_VSIZE           3

=item * JOBACCT_DATA_MAX_VSIZE_ID        4

=item * JOBACCT_DATA_TOT_VSIZE           5

=item * JOBACCT_DATA_MAX_RSS             6

=item * JOBACCT_DATA_MAX_RSS_ID          7

=item * JOBACCT_DATA_TOT_RSS             8

=item * JOBACCT_DATA_MAX_PAGES           9

=item * JOBACCT_DATA_MAX_PAGES_ID        10

=item * JOBACCT_DATA_TOT_PAGES           11

=item * JOBACCT_DATA_MIN_CPU             12

=item * JOBACCT_DATA_MIN_CPU_ID          13

=item * JOBACCT_DATA_TOT_CPU             14

=back

=head3 TRES Records

=over 2

=item * TRES_CPU                        1

=item * TRES_MEM                        2

=item * TRES_ENERGY                     3

=item * TRES_NODE                       4

=item * TRES_BILLING                    5

=item * TRES_FS_DISK                    6

=item * TRES_VMEM                       7

=item * TRES_PAGES                      8

=back

=head3 Task distribution

=over 2

=item * SLURM_DIST_CYCLIC               1        

=item * SLURM_DIST_BLOCK                2

=item * SLURM_DIST_ARBITRARY            3

=item * SLURM_DIST_PLANE                4

=item * SLURM_DIST_CYCLIC_CYCLIC        5

=item * SLURM_DIST_CYCLIC_BLOCK         6

=item * SLURM_DIST_BLOCK_CYCLIC         7

=item * SLURM_DIST_BLOCK_BLOCK          8

=item * SLURM_NO_LLLP_DIST              9

=item * SLURM_DIST_UNKNOWN              10

=back

=head3 CPU bind type

=over 2

=item * CPU_BIND_VERBOSE            0x01 

=item * CPU_BIND_TO_THREADS         0x02 

=item * CPU_BIND_TO_CORES           0x04 

=item * CPU_BIND_TO_SOCKETS         0x08 

=item * CPU_BIND_TO_LDOMS           0x10 

=item * CPU_BIND_NONE               0x20 

=item * CPU_BIND_RANK               0x40 

=item * CPU_BIND_MAP                0x80 

=item * CPU_BIND_MASK               0x100

=item * CPU_BIND_LDRANK             0x200

=item * CPU_BIND_LDMAP              0x400

=item * CPU_BIND_LDMASK             0x800

=back

=head3 Memory bind type

=over 2

=item * MEM_BIND_VERBOSE         0x01

=item * MEM_BIND_NONE            0x02

=item * MEM_BIND_RANK            0x04

=item * MEM_BIND_MAP             0x08

=item * MEM_BIND_MASK            0x10

=item * MEM_BIND_LOCAL           0x20

=back

=head3 Node state

=over 2

=item * NODE_STATE_UNKNOWN        0

=item * NODE_STATE_DOWN           1

=item * NODE_STATE_IDLE           2

=item * NODE_STATE_ALLOCATED      3

=item * NODE_STATE_ERROR          4

=item * NODE_STATE_MIXED          5

=item * NODE_STATE_FUTURE         6

=item * NODE_STATE_END            7

=back

=head3 Ctx keys

=over 2

=item * SLURM_STEP_CTX_STEPID               0

=item * SLURM_STEP_CTX_TASKS                1

=item * SLURM_STEP_CTX_TID                  2

=item * SLURM_STEP_CTX_RESP                 3

=item * SLURM_STEP_CTX_CRED                 4

=item * SLURM_STEP_CTX_SWITCH_JOB           5

=item * SLURM_STEP_CTX_NUM_HOSTS            6

=item * SLURM_STEP_CTX_HOST                 7

=item * SLURM_STEP_CTX_JOBID                8

=item * SLURM_STEP_CTX_USER_MANAGED_SOCKETS 9

=back


head2 SLURM ERRNO

=head3 Defined macro error values

=over 2

=item * SLURM_SUCCESS           0

=item * SLURM_ERROR             -1

=back

=head3 General Message error codes

=over 2

=item * SLURM_UNEXPECTED_MSG_ERROR                      1000

=item * SLURM_COMMUNICATIONS_CONNECTION_ERROR           1001

=item * SLURM_COMMUNICATIONS_SEND_ERROR                 1002

=item * SLURM_COMMUNICATIONS_RECEIVE_ERROR              1003

=item * SLURM_COMMUNICATIONS_SHUTDOWN_ERROR             1004

=item * SLURM_PROTOCOL_VERSION_ERROR                    1005

=item * SLURM_PROTOCOL_IO_STREAM_VERSION_ERROR          1006

=item * SLURM_PROTOCOL_AUTHENTICATION_ERROR             1007

=item * SLURM_PROTOCOL_INSANE_MSG_LENGTH                1008

=item * SLURM_MPI_PLUGIN_NAME_INVALID                   1009

=item * SLURM_MPI_PLUGIN_PRELAUNCH_SETUP_FAILED         1010

=item * SLURM_PLUGIN_NAME_INVALID                       1011

=item * SLURM_UNKNOWN_FORWARD_ADDR                      1012

=back

=head3 communication failures to/from slurmctld

=over 2

=item * SLURMCTLD_COMMUNICATIONS_CONNECTION_ERROR       1800

=item * SLURMCTLD_COMMUNICATIONS_SEND_ERROR             1801

=item * SLURMCTLD_COMMUNICATIONS_RECEIVE_ERROR          1802

=item * SLURMCTLD_COMMUNICATIONS_SHUTDOWN_ERROR         1803

=back

=head3 _info.c/communication layer RESPONSE_SLURM_RC message codes

=over 2

=item * SLURM_NO_CHANGE_IN_DATA                         1900

=back

=head3 slurmctld error codes

=over 2

=item * ESLURM_INVALID_PARTITION_NAME                   2000

=item * ESLURM_DEFAULT_PARTITION_NOT_SET                2001

=item * ESLURM_ACCESS_DENIED                            2002

=item * ESLURM_JOB_MISSING_REQUIRED_PARTITION_GROUP     2003

=item * ESLURM_REQUESTED_NODES_NOT_IN_PARTITION         2004

=item * ESLURM_TOO_MANY_REQUESTED_CPUS                  2005

=item * ESLURM_INVALID_NODE_COUNT                       2006

=item * ESLURM_ERROR_ON_DESC_TO_RECORD_COPY             2007

=item * ESLURM_JOB_MISSING_SIZE_SPECIFICATION           2008

=item * ESLURM_JOB_SCRIPT_MISSING                       2009

=item * ESLURM_USER_ID_MISSING                          2010

=item * ESLURM_DUPLICATE_JOB_ID                         2011

=item * ESLURM_PATHNAME_TOO_LONG                        2012

=item * ESLURM_NOT_TOP_PRIORITY                         2013

=item * ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE        2014

=item * ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE        2015

=item * ESLURM_NODES_BUSY                               2016

=item * ESLURM_INVALID_JOB_ID                           2017

=item * ESLURM_INVALID_NODE_NAME                        2018

=item * ESLURM_WRITING_TO_FILE                          2019

=item * ESLURM_TRANSITION_STATE_NO_UPDATE               2020

=item * ESLURM_ALREADY_DONE                             2021

=item * ESLURM_INTERCONNECT_FAILURE                     2022

=item * ESLURM_BAD_DIST                                 2023

=item * ESLURM_JOB_PENDING                              2024

=item * ESLURM_BAD_TASK_COUNT                           2025

=item * ESLURM_INVALID_JOB_CREDENTIAL                   2026

=item * ESLURM_IN_STANDBY_MODE                          2027

=item * ESLURM_INVALID_NODE_STATE                       2028

=item * ESLURM_INVALID_FEATURE                          2029

=item * ESLURM_INVALID_AUTHTYPE_CHANGE                  2030

=item * ESLURM_INVALID_SCHEDTYPE_CHANGE                 2032

=item * ESLURM_INVALID_SELECTTYPE_CHANGE                2033

=item * ESLURM_INVALID_SWITCHTYPE_CHANGE                2034

=item * ESLURM_FRAGMENTATION                            2035

=item * ESLURM_NOT_SUPPORTED                            2036

=item * ESLURM_DISABLED                                 2037

=item * ESLURM_DEPENDENCY                               2038

=item * ESLURM_BATCH_ONLY                               2039

=item * ESLURM_TASKDIST_ARBITRARY_UNSUPPORTED           2040

=item * ESLURM_TASKDIST_REQUIRES_OVERCOMMIT             2041

=item * ESLURM_JOB_HELD                                 2042

=item * ESLURM_INVALID_CRED_TYPE_CHANGE                 2043

=item * ESLURM_INVALID_TASK_MEMORY                      2044

=item * ESLURM_INVALID_ACCOUNT                          2045

=item * ESLURM_INVALID_PARENT_ACCOUNT                   2046

=item * ESLURM_SAME_PARENT_ACCOUNT                      2047

=item * ESLURM_INVALID_LICENSES                         2048

=item * ESLURM_NEED_RESTART                             2049

=item * ESLURM_ACCOUNTING_POLICY                        2050

=item * ESLURM_INVALID_TIME_LIMIT                       2051

=item * ESLURM_RESERVATION_ACCESS                       2052

=item * ESLURM_RESERVATION_INVALID                      2053

=item * ESLURM_INVALID_TIME_VALUE                       2054

=item * ESLURM_RESERVATION_BUSY                         2055

=item * ESLURM_RESERVATION_NOT_USABLE                   2056

=item * ESLURM_INVALID_WCKEY                            2057

=item * ESLURM_RESERVATION_OVERLAP                      2058

=item * ESLURM_PORTS_BUSY                               2059

=item * ESLURM_PORTS_INVALID                            2060

=item * ESLURM_PROLOG_RUNNING                           2061

=item * ESLURM_NO_STEPS                                 2062

=item * ESLURM_INVALID_BLOCK_STATE                      2063

=item * ESLURM_INVALID_BLOCK_LAYOUT                     2064

=item * ESLURM_INVALID_BLOCK_NAME                       2065

=item * ESLURM_INVALID_QOS                              2066

=item * ESLURM_QOS_PREEMPTION_LOOP                      2067

=item * ESLURM_NODE_NOT_AVAIL                           2068

=item * ESLURM_INVALID_CPU_COUNT                        2069

=item * ESLURM_PARTITION_NOT_AVAIL                      2070

=item * ESLURM_CIRCULAR_DEPENDENCY                      2071

=item * ESLURM_INVALID_GRES                             2072

=item * ESLURM_JOB_NOT_PENDING                          2073

=back

=head3 switch specific error codes specific values defined in plugin module

=over 2

=item * ESLURM_SWITCH_MIN        3000

=item * ESLURM_SWITCH_MAX        3099

=item * ESLURM_JOBCOMP_MIN       3100

=item * ESLURM_JOBCOMP_MAX       3199

=item * ESLURM_SCHED_MIN         3200

=item * ESLURM_SCHED_MAX         3299

=back

=head3 slurmd error codes

=over 2

=item * ESLURMD_PIPE_ERROR_ON_TASK_SPAWN        4000

=item * ESLURMD_KILL_TASK_FAILED                4001

=item * ESLURMD_KILL_JOB_ALREADY_COMPLETE       4002

=item * ESLURMD_INVALID_ACCT_FREQ               4003

=item * ESLURMD_INVALID_JOB_CREDENTIAL          4004

=item * ESLURMD_UID_NOT_FOUND                   4005

=item * ESLURMD_GID_NOT_FOUND                   4006

=item * ESLURMD_CREDENTIAL_EXPIRED              4007

=item * ESLURMD_CREDENTIAL_REVOKED              4008

=item * ESLURMD_CREDENTIAL_REPLAYED             4009

=item * ESLURMD_CREATE_BATCH_DIR_ERROR          4010

=item * ESLURMD_MODIFY_BATCH_DIR_ERROR          4011

=item * ESLURMD_CREATE_BATCH_SCRIPT_ERROR       4012

=item * ESLURMD_MODIFY_BATCH_SCRIPT_ERROR       4013

=item * ESLURMD_SETUP_ENVIRONMENT_ERROR         4014

=item * ESLURMD_SHARED_MEMORY_ERROR             4015

=item * ESLURMD_SET_UID_OR_GID_ERROR            4016

=item * ESLURMD_SET_SID_ERROR                   4017

=item * ESLURMD_CANNOT_SPAWN_IO_THREAD          4018

=item * ESLURMD_FORK_FAILED                     4019

=item * ESLURMD_EXECVE_FAILED                   4020

=item * ESLURMD_IO_ERROR                        4021

=item * ESLURMD_PROLOG_FAILED                   4022

=item * ESLURMD_EPILOG_FAILED                   4023

=item * ESLURMD_SESSION_KILLED                  4024

=item * ESLURMD_TOOMANYSTEPS                    4025

=item * ESLURMD_STEP_EXISTS                     4026

=item * ESLURMD_JOB_NOTRUNNING                  4027

=item * ESLURMD_STEP_SUSPENDED                  4028

=item * ESLURMD_STEP_NOTSUSPENDED               4029

=item * ESLURMD_INVALID_SOCKET_NAME_LEN         4030

=back

=head3 slurmd errors in user batch job

=over 2

=item * ESCRIPT_CHDIR_FAILED              4100

=item * ESCRIPT_OPEN_OUTPUT_FAILED        4101

=item * ESCRIPT_NON_ZERO_RETURN           4102

=back

=head3 socket specific Slurm communications error

=over 2

=item * SLURM_PROTOCOL_SOCKET_IMPL_ZERO_RECV_LENGTH        5000

=item * SLURM_PROTOCOL_SOCKET_IMPL_NEGATIVE_RECV_LENGTH    5001

=item * SLURM_PROTOCOL_SOCKET_IMPL_NOT_ALL_DATA_SENT       5002

=item * ESLURM_PROTOCOL_INCOMPLETE_PACKET                  5003

=item * SLURM_PROTOCOL_SOCKET_IMPL_TIMEOUT                 5004

=item * SLURM_PROTOCOL_SOCKET_ZERO_BYTES_SENT              5005

=back

=head3 slurm_auth errors

=over 2

=item * ESLURM_AUTH_CRED_INVALID        6000

=item * ESLURM_AUTH_FOPEN_ERROR         6001

=item * ESLURM_AUTH_NET_ERROR           6002

=item * ESLURM_AUTH_UNABLE_TO_SIGN      6003

=back

=head3 accounting errors

=over 2

=item * ESLURM_DB_CONNECTION            7000

=item * ESLURM_JOBS_RUNNING_ON_ASSOC    7001

=item * ESLURM_CLUSTER_DELETED          7002

=item * ESLURM_ONE_CHANGE               7003

=back

=head2 

=head1 SEE ALSO

Slurm

=head1 AUTHOR

This library is created by Hongjia Cao, E<lt>hjcao(AT)nudt.edu.cnE<gt> and Danny Auble, E<lt>da(AT)llnl.govE<gt>. It is distributed with Slurm.

=head1 COPYRIGHT AND LICENSE

This library is free software; you can redistribute it and/or modify
it under the same terms as Perl itself, either Perl version 5.8.4 or,
at your option, any later version of Perl 5 you may have available.

=cut

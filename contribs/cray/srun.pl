#! /usr/bin/perl -w
###############################################################################
#
# srun - Wrapper for Cray's "aprun" command. If not executed within a job
#	 allocation, then also use "salloc" to create the allocation before
#	 executing "aprun".
#
###############################################################################
#
#  Copyright (C) 2011 SchedMD LLC <http://www.schedmd.com>.
#  Supported by the Oak Ridge National Laboratory Extreme Scale Systems Center
#  Written by Morris Jette <jette1@schedmd.gov>.
#  CODE-OCEC-09-009. All rights reserved.
#
#  This file is part of SLURM, a resource management program.
#  For details, see <http://www.schedmd.com/slurmdocs/>.
#  Please also read the included file: DISCLAIMER.
#
#  SLURM is free software; you can redistribute it and/or modify it under
#  the terms of the GNU General Public License as published by the Free
#  Software Foundation; either version 2 of the License, or (at your option)
#  any later version.
#
#  In addition, as a special exception, the copyright holders give permission
#  to link the code of portions of this program with the OpenSSL library under
#  certain conditions as described in each individual source file, and
#  distribute linked combinations including the two. You must obey the GNU
#  General Public License in all respects for all of the code used other than
#  OpenSSL. If you modify file(s) with this exception, you may extend this
#  exception to your version of the file(s), but you are not obligated to do
#  so. If you do not wish to do so, delete this exception statement from your
#  version.  If you delete this exception statement from all source files in
#  the program, then also delete it here.
#
#  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
#  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
#  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
#  details.
#
#  You should have received a copy of the GNU General Public License along
#  with SLURM; if not, write to the Free Software Foundation, Inc.,
#  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
#
###############################################################################

use strict;
use FindBin;
use Getopt::Long 2.24 qw(:config no_ignore_case require_order autoabbrev bundling);
use lib "${FindBin::Bin}/../lib/perl";
use autouse 'Pod::Usage' => qw(pod2usage);
use Slurm ':all';
use Switch;

my (	$account,
	$acctg_freq,
	$alps,
	$aprun_line_buf,
	$begin_time,
	$chdir,
	$check_time,
	$check_dir,
	$comment,
	$constraint,
	$contiguous,
	$cores_per_socket,
	$cpu_bind,
	$cpus_per_task,
	$debugger_test,
	$dependency,
	$disable_status,
	$distribution,
	$error_file,
	$epilog,
	$exclude_nodes,
	$exclusive,
	$extra_node_info,
	$group_id,
	$gres,
	$help,
	$hint,
	$hold,
	$immediate,
	$input_file,
	$job_id,
	$job_name,
	$kill_on_bad_exit,
	$label,
	$licenses,
	$mail_type,
	$mail_user,
	$man,
	$memory,
	$memory_per_cpu,
	$memory_bind, $mem_local,
	$min_cpus,
	$msg_timeout,
	$mpi_type,
	$multi_prog, $multi_executables,
	$network,
	$nice,
	$no_allocate,
	$nodelist, $nid_list,
	$ntasks_per_core,
	$ntasks_per_node,
	$ntasks_per_socket,
	$num_nodes,
	$num_tasks,
	$overcommit,
	$output_file,
	$open_mode,
	$partition,
	$preserve_env,
	$prolog,
	$propagate,
	$pty,
	$quiet,
	$quit_on_interrupt,
	$qos,
	$relative,
	$resv_ports,
	$reservation,
	$restart_dir,
	$share,
	$signal,
	$slurmd_debug,
	$sockets_per_node,
	$task_epilog,
	$task_prolog,
	$test_only,
	$threads_per_core,
	$threads,
	$time_limit, $time_secs,
	$time_min,
	$tmp_disk,
	$unbuffered,
	$user_id,
	$version,
	$verbose,
	$wait,
	$wc_key
);

my $aprun  = "aprun";
my $salloc = "BINDIR/salloc";
my $srun   = "BINDIR/srun";

my $have_job;
$aprun_line_buf = 1;
$have_job = 0;

foreach (keys %ENV) {
#	print "$_=$ENV{$_}\n";
	$have_job = 1			if $_ eq "SLURM_JOBID";
	$account = $ENV{$_}		if $_ eq "SLURM_ACCOUNT";
	$acctg_freq = $ENV{$_}		if $_ eq "SLURM_ACCTG_FREQ";
	$chdir = $ENV{$_}		if $_ eq "SLURM_WORKING_DIR";
	$check_time = $ENV{$_}		if $_ eq "SLURM_CHECKPOINT";
	$check_dir = $ENV{$_}		if $_ eq "SLURM_CHECKPOINT_DIR";
	$cpu_bind = $ENV{$_}		if $_ eq "SLURM_CPU_BIND";
	$cpus_per_task = $ENV{$_}	if $_ eq "SLURM_CPUS_PER_TASK";
	$dependency = $ENV{$_}		if $_ eq "SLURM_DEPENDENCY";
	$distribution = $ENV{$_}	if $_ eq "SLURM_DISTRIBUTION";
	$epilog = $ENV{$_}		if $_ eq "SLURM_EPILOG";
	$error_file = $ENV{$_}		if $_ eq "SLURM_STDERRMODE";
	$exclusive  = 1			if $_ eq "SLURM_EXCLUSIVE";
	$input_file = $ENV{$_}		if $_ eq "SLURM_STDINMODE";
	$job_name = $ENV{$_}		if $_ eq "SLURM_JOB_NAME";
	$label = 1			if $_ eq "SLURM_LABELIO";
	$memory_bind = $ENV{$_}		if $_ eq "SLURM_MEM_BIND";
	$memory_per_cpu = $ENV{$_}	if $_ eq "SLURM_MEM_PER_CPU";
	$memory = $ENV{$_}		if $_ eq "SLURM_MEM_PER_NODE";
	$mpi_type = $ENV{$_}		if $_ eq "SLURM_MPI_TYPE";
	$network = $ENV{$_}		if $_ eq "SLURM_NETWORK";
	$ntasks_per_core = $ENV{$_}	if $_ eq "SLURM_NTASKS_PER_CORE";
	$ntasks_per_node = $ENV{$_}	if $_ eq "SLURM_NTASKS_PER_NODE";
	$ntasks_per_socket = $ENV{$_}	if $_ eq "SLURM_NTASKS_PER_SOCKET";
	$num_tasks = $ENV{$_}		if $_ eq "SLURM_NTASKS";
	$num_nodes = $ENV{$_}		if $_ eq "SLURM_NNODES";
	$overcommit = $ENV{$_}		if $_ eq "SLURM_OVERCOMMIT";
	$open_mode = $ENV{$_}		if $_ eq "SLURM_OPEN_MODE";
	$output_file = $ENV{$_}		if $_ eq "SLURM_STDOUTMODE";
	$partition = $ENV{$_}		if $_ eq "SLURM_PARTITION";
	$prolog = $ENV{$_}		if $_ eq "SLURM_PROLOG";
	$qos = $ENV{$_}			if $_ eq "SLURM_QOS";
	$restart_dir = $ENV{$_}		if $_ eq "SLURM_RESTART_DIR";
	$resv_ports = 1			if $_ eq "SLURM_RESV_PORTS";
	$signal = $ENV{$_}		if $_ eq "SLURM_SIGNAL";
	$task_epilog = $ENV{$_}		if $_ eq "SLURM_TASK_EPILOG";
	$task_prolog = $ENV{$_}		if $_ eq "SLURM_TASK_PROLOG";
	$threads = $ENV{$_}		if $_ eq "SLURM_THREADS";
	$time_limit = $ENV{$_}		if $_ eq "SLURM_TIMELIMIT";
	$unbuffered = 1			if $_ eq "SLURM_UNBUFFEREDIO";
	$wait = $ENV{$_}		if $_ eq "SLURM_WAIT";
	$wc_key = $ENV{$_}		if $_ eq "SLURM_WCKEY";
}

# Make fully copy of execute line. This is needed only so that srun can run
# again and get the job's memory allocation for aprun (which is not available
# until after the allocation has been made). Add quotes if an argument contains
# spaces (e.g. --alps="-r 1" needs to be treadted as a single argument).
my ($i, $len, $orig_exec_line);
if ($ARGV[0]) {
	foreach (@ARGV) {
		if (index($_, " ") == -1) {
			$orig_exec_line .= "$_ ";
		} else {
			$orig_exec_line .= "\"$_\" ";
		}
	}
}

GetOptions(
	'A|account=s'			=> \$account,
	'acctg-freq=i'			=> \$acctg_freq,
	'alps=s'			=> \$alps,
	'B|extra-node-info=s'		=> \$extra_node_info,
	'begin=s'			=> \$begin_time,
	'D|chdir=s'			=> \$chdir,
	'checkpoint=s'			=> \$check_time,
	'checkpoint-dir=s'		=> \$check_dir,
	'comment=s'			=> \$comment,
	'C|constraint=s'		=> \$constraint,
	'contiguous'			=> \$contiguous,
	'cores-per-socket=i'		=> \$cores_per_socket,
	'cpu_bind=s'			=> \$cpu_bind,
	'c|cpus-per-task=i'		=> \$cpus_per_task,
	'd|dependency=s'		=> \$dependency,
	'debugger-test'			=> \$debugger_test,
	'X|disable-status'		=> \$disable_status,
	'e|error=s'			=> \$error_file,
	'epilog=s'			=> \$epilog,
	'x|exclude=s'			=> \$exclude_nodes,
	'exclusive'			=> \$exclusive,
	'gid=s'				=> \$group_id,
	'gres=s'			=> \$gres,
	'help|usage|?'			=> \$help,
	'hint=s'			=> \$hint,
	'H|hold'			=> \$hold,
	'I|immediate'			=> \$immediate,
	'i|input=s'			=> \$input_file,
	'jobid=i'			=> \$job_id,
	'J|job-name=s'			=> \$job_name,
	'K|kill-on-bad-exit'		=> \$kill_on_bad_exit,
	'l|label'			=> \$label,
	'L|licenses=s'			=> \$licenses,
	'm|distribution=s'		=> \$distribution,
	'mail-type=s'			=> \$mail_type,
	'mail-user=s'			=> \$mail_user,
	'man'				=> \$man,
	'mem=s'				=> \$memory,
	'mem-per-cpu=s'			=> \$memory_per_cpu,
	'mem_bind=s'			=> \$memory_bind,
	'mincpus=i'			=> \$min_cpus,
	'msg-timeout=i'			=> \$msg_timeout,
	'mpi=s'				=> \$mpi_type,
	'multi-prog'			=> \$multi_prog,
	'network=s'			=> \$network,
	'nice=i'			=> \$nice,
	'Z|no-allocate'			=> \$no_allocate,
	'w|nodelist=s'			=> \$nodelist,
	'ntasks-per-core=i'		=> \$ntasks_per_core,
	'ntasks-per-node=i'		=> \$ntasks_per_node,
	'ntasks-per-socket=i'		=> \$ntasks_per_socket,
	'n|ntasks=s'			=> \$num_tasks,
	'N|nodes=s'			=> \$num_nodes,
	'O|overcommit'			=> \$overcommit,
	'o|output=s'			=> \$output_file,
	'open-mode=s'			=> \$open_mode,
	'p|partition=s'			=> \$partition,
	'E|preserve-env'		=> \$preserve_env,
	'prolog=s'			=> \$prolog,
	'propagate=s'			=> \$propagate,
	'pty'				=> \$pty,
	'Q|quiet'			=> \$quiet,
	'q|quit-on-interrupt'		=> \$quit_on_interrupt,
	'qos=s'				=> \$qos,
	'r|relative=i'			=> \$relative,
	'resv-ports'			=> \$resv_ports,
	'reservation=s'			=> \$reservation,
	'restart-dir=s'			=> \$restart_dir,
	's|share'			=> \$share,
	'signal=s'			=> \$signal,
	'slurmd-debug=i'		=> \$slurmd_debug,
	'sockets-per-node=i'		=> \$sockets_per_node,
	'task-epilog=s'			=> \$task_epilog,
	'task-prolog=s'			=> \$task_prolog,
	'test-only'			=> \$test_only,
	'threads-per-core=i'		=> \$threads_per_core,
	'T|threads=i'			=> \$threads,
	't|time=s'			=> \$time_limit,
	'time-min=s'			=> \$time_min,
	'tmp=s'				=> \$tmp_disk,
	'u|unbuffered'			=> \$unbuffered,
	'uid=s'				=> \$user_id,
	'V|version'			=> \$version,
	'v|verbose'			=> \$verbose,
	'W|wait=i'			=> \$wait,
	'wckey=s'			=> \$wc_key
) or pod2usage(2);

if ($version) {
	system("$salloc --version");
	exit(0);
}

# Display man page or usage if necessary
pod2usage(0) if $man;
if ($help) {
	if ($< == 0) {   # Cannot invoke perldoc as root
		my $id = eval { getpwnam("nobody") };
		$id = eval { getpwnam("nouser") } unless defined $id;
		$id = -2			  unless defined $id;
		$<  = $id;
	}
	$> = $<;			# Disengage setuid
	$ENV{PATH} = "/bin:/usr/bin";	# Untaint PATH
	delete @ENV{'IFS', 'CDPATH', 'ENV', 'BASH_ENV'};
	if ($0 =~ /^([-\/\w\.]+)$/) { $0 = $1; }    # Untaint $0
	else { die "Illegal characters were found in \$0 ($0)\n"; }

}

my $script;
if ($ARGV[0]) {
	foreach (@ARGV) {
		$script .= "$_ ";
	}
} else {
	pod2usage(2);
}
my %res_opts;
my %node_opts;

my $command;

if ($have_job == 0) {
	if ($memory_per_cpu) {
		$i = index($memory_per_cpu, "hs");
		if ($i >= 0) {
			$memory_per_cpu = substr($memory_per_cpu, 0, $i);
		}
		$i = index($memory_per_cpu, "h");
		if ($i >= 0) {
			$memory_per_cpu = substr($memory_per_cpu, 0, $i);
		}
	}

	$command = "$salloc";
	$command .= " --account=$account"		if $account;
	$command .= " --acctg-freq=$acctg_freq"		if $acctg_freq;
	$command .= " --begin=$begin_time"		if $begin_time;
	$command .= " --chdir=$chdir"			if $chdir;
	$command .= " --comment=\"$comment\""		if $comment;
	$command .= " --constraint=\"$constraint\""	if $constraint;
	$command .= " --contiguous"			if $contiguous;
	$command .= " --cores-per-socket=$cores_per_socket" if $cores_per_socket;
	$command .= " --cpu_bind=$cpu_bind"		if $cpu_bind;
	$command .= " --cpus-per-task=$cpus_per_task"	if $cpus_per_task;
	$command .= " --dependency=$dependency"		if $dependency;
	$command .= " --distribution=$distribution"	if $distribution;
	$command .= " --exclude=$exclude_nodes"		if $exclude_nodes;
	$command .= " --exclusive"			if $exclusive;
	$command .= " --extra-node-info=$extra_node_info" if $extra_node_info;
	$command .= " --gid=$group_id"			if $group_id;
	$command .= " --gres=$gres"			if $gres;
	$command .= " --hint=$hint"			if $hint;
	$command .= " --hold"				if $hold;
	$command .= " --immediate"			if $immediate;
	$command .= " --jobid=$job_id"			if $job_id;
	$command .= " --job-name=$job_name"		if $job_name;
	$command .= " --licenses=$licenses"		if $licenses;
	$command .= " --mail-type=$mail_type"		if $mail_type;
	$command .= " --mail-user=$mail_user"		if $mail_user;
	$command .= " --mem=$memory"			if $memory;
	$command .= " --mem-per-cpu=$memory_per_cpu"	if $memory_per_cpu;
	$command .= " --mem_bind=$memory_bind"		if $memory_bind;
	$command .= " --mincpus=$min_cpus"		if $min_cpus;
	$command .= " --network=$network"		if $network;
	$command .= " --nice=$nice"			if $nice;
	$command .= " --nodelist=$nodelist"		if $nodelist;
	$command .= " --ntasks-per-core=$ntasks_per_core"     if $ntasks_per_core;
	$command .= " --ntasks-per-node=$ntasks_per_node"     if $ntasks_per_node;
	$command .= " --ntasks-per-socket=$ntasks_per_socket" if $ntasks_per_socket;
	$command .= " --ntasks=$num_tasks"		if $num_tasks;
	$command .= " --nodes=$num_nodes"		if $num_nodes;
	$command .= " --overcommit"			if $overcommit;
	$command .= " --partition=$partition"		if $partition;
	$command .= " --qos=$qos"			if $qos;
	$command .= " --quiet"				if !$verbose;
	$command .= " --reservation=$reservation"	if $reservation;
	$command .= " --share"				if $share;
	$command .= " --signal=$signal"			if $signal;
	$command .= " --sockets-per-node=$sockets_per_node" if $sockets_per_node;
	$command .= " --threads-per-core=$threads_per_core" if $threads_per_core;
	$command .= " --minthreads=$threads"		if $threads;
	$command .= " --time=$time_limit"		if $time_limit;
	$command .= " --time-min=$time_min"		if $time_min;
	$command .= " --tmp=$tmp_disk"			if $tmp_disk;
	$command .= " --uid=$user_id"			if $user_id;
	$command .= " --verbose"			if $verbose;
	$command .= " --wait=$wait"			if $wait;
	$command .= " --wckey=$wc_key"			if $wc_key;
	$command .= " $srun";
	$command .= " $orig_exec_line";
} else {
	$command = "$aprun";

	# Options that get set if aprun is launch either under salloc or directly
	if ($alps) {
	#	aprun fails when arguments are duplicated, prevent duplicates here
		$command .= " $alps";
		if (index($alps, "-d") >= 0)  { $cpus_per_task = 0 };
		if (index($alps, "-L") >= 0)  { $nodelist = 0 };
		if (index($alps, "-m") >= 0)  { $memory_per_cpu = 0 };
		if (index($alps, "-n") >= 0)  { $num_tasks = 0; $num_nodes = 0; }
		if (index($alps, "-N") >= 0)  { $ntasks_per_node = 0; $num_nodes = 0; }
		if (index($alps, "-q") >= 0)  { $quiet = 0 };
		if (index($alps, "-S") >= 0)  { $ntasks_per_socket = 0 };
		if (index($alps, "-sn") >= 0) { $sockets_per_node = 0 };
		if (index($alps, "-ss") >= 0) { $memory_bind = 0 };
		if (index($alps, "-T") >= 0)  { $aprun_line_buf = 0 };
		if (index($alps, "-t") >= 0)  { $time_limit = 0 };
	}
	# $command .= " -a"		no srun equivalent, architecture
	# $command .= " -b"		no srun equivalent, bypass transfer of executable
	# $command .= " -B"		no srun equivalent, reservation options
	# $command .= " -cc"		NO GOOD MAPPING, cpu binding
	$command .= " -d $cpus_per_task"			if $cpus_per_task;
	# Resource sharing largely controlled by SLURM configuration,
	# so this is an imperfect mapping of options
	if ($share) {
		$command .= " -F share";
	} elsif ($exclusive) {
		$command .= " -F exclusive";
	}
	$nid_list = get_nids($nodelist)				if $nodelist;
	$command .= " -L $nid_list"				if $nodelist;
	$command .= " -m $memory_per_cpu"			if $memory_per_cpu;
	if ($ntasks_per_node) {
		$command .= " -N $ntasks_per_node";
		if (!$num_tasks && $num_nodes) {
			$num_tasks = $ntasks_per_node * $num_nodes;
		}
	} elsif ($num_nodes) {
		$num_tasks = $num_nodes if !$num_tasks;
		$ntasks_per_node = int (($num_tasks + $num_nodes - 1) / $num_nodes);
		$command .= " -N $ntasks_per_node";
	}

	if ($num_tasks) {
		$command .= " -n $num_tasks";
	} elsif ($num_nodes) {
		$command .= " -n $num_nodes";
	}

	$command .= " -q"					if $quiet;
	# $command .= " -r"		no srun equivalent, core specialization
	$command .= " -S $ntasks_per_socket" 			if $ntasks_per_socket;
	# $command .= " -sl"		no srun equivalent, task placement on nodes
	$command .= " -sn $sockets_per_node" 			if $sockets_per_node;
	if ($memory_bind && ($memory_bind =~ /local/i)) {
		$command .= " -ss"
	}
	$command .= " -T"					if $aprun_line_buf;
	$time_secs = get_seconds($time_limit)			if $time_limit;
	$command .= " -t $time_secs"				if $time_secs;

	# Srun option which are not supported by aprun
	#	$command .= " --disable-status"			if $disable_status;
	#	$command .= " --epilog=$epilog"			if $epilog;
	#	$command .= " --kill-on-bad-exit"		if $kill_on_bad_exit;
	#	$command .= " --label"				if $label;
	#	$command .= " --mpi=$mpi_type"			if $mpi_type;
	#	$command .= " --msg-timeout=$msg_timeout"	if $msg_timeout;
	#	$command .= " --no-allocate"			if $no_allocate;
	#	$command .= " --open-mode=$open_mode"		if $open_mode;
	#	$command .= " --preserve_env"			if $preserve_env;
	#	$command .= " --prolog=$prolog"			if $prolog;
	#	$command .= " --propagate=$propagate"		if $propagate;
	#	$command .= " --pty"				if $pty;
	#	$command .= " --quit-on-interrupt"		if $quit_on_interrupt;
	#	$command .= " --relative=$relative"		if $relative;
	#	$command .= " --restart-dir=$restart_dir"	if $restart_dir;
	#	$command .= " --resv-ports"			if $resv_ports;
	#	$command .= " --slurmd-debug=$slurmd_debug"	if $slurmd_debug;
	#	$command .= " --task-epilog=$task_epilog"	if $task_epilog;
	#	$command .= " --task-prolog=$task_prolog"	if $task_prolog;
	#	$command .= " --test-only"			if $test_only;
	#	$command .= " --unbuffered"			if $unbuffered;

	$script = get_multi_prog($script)			if $multi_prog;
	$command .= " $script";

	# Input and output file options are not supported as aprun arguments,
	# but forwarded
	$command .= " <$input_file"				if $input_file;
	if ($error_file && ($error_file eq "none")) {
		$error_file = "/dev/null"
	}
	if ($output_file && ($output_file eq "none")) {
		$output_file = "/dev/null"
	}
	if ($open_mode && ($open_mode eq "a")) {
		$command .= " >>$output_file"			if $output_file;
		if ($error_file) {
			$command .= " 2>>$error_file";
		} elsif ($output_file) {
			$command .= " 2>&1";
		}
	} else {
		$command .= " >$output_file"			if $output_file;
		if ($error_file) {
			$command .= " 2>$error_file";
		} elsif ($output_file) {
			$command .= " 2>&1";
		}
	}
}

# Print here for debugging
#print "command=$command\n";
exec $command;

# Convert a SLURM time format to a number of seconds
sub get_seconds {
	my ($duration) = @_;
	$duration = 0 unless $duration;
	my $seconds = 0;

	# Convert [[HH:]MM:]SS to duration in seconds
	if ($duration =~ /^(?:(\d+):)?(\d*):(\d+)$/) {
		my ($hh, $mm, $ss) = ($1 || 0, $2 || 0, $3);
		$seconds += $ss;
		$seconds += $mm * 60;
		$seconds += $hh * 60;
	} elsif ($duration =~ /^(\d+)$/) {  # Convert number in minutes to seconds
		$seconds = $duration * 60;
	} else { # Unsupported format
		die("Invalid time limit specified ($duration)\n");
	}
	return $seconds;
}

# Convert a SLURM hostlist expression into the equivalent node index value
# expression
sub get_nids {
	my ($host_list) = @_;
	my ($nid_list) = $host_list;

	$nid_list =~ s/nid//g;
	$nid_list =~ s/\[//g;
	$nid_list =~ s/\]//g;
	$nid_list =~ s/\d+/sprintf("%d", $&)/ge;

	return $nid_list;
}

# Convert SLURM multi_prog file into a aprun options
# srun file format is "task_IDs command args..."
sub get_multi_prog {
	my ($fname) = @_;
	my ($out_line);
	my ($line_num) = 0;
	my (@words, $word, $word_num, $num_pes);

	open(MP, $fname) || die("Can not read $fname");
	while (<MP>) {
		chop;
		if ($line_num != 0) {
			$out_line .= " : ";
		}
		$line_num++;
		@words = split(' ', $_);
		$word_num = 0;
		foreach $word (@words) {
			if ($word_num == 0) {
				$num_pes = get_num_pes($word);
				$out_line .= " -n $num_pes";
			} else {
				$out_line .= " $word";
			}
			$word_num++;
		}
	}
	return $out_line;
}

# Convert number ranges and sets into a total count
sub get_num_pes {
	my ($pes_range) = @_;
	my (@ranges, $range);
	my (@pairs, $value);
	my ($min_value, $max_value);
	my ($value_num);
	my ($num_pes) = 0;

	@ranges = split(',', $pes_range);
	foreach $range (@ranges) {
		@pairs = split('-', $range);
		$value_num = 0;
		foreach $value (@pairs) {
			if ($value_num == 0) {
				$min_value = $value;
			}
			$max_value = $value;
			$value_num++;
		}
		$num_pes += ($max_value - $min_value + 1);
	}
	return $num_pes;
}

# Convert a size format containing optional K, M, G or T suffix to the
# equvalent number of megabytes
sub convert_mb_format {
	my ($value) = @_;
	my ($amount, $suffix) = $value =~ /(\d+)($|[KMGT])/i;
	return if !$amount;
	$suffix = lc($suffix);

	if (!$suffix) {
		$amount /= 1048576;
	} elsif ($suffix eq "k") {
		$amount /= 1024;
	} elsif ($suffix eq "m") {
		#do nothing this is what we want.
	} elsif ($suffix eq "g") {
		$amount *= 1024;
	} elsif ($suffix eq "t") {
		$amount *= 1048576;
	} else {
		print "don't know what to do with suffix $suffix\n";
		return;

	}

	return $amount;
}
##############################################################################

__END__

=head1 NAME

B<srun> - Run a parallel job

=head1 SYNOPSIS

srun  [OPTIONS...] executable [arguments...]

=head1 DESCRIPTION

Run a parallel job on cluster managed by SLURM.  If necessary, srun will
first create a resource allocation in which to run the parallel job.

=head1 OPTIONS

NOTE: Many options only apply only when creating a job allocation as noted
below. When srun is allocated within an existing job allocation, these options
are silently ignored.
The following aprun options have no equivalent in srun and must be specified
by using the B<--alps> option: B<-a>, B<-b>, B<-B>, B<-cc>, B<-f>, B<-r>, and
B<-sl>.  Many other options do not exact functionality matches, but duplication
srun behavior to the extent possible.

=over 4

=item B<-A> | B<--account=account>

Charge resources used by this job to specified account.
Applies only when creating a job allocation.

=item B<--acctg-freq=seconds>

Specify the accounting sampling interval.
Applies only when creating a job allocation.

=item B<--alps=options>

Specify the options to be passed to the aprun command.
If conflicting native srun options and --alps options are specified, the srun
option will take precedence for creating the job allocation (if necessary) and
the --alps options will take precedence for launching tasks with the aprun
command.

=item B<-B> | B<--extra-node-info=sockets[:cores[:threads]]>

Request a specific allocation of resources with details as to the
number and type of computational resources within a cluster:
number of sockets (or physical processors) per node,
cores per socket, and threads per core.
The individual levels can also be specified in separate options if desired:
B<--sockets-per-node=sockets>, B<--cores-per-socket=cores>, and
B<--threads-per-core=threads>.
Applies only when creating a job allocation.

=item B<--begin=time>

Defer job initiation until the specified time.
Applies only when creating a job allocation.

=item B<--checkpoint=interval>

Specify the time interval between checkpoint creations.
Not supported on Cray computers.

=item B<--checkpoint-dir=directory>

Directory where the checkpoint image should be written.
Not supported on Cray computers.

=item B<--comment=string>

An arbitrary comment.
Applies only when creating a job allocation.

=item B<-C> | B<--constraint=string>

Constrain job allocation to nodes with the specified features.
Applies only when creating a job allocation.

=item B<--contiguous>

Constrain job allocation to contiguous nodes.
Applies only when creating a job allocation.

=item B<--cores-per-socket=number>

Count of cores to be allocated per per socket.
Applies only when creating a job allocation.

=item B<--cpu_bind=options>

Strategy to be used for binding tasks to the CPUs.
Not supported on Cray computers due to many incompatible options.
Use --alps="-cc=..." instead.

=item B<-c> | B<--cpus-per-task=number>

Count of CPUs required per task.

=item B<-d> | B<--dependency=[condition:]jobid>

Wait for job(s) to enter specified condition before starting the job.
Valid conditions include after, afterany, afternotok, and singleton.
Applies only when creating a job allocation.

=item B<-D> | B<--chdir=directory>

Execute the program from the specified directory.
Applies only when creating a job allocation.

=item B<--epilog=filename>

Execute the specified program after the job step completes.
Not supported on Cray computers.

=item B<-e> | B<--error=filename>

Write stderr to the specified file.

=item B<--exclusive>

The job or job step will not share resources with other jobs or job steps.
Applies only when creating a job allocation.

=item B<-E> | B<--preserve-env>

Pass the current values of environment variables SLURM_NNODES and
SLURM_NTASKS through to the executable, rather than computing them
from command line parameters.
Not supported on Cray computers.

=item B<--gid=group>

If user root, then execute the job using the specified group access permissions.
Specify either a group name or ID.
Applies only when creating a job allocation.

=item B<--gres=gres_name[*count]>

Allocate the specified generic resources on each allocated node.
Applies only when creating a job allocation.

=item B<-?> | B<--help>

Print brief help message.

=item B<--hint=type>

Bind tasks according to application hints.
Not supported on Cray computers.

=item B<-H> | B<--hold>

Submit the job in a held state.
Applies only when creating a job allocation.

=item B<-I> | B<--immediate>

Exit if resources are not available immediately.
Applies only when creating a job allocation.

=item B<-i> | B<--input=filename>

Read stdin from the specified file.

=item B<--jobid=number>

Specify the job ID number. Usable only by SlurmUser or user root.
Applies only when creating a job allocation.

=item B<-J> | B<--job-name=name>

Specify a name for the job.
Applies only when creating a job allocation.

=item B<-K> | B<--kill-on-bad-exit>

Immediately terminate a job if any task exits with a non-zero exit code.
Not supported on Cray computers.

=item B<-l> | B<--label>

Prepend task number to lines of stdout/err.
Not supported on Cray computers.

=item B<-l> | B<--licenses=names>

Specification of licenses (or other resources available on all
nodes of the cluster) which must be allocated to this job.
Applies only when creating a job allocation.

=item B<-m> | B<--distribution=layout>

Specification of distribution of tasks across nodes.
Not supported on Cray computers.

=item B<--man>

Print full documentation.

=item B<--mail-type=event>

Send email when certain event types occur.
Valid events values are BEGIN, END, FAIL, REQUEUE, and ALL (any state change).
Applies only when creating a job allocation.

=item B<--mail-user=user>

Send email to the specified user(s). The default is the submitting user.
Applies only when creating a job allocation.

=item B<--mem=MB>

Specify the real memory required per node in MegaBytes.
Applies only when creating a job allocation.

=item B<--mem-per-cpu=MB>[h|hs]

Specify the real memory required per CPU in MegaBytes.
Applies only when creating a job allocation.
Append "h" or "hs" for huge page support.

=item B<--mem_bind=type>

Bind tasks to memory. The only option supported on Cray systems is local which
confines memory use to the local NUMA node.

=item B<--mincpus>

Specify a minimum number of logical CPUs per node.
Applies only when creating a job allocation.

=item B<--msg-timeout=second>

Modify the job launch message timeout.
Not supported on Cray computers.

=item B<--mpi=implementation>

Identify the type of MPI to be used. May result in unique initiation
procedures.
Not supported on Cray computers.

=item B<--multi-prog>

Run a job with different programs and different arguments for
each task. In this case, the executable program specified is
actually a configuration file specifying the executable and
arguments for each task.

=item B<--network=type>

Specify the communication protocol to be used.
Not supported on Cray computers.

=item B<--nice=adjustment>

Run the job with an adjusted scheduling priority within SLURM.
Applies only when creating a job allocation.

=item B<--ntasks-per-core=ntasks>

Request the maximum ntasks be invoked on each core.
Applies only when creating a job allocation.

=item B<--ntasks-per-node=ntasks>

Request the maximum ntasks be invoked on each node.
Applies only when creating a job allocation.

=item B<--ntasks-per-socket=ntasks>

Request the maximum ntasks be invoked on each socket.
Applies only when creating a job allocation.

=item B<-N> | B<--nodes=num_nodes>

Number of nodes to use.

=item B<-n> | B<--ntasks=num_tasks>

Number of tasks to launch.

=item B<--overcommit>

Overcommit resources. Launch more than one task per CPU.
Applies only when creating a job allocation.

=item B<-o> | B<--output=filename>

Specify the mode for stdout redirection.

=item B<--open-mode=append|truncate>

Open the output and error files using append or truncate mode as specified.

=item B<--partition=name>

Request a specific partition for the resource allocation.
Applies only when creating a job allocation.

=item B<--prolog=filename>

Execute the specified file before launching the job step.
Not supported on Cray computers.

=item B<--propagate=rlimits>

Allows users to specify which of the modifiable (soft) resource limits
to propagate to the compute nodes and apply to their jobs.
Not supported on Cray computers.

=item B<--pty>

Execute task zero in pseudo terminal mode.
Not supported on Cray computers.

=item B<--quiet>

Suppress informational messages. Errors will still be displayed.

=item B<-q> | B<--quit-on-interrupt>

Quit immediately on single SIGINT (Ctrl-C).
This is the default behavior on Cray computers.

=item B<--qos=quality_of_service>

Request a specific quality of service for the job.
Applies only when creating a job allocation.

=item B<-r> | B<--relative=offset>

Run a job step at the specified node offset in the current allocation.
Not supported on Cray computers.

=item B<--resv-ports=filename>

Reserve communication ports for this job. Used for OpenMPI.
Not supported on Cray computers.

=item B<--reservation=name>

Allocate resources for the job from the named reservation.
Applies only when creating a job allocation.

=item B<--restart-dir=directory>

Specifies the directory from which the job or job step's checkpoint should
be read.
Not supported on Cray computers.

=item B<-s> | B<--share>

The job can share nodes with other running jobs.
Applies only when creating a job allocation.

=item B<--signal=signal_number[@seconds]>

When a job is within the specified number seconds of its end time,
send it the specified signal number.

=item B<--slurmd-debug=level>

Specify a debug level for slurmd daemon.
Not supported on Cray computers.

=item B<--sockets-per-node=number>

Allocate the specified number of sockets per node.
Applies only when creating a job allocation.

=item B<--task-epilog=filename>

Execute the specified program after each task terminates.
Not supported on Cray computers.

=item B<--task-prolog=filename>

Execute the specified program before launching each task.
Not supported on Cray computers.

=item B<--test-only>

Returns an estimate of when a job would be scheduled.
Not supported on Cray computers.

=item B<-t> | B<--time=limit>

Time limit in minutes or hours:minutes:seconds.

=item B<--time-min=limit>

The minimum acceptable time limit in minutes or hours:minutes:seconds.
The default value is the same as the maximum time limit.
Applies only when creating a job allocation.

=item B<--tmp=mb>

Specify a minimum amount of temporary disk space.
Applies only when creating a job allocation.

=item B<-u> | B<--unbuffered>

Do not line buffer stdout from remote tasks.
Not supported on Cray computers.

=item B<--uid=user>

If user root, then execute the job as the specified user.
Specify either a user name or ID.
Applies only when creating a job allocation.

=item B<--usage>

Print brief help message.

=item B<-V> | B<--version>

Display version information and exit.

=item B<-v> | B<--verbose>

Increase the verbosity of srun's informational messages.

=item B<-W> | B<--wait=seconds>

Specify how long to wait after the first task terminates before terminating
all remaining tasks.
Not supported on Cray computers.

=item B<-w> | B<--nodelist=hostlist|filename>

Request a specific list of hosts to use.

=item B<--wckey=key>

Specify wckey to be used with job.
Applies only when creating a job allocation.

=item B<-X> | B<--disable-status>

Disable the display of task status when srun receives a single SIGINT (Ctrl-C).
Not supported on Cray computers.

=item B<-x> | B<--exclude=hostlist>

Request a specific list of hosts to not use
Applies only when creating a job allocation.

=item B<-Z> | B<--no-allocate>

Run the specified tasks on a set of nodes without creating a SLURM
"job" in the SLURM queue structure, bypassing the normal resource
allocation step.
Not supported on Cray computers.

=back

=cut

#! /usr/bin/perl -w
###############################################################################
#
# bsub - submit jobs in familar openlava format.
#
#
###############################################################################
#  Copyright (C) 2015 SchedMD LLC.
#  Written by Danny Auble <da@schedmd.com>.
#
#  This file is part of SLURM, a resource management program.
#  For details, see <http://slurm.schedmd.com/>.
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

#use strict;
use FindBin;
use Getopt::Long 2.24 qw(:config no_ignore_case);
use lib "${FindBin::Bin}/../lib/perl";
use autouse 'Pod::Usage' => qw(pod2usage);
use Slurm ':all';
use Slurmdb ':all'; # needed for getting the correct cluster dims
use Switch;

my (#$start_time,
    #$account,
    #$array,
    $err_path,
    #$export_env,
    $interactive,
    #$hold,
    #$resource_list,
    #$mail_options,
    #$mail_user_list,
    $job_name,
    $node_list,
    $mem_limit,
    $min_proc,
    $out_path,
    #$priority,
    $partition,
    #$variable_list,
    #@additional_attributes,
    $help,
    $man);

my $sbatch = "${FindBin::Bin}/sbatch";
my $salloc = "${FindBin::Bin}/salloc";
my $srun = "${FindBin::Bin}/srun";

GetOptions(#'a=s'      => \$start_time,
	   #'A=s'      => \$account,
	   'e=s'      => \$err_path,
	   #'h'        => \$hold,
	   'I'        => \$interactive,
	   #'j:s'      => sub { warn "option -j is the default, " .
	   'J=s'      => \$job_name,
	   #"stdout/stderr go into the same file\n" },
	   #'J=s'      => \$array,
	   #'l=s'      => \$resource_list,
	   'm=s'      => \$node_list,
	   'M=s'      => \$mem_limit,
	   'n=s'      => \$min_proc,
	   'o=s'      => \$out_path,
	   #'p=i'      => \$priority,
	   'q=s'      => \$partition,
	   #'S=s'      => sub { warn "option -S is ignored, " .
	   # "specify shell via #!<shell> in the job script\n" },
	   #'t=s'      => \$array,
	   #'v=s'      => \$variable_list,
	   #'V'        => \$export_env,
	   #'W=s'      => \@additional_attributes,
	   'W=s'      => \$time,
	   'x'        => \$exclusive,
	   'help|?'   => \$help,
	   'man'      => \$man,
	   )
	or pod2usage(2);

# Display usage if necessary
pod2usage(0) if $help;
if ($man) {
	#print "i man if";
        if ($< == 0) {   # Cannot invoke perldoc as root
		my $id = eval { getpwnam("nobody") };
		$id = eval { getpwnam("nouser") } unless defined $id;
		$id = -2                          unless defined $id;
		$<  = $id;
        }
        $> = $<;                         # Disengage setuid
        $ENV{PATH} = "/bin:/usr/bin";    # Untaint PATH
        delete @ENV{'IFS', 'CDPATH', 'ENV', 'BASH_ENV'};
        if ($0 =~ /^([-\/\w\.]+)$/) { $0 = $1; }    # Untaint $0
        else { die "Illegal characters were found in \$0 ($0)\n"; }
        pod2usage(-exitstatus => 0, -verbose => 2);
}

# Use sole remaining argument as the exe or script
my $script;
if ($ARGV[0]) {
	foreach (@ARGV) {
	        $script .= "$_ ";
	}

	#print "script = $script"
}

my $command;

if ($interactive || !$script) {
	$command = "$salloc";

#	Always want at least one node in the allocation
	if (!$node_opts{node_cnt}) {
		$node_opts{node_cnt} = 1;
	}
} else {
	if (!$script) {
		pod2usage(2);
	}
	if (_check_script($script)) {
		$command = "$sbatch";
	} else {
		$command = "$srun";
	}

	$command .= " -e $err_path" if $err_path;
	$command .= " -o $out_path" if $out_path;
}
#print " command = $command\n";


#$command .= " -n$node_opts{task_cnt}" if $ntask_cnt;

if ($node_list) {
	$node_list_tmp = _parse_node_list($node_list);
	$command .= " -w $node_list_tmp";
}

# $command .= " --mincpus=$res_opts{ncpus}"            if $res_opts{ncpus};
# $command .= " --ntasks-per-node=$res_opts{mppnppn}"  if $res_opts{mppnppn};

# if($res_opts{walltime}) {
# 	$command .= " -t$res_opts{walltime}";
# } elsif($res_opts{cput}) {
# 	$command .= " -t$res_opts{cput}";
# } elsif($res_opts{pcput}) {
# 	$command .= " -t$res_opts{pcput}";
# }

#$command .= " --account='$group_list'" if $group_list;
#$command .= " --array='$array'" if $array;
#$command .= " --constraint='$res_opts{proc}'" if $res_opts{proc};
#$command .= " --dependency=$depend"   if $depend;
#$command .= " --tmp=$res_opts{file}"  if $res_opts{file};
$command .= " --mem=$mem_limit"   if $mem_limit;
#$command .= " --nice=$res_opts{nice}" if $res_opts{nice};

#$command .= " --gres=gpu:$res_opts{naccelerators}"  if $res_opts{naccelerators};

# Cray-specific options
# $command .= " -n$res_opts{mppwidth}"		    if $res_opts{mppwidth};
# $command .= " -w$res_opts{mppnodes}"		    if $res_opts{mppnodes};
# $command .= " --cpus-per-task=$res_opts{mppdepth}"  if $res_opts{mppdepth};

# $command .= " --begin=$start_time" if $start_time;
# $command .= " --account=$account" if $account;
# $command .= " -H" if $hold;

# if($mail_options) {
# 	$command .= " --mail-type=FAIL" if $mail_options =~ /a/;
# 	$command .= " --mail-type=BEGIN" if $mail_options =~ /b/;
# 	$command .= " --mail-type=END" if $mail_options =~ /e/;
# }
# $command .= " --mail-user=$mail_user_list" if $mail_user_list;
$command .= " -J $job_name" if $job_name;
# $command .= " --nice=$priority" if $priority;
if ($min_proc) {
	$min_proc_tmp = _parse_procs($min_proc);
	$command .= " -n $min_proc_tmp";
}
$command .= " -t $time" if $time;
$command .= " -p $partition" if $partition;
$command .= " --exclusive" if $exclusive;
$command .= " $script" if $script;
#print " command = $command\n";
#exit;


#print "Command to run = $command\n";

# Execute the command and capture its stdout, stderr, and exit status. Note
# that if interactive mode was requested, the standard output and standard
# error are _not_ captured.
if ($interactive) {
	my $ret = system($command);
	exit ($ret >> 8);
} else {
	# Capture stderr from the command to the stdout stream.
	$command .= ' 2>&1';

	# Execute the command and capture the combined stdout and stderr.
	my @command_output = `$command 2>&1`;

	# Save the command exit status.
        my $command_exit_status = $?;

	# If available, extract the job ID from the command output and print
	# it to stdout, as done in the PBS version of qsub.
	if ($command_exit_status == 0) {
		my @spcommand_output=split(" ", $command_output[$#command_output]);
		$job_id= $spcommand_output[$#spcommand_output];
		print "Submitted job $job_id\n";
	} else {
		print("There was an error running the SLURM sbatch command.\n" .
		      "The command was:\n" .
		      "'$command'\n" .
		      "and the output was:\n" .
		      "'@command_output'\n");
	}

	# If block is true wait for the job to finish
	# my($resp, $count);
	my $slurm = Slurm::new();
	# if ( (lc($block) eq "true" ) and ($command_exit_status == 0) ) {
	# 	sleep 2;
	# 	my($job) = $slurm->load_job($job_id);
	# 	$resp = $$job{'job_array'}[0]->{job_state};
	# 	while ( $resp < JOB_COMPLETE ) {
	# 		$job = $slurm->load_job($job_id);
	# 		$resp = $$job{'job_array'}[0]->{job_state};
	# 		sleep 1;
	# 	}
	# }

	# Exit with the command return code.
	exit($command_exit_status >> 8);
}

# Get the process count
sub _parse_procs {
	my ($procs_range) = @_;

	# Get the max process count it if exist
	if ($procs_range =~ /,/) {
		my @sub_parts = split(/,/, $procs_range);
			return $sub_parts[1];
	} else {
		return $procs_range;
	}
}

sub _check_script {
	my ($script) = @_;

	if (open (my $file, "<$script")) {
		my $first_line = <$file>;
		close $file;

		return ($first_line =~ /\#!/);
	}

	return "";
}

# sub parse_resource_list {
# 	my ($rl) = @_;
# 	my %opt = ('accelerator' => "",
# 		   'arch' => "",
# 		   'block' => "",
# 		   'cput' => "",
# 		   'file' => "",
# 		   'host' => "",
# 		   'mem' => "",
# 		   'mpiprocs' => "",
# 		   'ncpus' => "",
# 		   'nice' => "",
# 		   'nodes' => "",
# 		   'naccelerators' => "",
# 		   'opsys' => "",
# 		   'other' => "",
# 		   'pcput' => "",
# 		   'pmem' => "",
# 		   'proc' => '',
# 		   'pvmem' => "",
# 		   'select' => "",
# 		   'software' => "",
# 		   'vmem' => "",
# 		   'walltime' => "",
# 		   # Cray-specific resources
# 		   'mppwidth' => "",
# 		   'mppdepth' => "",
# 		   'mppnppn' => "",
# 		   'mppmem' => "",
# 		   'mppnodes' => ""
# 		   );
# 	my @keys = keys(%opt);

# #	The select option uses a ":" separator rather than ","
# #	This wrapper currently does not support multiple select options

# #	Protect the colons used to separate elements in walltime=hh:mm:ss.
# #	Convert to NNhNNmNNs format.
# 	$rl =~ s/walltime=(\d{1,2}):(\d{2}):(\d{2})/walltime=$1h$2m$3s/;

# 	$rl =~ s/:/,/g;
# 	foreach my $key (@keys) {
# 		#print "$rl\n";
# 		($opt{$key}) = $rl =~ m/$key=([\w:\+=+]+)/;

# 	}

# #	If needed, un-protect the walltime string.
# 	if ($opt{walltime}) {
# 		$opt{walltime} =~ s/(\d{1,2})h(\d{2})m(\d{2})s/$1:$2:$3/;
# #		Convert to minutes for SLURM.
# 		$opt{walltime} = get_minutes($opt{walltime});
# 	}

# 	if($opt{accelerator} && $opt{accelerator} =~ /^[Tt]/ && !$opt{naccelerators}) {
# 		$opt{naccelerators} = 1;
# 	}

# 	if($opt{cput}) {
# 		$opt{cput} = get_minutes($opt{cput});
# 	}

# 	if ($opt{mpiprocs} && (!$opt{mppnppn} || ($opt{mpiprocs} > $opt{mppnppn}))) {
# 		$opt{mppnppn} = $opt{mpiprocs};
# 	}

# 	if($opt{mppmem}) {
# 		$opt{mem} = convert_mb_format($opt{mppmem});
# 	} elsif($opt{mem}) {
# 		$opt{mem} = convert_mb_format($opt{mem});
# 	}

# 	if($opt{file}) {
# 		$opt{file} = convert_mb_format($opt{file});
# 	}

# 	return \%opt;
# }

sub _parse_node_list {
	my ($node_string) = @_;
	my $hostlist = "";

	# Create the hostlist for formating
	my $hl = Slurm::Hostlist::create("");

	my @sub_parts = split(/ /, $node_string);
	foreach my $sub_part (@sub_parts) {
		if(!Slurm::Hostlist::push($hl, $sub_part)) {
			print "problem pushing host $sub_part onto hostlist\n";
		}
	}

        $hostlist = Slurm::Hostlist::ranged_string($hl);;
	my $hl_cnt = Slurm::Hostlist::count($hl);

	return $hostlist;
}

# sub get_minutes {
#     my ($duration) = @_;
#     $duration = 0 unless $duration;
#     my $minutes = 0;

#     # Convert [[HH:]MM:]SS to duration in minutes
#     if ($duration =~ /^(?:(\d+):)?(\d*):(\d+)$/) {
#         my ($hh, $mm, $ss) = ($1 || 0, $2 || 0, $3);
# 	$minutes += 1 if $ss > 0;
#         $minutes += $mm;
#         $minutes += $hh * 60;
#     } elsif ($duration =~ /^(\d+)$/) {  # Convert number in minutes to seconds
# 	    my $mod = $duration % 60;
# 	    $minutes = int($duration / 60);
# 	    $minutes++ if $mod;
#     } else { # Unsupported format
#         die("Invalid time limit specified ($duration)\n");
#     }

#     return $minutes;
# }

# sub convert_mb_format {
# 	my ($value) = @_;
# 	my ($amount, $suffix) = $value =~ /(\d+)($|[KMGT])/i;
# 	return if !$amount;
# 	$suffix = lc($suffix);

# 	if (!$suffix) {
# 		$amount /= 1048576;
# 	} elsif ($suffix eq "k") {
# 		$amount /= 1024;
# 	} elsif ($suffix eq "m") {
# 		#do nothing this is what we want.
# 	} elsif ($suffix eq "g") {
# 		$amount *= 1024;
# 	} elsif ($suffix eq "t") {
# 		$amount *= 1048576;
# 	} else {
# 		print "don't know what to do with suffix $suffix\n";
# 		return;
# 	}

# 	$amount .= "M";

# 	return $amount;
# }
##############################################################################

__END__

=head1 NAME

B<bsub> - submit a batch job in a familiar OpenLava format

=head1 SYNOPSIS

bsub
      [-e Error Path]
      [-I Interactive Mode]
      [-m Node List]
      [-M Memory Limit]
      [-n Min Process]
      [-o Output Path]
      [-q Queue Name]
      [-W Time]
      [-x Exclusive]
      [-h]
      [script]

=head1 DESCRIPTION

The B<bsub> submits batch jobs. It is aimed to be feature-compatible with OpenLavas' bsub.

=head1 OPTIONS

=over 4

=item B<-e Error Path>

Specify a new path to receive the standard error output for the job.

=item B<-I>

Interactive execution.

=item B<-J Job Name>

Name if the job to be submitted.

=item B<-m Host List>

Space seperated list of hosts that this job will run on.

=item B<-M Memory Limit>

Memory limit of the job.

=item B<-n Min Processes>

Minimum number of processes for the job.

=item B<-o out_path>

Specify the path to a file to hold the standard output from the job.

=item B<-q Queue>

The partition that this job will run on.

=item B<-W Time>

Run time of the job.

=item B<-x Exclusive>

Run this job in exclusive mode. Job will not share nodes with other jobs.

=item B<-?> | B<--help>

brief help message

=item B<--man>

full documentation

=back

=cut


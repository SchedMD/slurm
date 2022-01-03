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
#  This file is part of Slurm, a resource management program.
#  For details, see <https://slurm.schedmd.com/>.
#  Please also read the included file: DISCLAIMER.
#
#  Slurm is free software; you can redistribute it and/or modify it under
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
#  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
#  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
#  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
#  details.
#
#  You should have received a copy of the GNU General Public License along
#  with Slurm; if not, write to the Free Software Foundation, Inc.,
#  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
#
###############################################################################

use strict;
use FindBin;
use Getopt::Long 2.24 qw(:config no_ignore_case require_order);
use lib "${FindBin::Bin}/../lib/perl";
use autouse 'Pod::Usage' => qw(pod2usage);
use Slurm ':all';
use Slurmdb ':all'; # needed for getting the correct cluster dims
use Switch;

my (#$start_time,
    #$account,
    #$array,
    $workdir,
    $err_path,
    #$export_env,
    $exclusive,
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
    $time,
    #$variable_list,
    #@additional_attributes,
    $help,
    $man);

my $sbatch = "${FindBin::Bin}/sbatch";
my $salloc = "${FindBin::Bin}/salloc";
my $srun = "${FindBin::Bin}/srun";

GetOptions(#'a=s'      => \$start_time,
	   #'A=s'      => \$account,
	   'cwd=s'    => \$workdir,
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
} else {
	$interactive = 1;
	foreach (<STDIN>) {
		chomp($_);
		$script .= "$_\n";
	}
}
#print "script = $script";

my $command = $sbatch;

if (!$script) {
	if ($interactive) {
		print "Full interactive mode is not currently possible.  Please give me a command to run.\n";
		exit 1;
	} else {
		pod2usage(2);
	}
}

$command .= " -e $err_path" if $err_path;
$command .= " -o $out_path" if $out_path;

$command .= " -D $workdir" if $workdir;

#print " command = $command\n";


#$command .= " -n$node_opts{task_cnt}" if $ntask_cnt;

if ($node_list) {
	my $node_list_tmp = _parse_node_list($node_list);
	$command .= " -w $node_list_tmp";
}

$command .= " --mem=$mem_limit"   if $mem_limit;
$command .= " -J $job_name" if $job_name;

if ($min_proc) {
	my $min_proc_tmp = _parse_procs($min_proc);
	$command .= " -n $min_proc_tmp";
}
$command .= " -t $time" if $time;
$command .= " -p $partition" if $partition;
$command .= " --exclusive" if $exclusive;

# Here we are checking to see if the file is a known bsub script.
# If it isn't wrap it.  We have seen with certain scripts they need $0
# to point to the original script's name.  Since Slurm will rename the
# batch script when it gets ran on a compute node it breaks the $0
# functionality.  If we wrap it the problem is solved.
if ($interactive || !_check_bsub_script($ARGV[0])) {
	$command .=" --wrap=\"$script\"";
} else {
	$command .= " $script";
}
#print " command = $command\n";
#exit;

# Execute the command and capture its stdout, stderr, and exit status. Note
# that if interactive mode was requested, the standard output and standard
# error are _not_ captured.

# Execute the command and capture the combined stdout and stderr.
my @command_output = `$command 2>&1`;

#Save the command exit status.
my $command_exit_status = $?;

# If available, extract the job ID from the command output and print
# it to stdout, as done in the OpenLava version of bsub.
if ($command_exit_status == 0) {
	my @spcommand_output=split(" ",
				   $command_output[$#command_output]);
	my $job_id = $spcommand_output[$#spcommand_output];
	_print_job_submitted($job_id);
} else {
	print("There was an error running the Slurm sbatch command.\n" .
	      "The command was:\n" .
	      "'$command'\n" .
	      "and the output was:\n" .
	      "'@command_output'\n");
}
# Exit with the command return code.
exit($command_exit_status >> 8);

sub _get_default_partition_name {

	my $resp = Slurm->load_partitions(0, SHOW_ALL);
	if(!$resp) {
		die "Problem loading partitions.\n";
	}

	foreach my $part (@{$resp->{partition_array}}) {

		if ($part->{flags} & PART_FLAG_DEFAULT) { # Default
			return $part->{name};
		}
	}
	return "Unknown";
}

sub _print_job_submitted {
	my ($job_id) = @_;

	print "Job <$job_id> is submitted to ";
	if (!$partition) {
		print "default queue <" . _get_default_partition_name() . ">\n";
	} else {
		print "queue <$partition>\n";
	}
}

# Get the process count
sub _parse_procs {
	my ($procs_range) = @_;

	# Get the max process count if it exists
	if ($procs_range =~ /,/) {
		my @sub_parts = split(/,/, $procs_range);
			return $sub_parts[1];
	} else {
		return $procs_range;
	}
}

sub _check_bsub_script {
	my ($script) = @_;

	my $rc = 0;

	if (open (my $file, "<$script")) {
		my $line = <$file>;

		# check to make sure this is a script to begin with
		if ($line =~ /\#!/) {
			# Now check the first lines and make sure the first line
			# that isn't a comment is a #BSUB line.  If it isn't
			# we will presume this file needs to be wrapped.
			while ($line = <$file>) {
				next if ($line =~ /^$/);

				if ($line =~ /^\#BSUB/) {
					$rc = 1;
				} elsif ($line =~ /^\#/) {
					next;
				}

				last;
			}
		}

		close $file;
	}

	return $rc;
}

sub _parse_node_list {
	my ($node_string) = @_;
	my $hostlist = "";

	# Create the hostlist for formatting
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

##############################################################################

__END__

=head1 NAME

B<bsub> - submit a batch job in a familiar OpenLava format

=head1 SYNOPSIS

bsub
      [-cwd Working Directory Path]
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

=item B<-cwd Working Directory Path>

Specify the working directory path for the job.

=item B<-e Error Path>

Specify a new path to receive the standard error output for the job.

=item B<-I>

Interactive execution.

=item B<-J Job Name>

Name if the job to be submitted.

=item B<-m Host List>

Space separated list of hosts that this job will run on.

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


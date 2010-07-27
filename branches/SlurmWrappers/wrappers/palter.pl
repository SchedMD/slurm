#! /usr/bin/perl -w

#
# A utility to emulate palter.
#
# Modified:	2010-07-27
# By:		Phil Eckert
#

#
# For debugging.
#
#use lib "/var/opt/slurm_banana/lib64/perl5/site_perl/5.8.8/x86_64-linux-thread-multi/";

use File::Basename;
use Slurm;

BEGIN {
    # Just dump the man page in *roff format and exit if --roff specified.
    foreach my $arg (@ARGV) {
	if ($arg eq "--") {
	    last;
	} elsif ($arg eq "--roff") {
	    use Pod::Man;
	    my $parser = Pod::Man->new (section => 1);
	    $parser->parse_from_file($0, \*STDOUT);
	    exit 0;
	}
    }
}

use strict;
use Getopt::Long 2.24 qw(:config no_ignore_case);
use Time::Local;
use autouse 'Pod::Usage' => qw(pod2usage);

my @States = qw /
        ELIG
        RUN
        SUSPENDED
        COMPLETE
        CANCELLED
        FAILED
        TIMEOUT
        NODE_FAIL /;

my (
	$bankName,	$constraint,	$cpusPerNode,	$cpuTimeLimit,
	$debug,		$depJobId,	$earliestTime,	$exempt,
	$expedite,	$force,		$geometry,	$help,
	$jobId,		$man,		$nodes,		$noexempt,
	$noexpedite,	$normal,	$nostandby,	$pool,
	$priority,	$runTimeLimit,	$standby,	$translate,
	$verbose
);

#
# Slurm Version.
#
chomp(my $soutput = `sinfo --version`);
my ($sversion) = ($soutput =~ m/slurm (\d+\.\d+)/);

GetOptions('A=s'	=> \$earliestTime,
	   'b=s' 	=> \$bankName,
	   'c=s' 	=> \$constraint,
	   'cpn=s' 	=> \$cpusPerNode,
	   'd=s' 	=> \$depJobId,
	   'exempt' 	=> \$exempt,
	   'expedite' 	=> \$expedite,
	   'f' 		=> \$force,
	   'g=s' 	=> \$geometry,
	   'help|h|?' 	=> \$help,
	   'debug' 	=> \$debug,
	   'ln=s' 	=> \$nodes,
	   'man' 	=> \$man,
	   'n=s' 	=> \$jobId,
	   'noexempt'   => \$noexempt,
	   'noexpedite' => \$noexpedite,
	   'nostandby'  => \$nostandby,
	   'normal' 	=> \$normal,
	   'p=f' 	=> \$priority,
	   'pool=s' 	=> \$pool,
	   'standby' 	=> \$standby,
	   'translate' 	=> \$translate,
	   'tW=s' 	=> \$runTimeLimit,
	   'tM=s' 	=> \$cpuTimeLimit,
	   'v' 	=> \$verbose
	  ) or pod2usage(2);

#
# Display usage
#
if ($help) {
	pod2usage(-verbose => 0, -exit => 'NOEXIT', -output => \*STDOUT);
	print "Report problems to LC Hotline.\n";
	exit(0);
}

#
# Display man page
#
if ($man) {
#
#	Cannot invoke perldoc as root
#
	if ($< == 0) {    # Cannot invoke perldoc as root
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

#
# Use single argument as job id or fail if extra args
#
if (@ARGV) {
	if (!$jobId && @ARGV == 1) {
		$jobId = shift @ARGV;
	}
}

#
# Fail on unsupported option combinations
#
pod2usage(2) if @ARGV;
pod2usage(-message => "The option \"-n\" is required.", -verbose => 0) unless defined $jobId;

my $exclusive = 0;
$exclusive++ if $exempt;
$exclusive++ if $noexempt;
$exclusive++ if $expedite;
$exclusive++ if $noexpedite;
$exclusive++ if $standby;
$exclusive++ if $nostandby;
$exclusive++ if $normal;

if ($exclusive > 1) {
	pod2usage(-message => "Only one of the following options may be used at a time:\n
	\t-exempt\n\
	\t-noexempt\n
	\t-expedite\n
	\t-noexpedite\n
	\t-standby\n
	\t-nostandby\n
	\t-normal\n", -verbose => 0);
}


#
# Fail on unsupported options
#
unsupportedOption("-c <constraint>") 	if $constraint;
unsupportedOption("-cpn <cpuspernode>") if $cpusPerNode;
unsupportedOption("-exempt <exlist>") 	if $exempt;
unsupportedOption("-noexempt <exlist>") if $noexempt;
unsupportedOption("-g <geometry>") 	if $geometry;
unsupportedOption("-p <priority>") 	if $priority;


#
# Query the SLURM  job info
#

#
# Build command to get job data.
#
my $resp = Slurm->load_jobs(1);
if(!$resp) {
	die "Problem loading jobs.\n";
}

my $job;
foreach my $tmp (@{$resp->{job_array}}) {
	if ($jobId eq $tmp->{'job_id'}) {
		$job = $tmp;
		last;
	}
}

die("Job $jobId not found\n") if (!$job);

#
# Require confirmation unless force option specified
#
my $state = $States[$job->{'job_state'}];
my $user  = getpwuid($job->{'user_id'});
unless ($force) {
	confirm($user, $state, $job->{'account'} || '') or exit 0;
};

my $scontrol = "scontrol";
#
# Build an array of the commands necessary to make the requested
# job attribute changes
#
my @cmds = ();

#
# Only one of expedite, noexpedite, normal, standby, or nostandby
# may be set at a time.  This is enforced earlier in the code.
#
if ((defined $expedite) && ($job->{'qos'} ne "expedite")) {
	push @cmds, "$scontrol update jobid=$jobId qos=expedite  2>&1";
}

if ((defined $noexpedite) && ($job->{'qos'} ne "normal")) {
	push @cmds, "$scontrol update jobid=$jobId qos=normal  2>&1";
}

if ((defined $exempt) && ($job->{'qos'} ne "exempt")) {
	push @cmds, "$scontrol update jobid=$jobId qos=exempt  2>&1";
}

if ((defined $noexempt) && ($job->{'qos'} ne "normal")) {
	push @cmds, "$scontrol update jobid=$jobId qos=normal  2>&1";
}

if ((defined $normal) && ($job->{'qos'} ne "normal")) {
	push @cmds, "$scontrol update jobid=$jobId qos=normal  2>&1";
}

if ((defined $standby) && ($job->{'qos'} ne "standby")) {
	push @cmds, "$scontrol update jobid=$jobId qos=standby 2>&1";
}

if ((defined $nostandby) && ($job->{'qos'} ne "normal")) {
	push @cmds, "$scontrol update jobid=$jobId qos=normal  2>&1";
}

if ((defined $bankName) && ($job->{'account'} ne $bankName)) {
	push @cmds, "$scontrol update jobid=$jobId account=$bankName 2>&1";
}

if (defined $depJobId) {
	push @cmds, "$scontrol update jobid=$jobId dependency=$depJobId 2>&1";
}

if (defined $cpusPerNode) {
	push @cmds, "$scontrol update jobid=$jobId minprocs=$cpusPerNode 2>&1";
}

if (defined $pool) {
	push @cmds, "$scontrol update  jobid=$jobId partition=$pool  2>&1";
}

#if (defined $priority) {
#	if ($priority !~ /^(\d+(\.\d+)?)|(\.\d+)$/ 
#		or (($priority > 1) || ($priority < 0))) {
#		die("Priority must be a float between 0 and 1.\n");
#	}
#	my $moab_prio = int (($priority * 999) + 1);
#	push @cmds, "$scontrol update jobid=$jobId priority=$moab_prio  2>&1";
#}

if (defined $earliestTime) {
	my $epoch_time = `LCRM_date2epoch "$earliestTime"`;
	chomp $epoch_time;
	die("Invalid time specification ($earliestTime).\n") if ($epoch_time == 0);
	push @cmds, "$scontrol update jobid=$jobId starttime=$epoch_time 2>&1";
}

if (defined $nodes) {
	push @cmds, "$scontrol update numnodes=$nodes jobid=$jobId 2>&1";
}

if (defined $runTimeLimit) {
	my $wclimit = minutes($runTimeLimit);
	push @cmds, "$scontrol update timelimit=$wclimit jobid=$jobId 2>&1";
}

if (defined $cpuTimeLimit) {
	print STDERR "WARNING: -tM is not supported.  Setting wall clock time instead.\n";
	my $wclimit = minutes($cpuTimeLimit);
	push @cmds, "$scontrol update time=$wclimit jobid=$jobId 2>&1";
}

#
# Just exit if job already has requested attributes
#
if (scalar(@cmds) == 0) {
	print STDERR "Job not altered.\n";
	exit(0);
}

#
# Run each of the job attribute modification commands
#
$translate = 1;
foreach my $cmd (@cmds) {
	print "Running SLURM command: $cmd\n" if defined $translate;
	my $output = `$cmd 2>&1`;
	my $rc = $?;
	if ($rc) {
		die("Unable to alter moab job ($cmd) [rc=$rc]: $output\n") if defined $output;
		die("Unable to run the command \"$cmd\": $!\n");
	}
}


#
# $boolean = confirm()
# Confirm the job attribute alteration with y[es] or n[o]
# Timout after 10 seconds
#
sub confirm
{
	my ($user, $state, $account) = @_;
	print "Alter ", lc($state), " job $jobId ($user, $account)?";
	my $timeout = 10;
	my $confirmation;
	while (1) {
		print "  [y/n]  ";
		eval {
			local $SIG{ALRM} = sub { die "alarm\n" }; # \n required
			alarm $timeout;
			$confirmation = <STDIN>;
			alarm(0);
		};
		if ($@) {
			die("Alarm failed: $!\n") unless $@ eq "alarm\n";
			print "\n";
			return(0);
		} else {
			if ($confirmation =~ /^y/i) { return 1; }
			elsif ($confirmation =~ /^n/i) { return(0); }
		}
		
	}
}


#
# $seconds = seconds($duration)
# Converts a duration in nnnn[dhms] or [[dd:]hh:]mm to seconds
#
sub minutes
{
	my ($duration) = @_;

	$duration = 0 unless $duration;
	my $minutes = 0;

#
#	Convert [[dd:]hh:]mm to duration in seconds
#
	if ($duration =~ /^(?:(\d+):)?(\d*):(\d+)$/) {
		my ($dd, $hh, $mm) = ($1 || 0, $2 || 0, $3);
		$minutes += $hh * 60 * 60;
		$minutes += $dd * 24 * 60 * 60;
	}

#
#	Convert nnnn[dhms] to duration in seconds
#
	elsif ($duration =~ /^(\d+)([dhms])$/) {
		my ($number, $metric) = ($1, $2);
		if    ($metric eq 's') {  }
		elsif ($metric eq 'm') { $minutes = $number; }
		elsif ($metric eq 'h') { $minutes = $number * 60; }
		elsif ($metric eq 'd') { $minutes = $number * 24 * 60; }
	}

#
#	Convert number in minutes to seconds
#
	elsif ($duration =~ /^(\d+)$/) {
		$minutes = $duration;
	}

#
#	Unsupported format
#
	else {
		die("Invalid time limit specified ($duration)\n");
	}

	return($minutes);
}


#
# Report an option as not being supported.
#
sub unsupportedOption
{
	my ($option) = @_;

	printf("\n Unsupported option: $option\n\n");

	exit(1);
}

##############################################################################

__END__

=head1 NAME

palter - alter job attributes in a familiar lcrm format

=head1 SYNOPSIS

B<palter> <I<jobid> | B<-n> I<jobid>> [B<-f>] [B<-A> I<starttime>] [B<-b> I<bank>] [B<-d> I<jobid>] [B<-g> I<geometry>] [B<-ln> I<num_nodes>] [B<-p> I<priority>] [B<-pool> I<poolname>] [B<-tW> I<runtimelim>] [B<-tM> I<cputimelim>] [B<-exempt> [I<exlist>] | B<-noexempt> [I<exlist>] | B<-expedite> | B<-noexpedite> | B<-standby> | B<-nostandby> | B<-normal>] [B<-v>] [B<-h, -?, --help>] [B<--man>]

=head1 DESCRIPTION

The B<palter> command is used to alter job attributes.

If the -f option is not specified the attributes of the
selected job are modified only upon confirmation. The
confirmation message lists the jobid, the job owner
name and the bank name of the selected job. A reply of y[es]
or n[o] is required. If a message is not input within ten
seconds, a "timeout" message is sent to the terminal and
palter terminates.

If the specified job is not found or none of its attributes
were changed, the following message is sent to standard
error: "Job not altered."

=head1 OPTIONS

=over 4

=item B<-b> I<bank_name>

Make the selected job draw resources from the designated bank
(not permitted on a running job).

=item B<-f>

Causes attributes to change without asking for confirmation.

=item B<-tM> I<time>

WARNING: -tM is not really supported.  pstat will accept -tM but set
-tW instead.

=item B<-tW> I<time>

Set the maximum wall clock time for the selected job to time.

=item B<-v>

Verbose mode. Show the jobs that were deleted and write error 
messages to standard error.

=item B<-h, -?, --help>

Display a brief help message

=item B<--man>

Display full documentation

=back

=head1 EXAMPLE

palter -n 123 -b lc

=head1 REPORTING BUGS

Report problems to LC Hotline.

=cut


#! /usr/bin/perl -w
#
#
# prm - cancel slurm jobs in familar lcrm format.
#
# Modified:	2010-12014
# By:     	Phil Eckert
#

#
# For debugging.
#
my $debug=0;

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

use Getopt::Long 2.24 qw(:config no_ignore_case);
use Time::Local;
use autouse 'Pod::Usage' => qw(pod2usage);

my ($cmd, $rc, $output);

my (
	$startTime, 	 $bankName,   $force,    $graceTime,
	$help,           $machine,    $man,      $message,
	$jobList,        $terminated, $userName, $verbose
);

#
# Check SLURM status.
#
isslurmup();

chomp(my $soutput = `sinfo --version`);
my ($sversion) = ($soutput =~ m/slurm (\d+\.\d+)/);

#
# Get options.
#
GetOpts();

$graceTime = seconds($graceTime) if ($graceTime);

#
# Iterate through the selected jobs
#
$userName = (getpwuid($<))[0] unless $bankName || $userName || $jobList;
my $deleteCount = 0;
my $worstRc = 0;
my ($jobId, $user, $account, $state, $start);
my $now = time();


#
# Process job argument(s).
#
my @jobs = `scontrol show job --oneliner`;
if ($?) {
	printf("\n Error getting job information.\n\n");
	exit($?);
}

foreach my $job (@jobs) {
        next if ($job =~ /No jobs in the system/);
	($jobId)     = ($job =~ m/JobId=(\S+)/);
	($user)      = ($job =~ m/UserId=(\S+)/);
	$user        =~ s/\(.*\)//;

	($account)   = ($job =~ m/Account=(\S+)/);
	($state)     = ($job =~ m/JobState=(\S+)/);
	($start)     = ($job =~ m/StartTime=(\S+)/);
	$start       = slurm2epoch($start);

	next if ($state eq "CANCELLED" || $state eq "TIMEOUT" || $state eq "COMPLETED" );

#
#	Filter jobs according to options and arguments
#
	if ($bankName) { next unless $account eq $bankName; }
	if ($userName) { next unless $user    eq $userName; }
	if ($jobList) {
		next unless grep { $_ eq $jobId } split(/,/, $jobList);
	}

#
#	Require confirmation unless force option specified
#
	unless ($force) {
		confirm() or next;
	}

#
#	Build command
#
	if ($graceTime) {
		my $diff = $now - $start;
		my $new_time = $diff + $graceTime;
		$new_time = int ($new_time / 60);
		$cmd = "scontrol update job=$jobId timelimit=$new_time 2>&1";
	} else {
		$cmd = "scancel  $jobId 2>&1";
	}
	printf("cmd - $cmd\n") if ($debug);

	$output = `$cmd 2>&1`;
	$rc     = $? >> 8;
	$worstRc = $rc if $rc > $worstRc;

#
#	Display and log error output
#
	if ($rc) {
		if ($verbose) {
			printf($output);
		}
	} else {
		$deleteCount++;
		if ($graceTime) {
			print "Job $jobId will be removed in $graceTime seconds\n";
		} else {
			print "Job $jobId removed\n";
		}
	}
}
printf("No jobs were deleted.\n") unless $deleteCount;

#
# Exit with worst seen status code
#
exit($worstRc);


#
# Get user options.
#
sub GetOpts
{
	GetOptions(
		'A'        => \$startTime,
		'b=s'      => \$bankName,
		'f'        => \$force,
		'gt=s'     => \$graceTime,
		'help|h|?' => \$help,
		'm=s'      => \$machine,
		'man'      => \$man,
		'msg=s'    => \$message,
		'n=s'      => \$jobList,
		'T'        => \$terminated,
		'u=s'      => \$userName,
		'v'        => \$verbose,
	) or pod2usage(2);

#
#	Show help.
#
	if ($help) {
		pod2usage(-verbose => 0, -exit => 'NOEXIT', -output => \*STDOUT);
		print "Report problems to LC Hotline.\n";
		exit(0);
	}

#
#	Display man page.
#
	if ($man) {
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

#
#	Use single argument as job id or fail if extra args.
#
	if (@ARGV) {
		if (!$jobList && @ARGV == 1) {
			$jobList = shift @ARGV;
		}
	}

#
#	Fail on unsupported option combinations
#
	pod2usage(2) if @ARGV;
	pod2usage(-message => "The -b or -u options may not be used with the -n option", -verbose => 0) if ($jobList && ($userName || $bankName));

#
#	Fail on unsupported options
#
	notSupported("-T") if $terminated;
	notSupported("-A <date time>") if $startTime;
	notSupported("-m <host>") if $machine;

	return;
}


#
# Report options that are not supported in this version.
#
sub notSupported
{
	my ($opt) = @_;

	printf("\n $opt - not supported.\n\n");

	exit(1);
}

#
# Confirm the job deletion with y[es] or n[o]
# Timout after 10 seconds
#
sub confirm
{
	if ($graceTime) {
		print "Set remaining time of job $jobId ($user, $account) ";
		print "to $graceTime seconds?";
	} else {
		print "removing ", lc($state), " job $jobId ($user, $account)?";
	}

	my $timeout = 10;
	my $confirmation;
	while (1) {
		print "  [y/n]  ";
		eval {
			local $SIG{ALRM} = sub { die "alarm\n" }; # \n required
			alarm $timeout;
			$confirmation = <STDIN>;
			alarm 0;
		};
		if ($@) {
			die("Alarm failed: $!\n") unless $@ eq "alarm\n";
			print "\n";
			return 0;
		} else {
			if ($confirmation =~ /^y/i) {
				return 1;
			} elsif ($confirmation =~ /^n/i) {
				return 0;
			}
		}
	}

	return;
}


#
# Slurm time to epoch time.
#
sub slurm2epoch
{
	my ($slurmTime) = @_;

	return ("0") if ($slurmTime =~ /Unknown/);

	my $mes;

	my ($yr,$mm, $dd, $hr, $mn, $sc) = split(/[-|T|:]/, $slurmTime);
	my $epoch = timelocal($sc,$mn,$hr,$dd,$mm-1,$yr);

	return($epoch);
}


#
# $seconds = seconds($duration)
# Converts a duration in nnnn[dhms] or [[dd:]hh:]mm to seconds
#
sub seconds
{
	my ($duration) = @_;

	$duration = 0 unless $duration;
	my $seconds = 0;

#
# Convert [[dd:]hh:]mm to duration in seconds
#
	if ($duration =~ /^(?:(\d+):)?(\d*):(\d+)$/) {
		my ($dd, $hh, $mm) = ($1 || 0, $2 || 0, $3);
		$seconds += $mm * 60;
		$seconds += $hh * 60 * 60;
		$seconds += $dd * 24 * 60 * 60;
	}


#
#	Convert nnnn[dhms] to duration in seconds.
#
	elsif ($duration =~ /^(\d+)([dhms])$/) {
		my ($number, $metric) = ($1, $2);
		if    ($metric eq 's') { $seconds = $number; }
		elsif ($metric eq 'm') { $seconds = $number * 60; }
		elsif ($metric eq 'h') { $seconds = $number * 60 * 60; }
		elsif ($metric eq 'd') { $seconds = $number * 24 * 60 * 60; }
	}

#
#	Convert number in minutes to seconds.
#
	elsif ($duration =~ /^(\d+)$/) {
		$seconds = $duration * 60;
	}

#
#	Unsupported format.
#
	else {
		logDie("Invalid time limit specified ($duration)\n");
	}

#
#	Time must be at least 1 minute (60 seconds).
#
	$seconds = 60 if $seconds < 60;

	return($seconds);
}


#
# Determine if SLURM is available.
#
sub isslurmup
{
	my $out = `scontrol show part 2>&1`;
	if ($?) {
		printf("\n SLURM is not communicating.\n\n");
		exit(1);
	}

	return;
}

##############################################################################

__END__

=head1 NAME

prm - deletes jobs in a familiar lcrm format

=head1 SYNOPSIS

B<prm> [B<-b> I<bank_name>] [B<-f>] [I<jobid> | B<-n> I<jobid_list>] [B<-u> I<user_name>] [B<-gt> I<time>] [B<-v>] [B<-h, -?, --help>] [B<--man>]

=head1 DESCRIPTION

The B<prm> command is used to delete jobs.

If B<prm> is issued without specifying one of the B<-b>, B<-u> or B<-n> flags, all jobs owned by the calling user will be targetted for deletion.

If the B<-f> option is not specified, each selected job is deleted only upon confirmation for that job. The confirmation messages list the job id, the job owner name and the bank name of selected job. A reply of y[es] or n[o] is required. If a message is not input within ten seconds, a "timeout" message is sent to the terminal and prm terminates.

=head1 OPTIONS

=over 4

=item B<-b> I<bank_name>

Delete only jobs that are drawing from the specified bank.

=item B<-f>

Causes jobs to be deleted without asking for confirmation.
 
=item B<-n> I<job_id_list>

Delete only the jobs with jobids that correspond to one of the items in I<job_id_list>. I<job_id_list> must be a comma-separated list of jobids. This option may not be used in conjunction with either the B<-b> or B<-u> options.

=item B<-u> I<user_name>

Delete only jobs that are owned by the specified user.

=item B<-gt> I<gracetime>

The  amount of wall clock time that the job is to continue to run before it
will be removed by Moab. If  the  job  is registered  to  receive  a signal,
Moab will send one when the prm is executed.

=item B<-v>

Verbose mode. Show the jobs that were deleted and write error messages to standard error.

=item B<-h, -?, --help>

Display a brief help message

=item B<--man>

Display full documentation

=back

=head1 EXAMPLE

prm -v

prm -b projectA

prm -n PBS.1234.0 -f

=head1 REPORTING BUGS

Report problems to LC Hotline.

=cut






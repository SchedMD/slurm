#! /usr/bin/perl -w
#
# prel - release SLURM jobs in an LCRM manner.
#
# Modified:	2010-12-14
# By:		Phil Eckert
#

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

my ($cmd, $rc, $output);


my (
    $bankName, 	$force,	   $help,	$man,
    $jobList,   $userName, $verbose
);

#
# Check SLURM status.
#
isslurmup();

my $hst = `scontrol show config | grep ClusterName`;
my ($host) = ($hst =~ m/ = (\S+)/);

#
# Slurm Version.
#
chomp(my $soutput = `sinfo --version`);
my ($sversion) = ($soutput =~ m/slurm (\d+\.\d+)/);
if ($sversion < 2.2) {
	printf("\n Hold/Release functionality not available in this release.\n\n");
	exit(1);
}

#
# Get User Options.
#
GetOpts();

#
# Query the jobs.
#
my $worstRc = 0;
my ($jobId, $user, $account, $state, $allocPartition, $reason);

my @jobs = `scontrol show job --oneliner`;
if ($?) {
	printf("\n Error getting job information.\n\n");
	exit($?);
}

foreach my $job (@jobs) {
	next if ($job =~ /No jobs in the system/);
#               next if ($job->{job_id} > 1000000); # don't process interactive jobs.
	($jobId)     = ($job =~ m/JobId=(\S+)/);
	($user)      = ($job =~ m/UserId=(\S+)/);
        $user        =~ s/\(.*\)//;
	($account)   = ($job =~ m/Account=(\S+)/);
	($reason)    = ($job =~ m/Reason=(\S+)/);
	($state)     = ($job =~ m/JobState=(\S+)/);
	next if ($state eq "CANCELLED" ||
	 	 $state eq "RUNNING" ||
	 	 $state eq "COMPLETED" ||
		 $state eq "TIMEOUT" ||
		 $reason !~ /Held/);

	$allocPartition = $host;

#
#	Filter jobs according to options and arguments.
#
	if ($bankName) { next unless $account eq $bankName; }
	if ($userName) { next unless $user    eq $userName; }
	if ($jobList) {
		next unless grep { $_ eq $jobId } split(/,/, $jobList);
	}

#
#	Require confirmation unless force option specified.
#
	unless ($force) {
	    confirm() or next;
	}

#
#	Build SLURM command.
#
	$cmd = "scontrol release $jobId";
	$output = `$cmd 2>&1`;
	$rc     = $? >> 8;
	$worstRc  = $rc if $rc > $worstRc;

#
#	Display and log error output
#
	print "Job $jobId released\n";
}

#
# Exit with worst seen status code.
#
exit($worstRc);


#
# $boolean = confirm()
# Confirm the job release with y[es] or n[o]
# Timout after 10 seconds
#
sub confirm
{
	print "release ", lc($state), " job $jobId ($user, $account)?";
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
			Die("Alarm failed: $!\n") unless $@ eq "alarm\n";
			print "\n";
			return(0);
		} else {
			if ($confirmation =~ /^y/i) { return 1; }
			elsif ($confirmation =~ /^n/i) { return 0; }
		}
	}
}


#
# Get User Options.
#
sub GetOpts
{

	GetOptions(
		'b=s'      => \$bankName,
		'f'        => \$force,
		'help|h|?' => \$help,
		'man'      => \$man,
		'n=s'      => \$jobList,
		'u=s'      => \$userName,
		'v'        => \$verbose,
	) or pod2usage(2);

#
#	Display usage.
#
	if ($help) {
		printf("\n");
		pod2usage(-verbose => 0, -exit => 'NOEXIT', -output => \*STDOUT);
		print "Report problems to LC Hotline.\n\n";
		exit(0);
	}

#
#	Display man page.
#
	if ($man) {
		if ($< == 0) {  # Cannot invoke perldoc as root
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
		printf("\n");
		pod2usage(-exitstatus => 0, -verbose => 2);
	}

#
#	Use single argument as job id or fail if extra args.
#
	if (@ARGV) {
		if (!$jobList && @ARGV == 1) {
			$jobList = shift @ARGV;
		} else {
			pod2usage(2);
		}
	}

	return;
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

prel - releases jobs in a familiar lcrm format

=head1 SYNOPSIS

B<prel> [B<-b> I<bank_name>] [B<-f>] [I<jobid> | B<-n> I<jobid_list>] [B<-u> I<user_name>] [B<-v>] [B<-h, -?, --help>] [B<--man>]

=head1 DESCRIPTION

The B<prel> command is used to release jobs that are on hold.

If B<prel> is issued without specifying one of the B<-b>, B<-u> or B<-n> flags, all jobs owned by the calling user will be targetted for release.

If the B<-f> option is not specified, each selected job is released only upon confirmation for that job. The confirmation messages list the job id, the job owner name and the bank name of selected job. A reply of y[es] or n[o] is required. If a message is not input within ten seconds, a "timeout" message is sent to the terminal and prel terminates.

=head1 OPTIONS

=over 4

=item B<-b> I<bank_name>

Holds only jobs that are drawing from the specified bank.

=item B<-f>

Causes jobs to be released without asking for confirmation.
 
=item B<-n> I<job_id_list>

Hold only the jobs with jobids that correspond to one of the items in I<job_id_list>. I<job_id_list> must be a comma-separated list of jobids. This option may not be used in conjunction with either the B<-b> or B<-u> options.

=item B<-u> I<user_name>

Hold only jobs that are owned by the specified user.

=item B<-v>

Verbose mode. Show the jobs that were released and write error messages to standard error.

=item B<-h, -?, --help>

Display a brief help message

=item B<--man>

Display full documentation

=back

=head1 EXAMPLE

prel -v

prel -b projectA

prel -n PBS.1234.0 -f

=head1 REPORTING BUGS

Report problems to LC Hotline.

=cut

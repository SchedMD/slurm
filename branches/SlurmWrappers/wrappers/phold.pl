#! /usr/bin/perl -w

#
# phold - hold SLURM jobs in an LCRM manner.
#
# Modified:	2010-07-27
# By:		Phil Eckert
#

#
# For debugging.
#
#use lib "/var/opt/slurm_banana/lib64/perl5/site_perl/5.8.8/x86_64-linux-thread-multi/";

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
use Slurm;

my ($cmd, $rc, $output);

my (
    $bankName, $force,     $help,     $man,
    $jobList,   $userName, $verbose
);

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
# Get user Options.
#
GetOpts();

#
# Query the jobs.
#
my $tmp = Slurm->load_jobs();
unless($tmp) {
	my $errmsg = Slurm->strerror();
	print "Error loading nodes: $errmsg";
}

my $worstRc = 0;
my ($jobId, $user, $account, $state, $allocPartition, $reason);

foreach my $jobs (@{$tmp->{job_array}}) {

	$jobId   = $jobs->{job_id};
	$user    = getpwuid($jobs->{user_id});
	$account = $jobs->{account} || "N/A";
	$reason  = Slurm::job_reason_string($jobs->{state_reason}) || 'N/A';
	$state   = Slurm::job_state_string($jobs->{job_state}) || 'N/A';
	next if ($state eq "CANCELLED" || 
		 $state eq "RUNNING" || 
		 $state eq "TIMEOUT" ||
		 $reason =~ /Held/);

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
	$cmd = "scontrol hold $jobId";
	$output = `$cmd 2>&1`;
	$rc     = $? >> 8;
	$worstRc  = $rc if $rc > $worstRc;

#
#	Display and log error output.
#
	print "Job $jobId held\n";
}

#
# Exit with worst seen status code.
#
exit($worstRc);


#
# $boolean = confirm()
# Confirm the job hold with y[es] or n[o]
# Timout after 10 seconds
#
sub confirm
{
	print "hold ", lc($state), " job $jobId ($user, $account)?";
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
			return 0;
		} else {
			if ($confirmation =~ /^y/i) { return 1; }
			elsif ($confirmation =~ /^n/i) { return 0; }
		}
	}

	return;
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
		pod2usage(-verbose => 0, -exit => 'NOEXIT', -output => \*STDOUT);
		print "Report problems to LC Hotline.\n";
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

##############################################################################

__END__

=head1 NAME

phold - holds SLURM jobs in a familiar lcrm format

=head1 SYNOPSIS

B<phold> [B<-b> I<bank_name>] [B<-f>] [I<jobid> | B<-n> I<jobid_list>] [B<-u> I<user_name>] [B<-v>] [B<-h, -?, --help>] [B<--man>]

=head1 DESCRIPTION

The B<phold> command is used to hold jobs. A job that is held is ineligible to run until the hold is released.

If B<phold> is issued without specifying one of the B<-b>, B<-u> or B<-n> flags, all jobs owned by the calling user will be targetted for being held.

If the B<-f> option is not specified, each selected job is held only upon confirmation for that job. The confirmation messages list the job id, the job owner name and the bank name of selected job. A reply of y[es] or n[o] is required. If a message is not input within ten seconds, a "timeout" message is sent to the terminal and phold terminates.

=head1 OPTIONS

=over 4

=item B<-b> I<bank_name>

Holds only jobs that are drawing from the specified bank.

=item B<-f>

Causes jobs to be held without asking for confirmation.
 
=item B<-n> I<job_id_list>

Hold only the jobs with jobids that correspond to one of the items in I<job_id_list>. I<job_id_list> must be a comma-separated list of jobids. This option may not be used in conjunction with either the B<-b> or B<-u> options.

=item B<-u> I<user_name>

Hold only jobs that are owned by the specified user.

=item B<-v>

Verbose mode. Show the jobs that were held and write error messages to standard error.

=item B<-h, -?, --help>

Display a brief help message

=item B<--man>

Display full documentation

=back

=head1 EXAMPLE

phold -v

phold -b projectA

phold -n PBS.1234.0 -f

=head1 REPORTING BUGS

Report problems to LC Hotline.

=cut


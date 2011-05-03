#! /usr/bin/perl 
#

#
# sjobexitmod
#
# Author:        Phil Eckert
# Date:          10/28/2010
# Last Modified: 10/28/2010
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
use autouse 'Pod::Usage' => qw(pod2usage);
use File::Basename;

my (
	$base,   $help,   $cluster,   $code,   $execute_line,
	$jobid,  $list,   $man,       $reason
);

#
# Format for listing job.
#
my $list_format = "JobID,Account,NNodes,NodeList,State,ExitCode,DerivedExitCode,Comment";


#
# Get options.
#
getoptions();

my $rval;

#
# Exexute the utility.
#
$rval = `$execute_line 2>&1`;

#
# Determine if Successful.
#
my $status = $?;

if ($status == 0) {
	printf("\n Modification of job $jobid was successful.\n\n");
	exit(0);
} else {
	printf("\n $rval\n");
	exit($status);
}



sub getoptions
{
	my $argct = $#ARGV;

#
#	Set default partition name.
#

	GetOptions(
		'help|h|?'   => \$help,
		'man'        => \$man,
		'e=s'        => \$code,
		'r=s'        => \$reason,
		'c=s'        => \$cluster,
		'l'          => \$list,
	) or usage();

#
#	Fix the exit code (if set) to reflect the 
#	fact that it represents the leftmost 8 bits
#	of the integer field.
#
	$code = 256  * ($code & 0xFF) if ($code);

#
#
#	Display a simple help package.
#
	usage() if ($help);

	show_man() if ($man);

#
#       Make sure there is a  job id, and make  sure it is numeric.
#
	if (!($jobid = shift(@ARGV)) || !isnumber($jobid)) {
		printf("\n Job Id needed.\n\n");
		usage();
	}

#
#	List option was selected.
#
	if ($list) {
		die(" \n  wrong use of list option, format is ' $base -l JobId'\n\n") if ($argct != 1);
		system(" sacct -X -j $jobid -o $list_format");
		exit(0);
	}

#
#	Check for required options.
#
	if (!$reason && !$code) {
		printf("\n Either reason string or exit code required.\n\n");
		exit(1);
	}

#
#	Build execute line from the options that are set.
#
	$execute_line = "sacctmgr -i modify job jobid=$jobid set";

	$execute_line .= " Comment=\"$reason\""        if ($reason);
	$execute_line .= " DerivedExitCode=$code"      if ($code);
	$execute_line .= " Cluster=$cluster"           if ($cluster);

	return;
}


#
# Simple check to see if number is an integer,
# retrun 0 if it is not, else return 1.
#
sub isnumber
{
	my ($var) = $_;

	if ($var !~ /\D+/) {
		return(1); #if it is just a number.
	} else {
		return(0); #if it is not just a number.
	}
}


sub usage
{
	my $base = basename($0);

	printf("\
 Usage: $base [-e <exit code>] [-r <reason string>] [-c <cluster>] JobId
        $base -l JobId
        $base [-h]
        $base [-man]

	-e <exit code>		Modify the derived exit code to new value.
	-r <reason string>	Modify the job's comment field to new value.
	-c <cluster>		Name of cluster (optional).
	-l 			List information for a completed job.
	-h 			Show usage.
	JobId			The identification number of the job.
	-man 			Show man page.

\n");

	exit;
}

sub show_man
{

	if ($< == 0) {    # Cannot invoke perldoc as root
		my $id = eval { getpwnam("nobody") };
		$id = eval { getpwnam("nouser") } unless defined $id;
		$id = -2                          unless defined $id;
		$<  = $id;
		printf("\n You can not do this as root!\n\n");
		exit 1;
	}
	$> = $<;                         # Disengage setuid
	$ENV{PATH} = "/bin:/usr/bin";    # Untaint PATH
	delete @ENV{'IFS', 'CDPATH', 'ENV', 'BASH_ENV'};
	if ($0 =~ /^([-\/\w\.]+)$/) { $0 = $1; }    # Untaint $0
	else { die "Illegal characters were found in \$0 ($0)\n"; }
	pod2usage(-exitstatus => 0, -verbose => 2);

	return;
}

__END__


=head1 NAME

B<sjobexitmod> - Modifies a completed job in the slurmdbd

=head1 SYNOPSIS

       sjobexitmod	[-e exit_code] [-r reason_string] [-c cluster_name] JobId 
       sjobexitmod	-l JobId 
       sjobexitmod	-h
       sjobexitmod	-man

=head1 DESCRIPTION

 sjobexitmod is a wrapper which effectively does the same operation as using the
 sacct utility to modify certain aspects of a completed job.

	sacctmgr -i modify job jobid=1286 set DerivedExitCode=1 Comment="code error"

 or to list certain aspects of a completed job.

	sacct -o jobid,derivedexitcode,comment,cluster

=head1 OPTIONS

=over 4

=item B<-h>

A usage summary message is displayed, and sjobexitmod terminates.

=item B<-man>

Show the man page for this utility..

=item B<-c> I<cluster_name>

The name of the cluster the job ran on.

=item B<-e> I<exit_code>

The exit code (DerivedExitCode) to be used.

=item B<-l> I<JobID>

List selected attributes of a completed job.

=item B<-r> I<reason_string>

The reason (Comment) for job termination.

=item B<JobId>

the numeric job id.

=back

=head1 EXIT CONDITIONS

If there is an error, sjobexitmod returns either the  exit status returned by sacctmgr,
or a non-zero value.

=head1 AUTHOR

Written by Philip D. Eckert

=head1 REPORTING BUGS

Report bugs to <pdesr@llnl.gov>

=head1 SEE ALSO

sacctmgr,sacct


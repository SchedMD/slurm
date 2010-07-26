#! /usr/bin/perl 
#
# Convert mjobctl commands into slurm commands.
#
# Author:Phil Eckert
# Date: 2010-06-22
#

#
# For debugging.
#
my $debug = 0;

use Getopt::Long 2.24 qw(:config no_ignore_case);
use autouse 'Pod::Usage' => qw(pod2usage);
use strict;

BEGIN {
#
#	Just dump the man page in *roff format and exit if --roff specified.
#
	foreach my $arg (@ARGV) {
		last if ($arg eq "--");
		if ($arg eq "--roff") {
			use Pod::Man;
			my $parser = Pod::Man->new (section => 1);
			$parser->parse_from_file($0, \*STDOUT);
			exit 0;
		}
	}
}

#
# Standard options, variables and arrays.
#

my (
	$cancel, $help,    $hold,   $jobid,
	$man, 	 $modify,  $unhold, $query,
	$resume, $requeue, $signal, $suspend
);

#
# Slurm Version.
#
chomp(my $soutput = `sinfo --version`);
my ($sversion) = ($soutput =~ m/slurm (\d+\.\d+)/);

#
# Get user options.
#
GetOpts();

#
# If Slurm is earlier than 2.2.
#
if ($sversion < 2.2)&&  ($hold || $unhold)) {
	printf("\n Hold/Release functionality not available in this release.\n\n");
	exit(1);
}

executei("scontrol update job=$jobid prio=0")	if ($hold);
execute("scontrol update job=$jobid prio=1")	if ($unhold);
execute("scontrol requeue $jobid")		if ($requeue);
execute("scontrol resume $jobid")		if ($resume);
execute("scontrol suspend $jobid")		if ($suspend);
execute("scontrol show job $jobid")		if ($query);
execute("scancel $jobid")			if ($cancel);
execute("scancel -s $signal  $jobid")		if ($signal);

do_modify()  if ($modify);

exit(0);


#
# Modify an attribute of a job.
#
# A little more complex, since we need to translate Moab
# options to those of SLURM.
#
sub do_modify
{
#
#	+= doesn't work in SLURM, will add routine
#	to calculate and adjust later.
#

	if ($modify =~ /\-/ || $modify =~ /\+/) {
		printf(" -- can not use += or -= in modify.\n");
		exit(1);
	}

#
#	Now split the args into type, value
#
	my ($type,$value)  = split(/=/, $modify);

#
#	Do some remapping of types for SLURM.
#
	$type =~ s/class/partition/     if ($type =~ /class/);
	$type =~ s/jobname/name/        if ($type =~ /jobname/);
	$type =~ s/nodes/numnodes/      if ($type =~ /nodes/);
	$type =~ s/userprio/prio/       if ($type =~ /userprio/);
	$type =~ s/wclimit/timelimit/   if ($type =~ /wclimit/);

#
#	Now do it.
#
	execute("scontrol update job=$jobid $type=$value");
}


#
# Use arrays, when appropriate, for the options, since somemany
# will have two or more elements.
# 
sub GetOpts
{

#
#	Could have written my own parser, but it is easier to
#	manipulate the arrays for what needs to be done.
#

#
#	Remove any "types" from hold and unhold.
#
	@ARGV = grep { $_ !~ /user/   } @ARGV if (grep /\-u/, @ARGV);
	@ARGV = grep { $_ !~ /batch/  } @ARGV if (grep /\-u/, @ARGV);
	@ARGV = grep { $_ !~ /system/ } @ARGV if (grep /\-u/, @ARGV);

	@ARGV = grep { $_ !~ /user/   } @ARGV if (grep /\-h/, @ARGV);
	@ARGV = grep { $_ !~ /batch/  } @ARGV if (grep /\-h/, @ARGV);
	@ARGV = grep { $_ !~ /system/ } @ARGV if (grep /\-h/, @ARGV); 
#
#	Remove extra options to the query.
#
	@ARGV = grep { $_ !~ /diag/      } @ARGV if (grep /\-q/, @ARGV);
	@ARGV = grep { $_ !~ /hostlist/  } @ARGV if (grep /\-q/, @ARGV);
	@ARGV = grep { $_ !~ /starttime/ } @ARGV if (grep /\-q/, @ARGV);

	GetOptions(
		'help'		=> \$help,
		'man'		=> \$man,
		'F'		=> \$cancel,
		'c'		=> \$cancel,
		'h'		=> \$hold,
		'u'		=> \$unhold,
		'q'		=> \$query,	
		'r'		=> \$resume,
		'R'		=> \$requeue,
		's'		=> \$suspend,
		'N=s'		=> \$signal,
		'm=s'		=> \$modify,
	);

#
#	Display a simple help package.
#
	usage() if ($help);
#

#	The oly thing left, should be the job id. Make sure it is numberic.
#
	if (!($jobid = shift(@ARGV)) || !isnumber($jobid)) {
		printf("\n Job Id needed.\n\n");
		usage();
	}

#
#	If there are any more arguments left over, then it
#	an option was used incorrectly and we will return
#	a format error.
#
#	my $extra = pop @ARGV;	
	if (my $extra = pop @ARGV) {
		printf("\n Format Error.\n\n");
		usage();
	}

#
#	Display man page if requested.
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

	return;
}


sub usage
{
	printf("\n");
	pod2usage(-verbose => 0, -exit => 'NOEXIT', -output => \*STDOUT);
	print "Report problems to LC Hotline.\n\n";

	exit(0);
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


#
# Execute and exit.
#
# (or print the execute line if debug
#  is turned on.)
#
sub execute
{
	my ($line) = @_;

	if ($debug) {
		printf("\n exeline: $line\n\n");
		exit(0);
	}

	my $result = `$line 2>&1`;
	my $status = $?;

	$status = 1 if ($status != 0);
	printf("$result\n");

	exit($status);
}


__END__

=head1 NAME

B<mjobctl> - modify batch job.

=head1 SYNOPSIS

 mjobctl --help 
 mjobctl -c jobexp
 mjobctl -h jobexp
 mjobctl -m attr{=}val jobid
 mjobctl -N [<SIGNO>] jobid
 mjobctl -q jobid
 mjobctl -r jobid
 mjobctl -R jobid
 mjobctl -s jobid
 mjobctl -u  jobexp

=head1 DESCRIPTION

 The B<mjobctl> command allows the user to modify attribultes of their jobs.

=head1 OPTIONS

=over 4

=item B<--help>

Show help.

=item B<-c> I<jobid>

Cancel a job.

=item B<-h> I<jobid>

Hold a job.

=item B<-m> I<attribute=value> I<jobid>

Modify an attribute of a job.

=over 4

 mjobctl -m account=bofa 1045
 mjobctl -m class=pbatch 1045
 mjobctl -m depend=none 1045
 mjobctl -m depend=1044 1045
 mjobctl -m depend=1043 1045
 mjobctl -m depend=1042:jobstart:1043:1044 1045
 mjobctl -m jobname=24run 1045
 mjobctl -m nodes=2 1045
 mjobctl -m qos=standby 1045
 mjobctl -m wclimit=01:00:00 1045

=back

(Note: unning jobs can not be modified except for the time limit,
and that can only be modified downward, except by an administrator.)

=item B<-N> I<signal> I<jobid>

Send a signal to a running job.

=item B<-q> I<jobid>

Query a job.

=item B<-r> I<jobid>

Resume a job.

=item B<-R> I<jobid>

Requeue a job.

=item B<-s> I<jobid>

Suspend a job.


=item B<--man>

Display full documentation

=back

=head1 REPORTING BUGS

Report problems to LC Hotline.

=cut


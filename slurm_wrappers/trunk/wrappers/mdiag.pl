#! /usr/bin/perl
#
# Convert mjobctl commands into slurm commands.
#
# Last Update: 2010-07-27
#
# Copyright (C) 2010 Lawrence Livermore National Security.
# Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
# Written by Philip D. Eckert <eckert2@llnl.gov>
# CODE-OCEC-09-009. All rights reserved.
#

#
# For debugging.
#
my $debug=0;

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
# Define the options variables.
#
my (
	$account,	$class,		$fairshare,	$help,	$jobs,
	$man,		$nodes,		$part,		$prio,
	$qos,		$resv,		$user,		$verbose,
	$host_name
);

my $tmp = `scontrol show config | grep ClusterName`;
my ($host) = ($tmp =~ m/ = (\S+)/);
chomp($host = `nodeattr -v cluster`) if ($host =~ /null/);

#
# Slurm Version.
#
chomp(my $soutput = `sinfo --version`);
my ($sversion) = ($soutput =~ m/slurm (\d+\.\d+)/);

#
# Get input from user.
#
GetOpts();

#
# Process the request.
#
do_fairshare() if ($fairshare);
do_prio()      if ($prio);

do_account()   if (defined $account);
do_class()     if (defined $class);
do_nodes()     if (defined $nodes);
do_jobs()      if (defined $jobs);
do_resv()      if (defined $resv);
do_user()      if (defined $user);
do_qos()       if (defined $qos);

#
# IMPORTANT WARNING:
#
# Make sure the partition is the last option checked for,
# since in Moab it can be an auxillary option which is
# not needed when dealing with a cluster. More work
# will be needed when SLURM is able to manage clsuters.
#
do_part()      if (defined $part);

exit;


#
# Show one, or all.
#
sub do_fairshare
{
	my $line = "sshare";
	$line .= "  -a"  if ($verbose);

	execute($line);
}


#
# Show all or one.
#
sub do_nodes
{
	my $line = "scontrol show node";
	$line .= "  $nodes"  if ($nodes);
	$line .= "  -v"      if ($verbose);

	execute($line);
}


#
# Short or long form.
#
sub do_prio
{
	my $line ="sprio";
	$line .= "  -l"  if ($verbose);

	execute($line);
}


#
# Show all or one.
#
sub do_account
{
	my $line = "sacctmgr show account witha format=Account,Cluster,User,FairShare";
	$line .= " account=$account"    if ($account);
	$line .= " cluster=$host_name"  if ($host_name);

	execute($line);
}


#
# Show all or one.
#
sub do_jobs
{
	my $line = "squeue ";
	$line .= "  -j $jobs"  if ($jobs);
	$line .= "  -l"        if ($verbose);

	execute($line);
}


#
# Show all or one.
#
sub do_class
{
	my $line = "scontrol show part";
	$line .= " $class"  if ($class);

	execute($line);
}


#
# Show all or one.
#
sub do_resv
{
	my $line = "scontrol show reservation";
	$line .= " $resv" if ($resv);

	execute($line);
}


#
# Show all or one.
#
sub do_user
{
	my $line = "sacctmgr  show user witha format=User,DefaultAccount,Cluster,Account,FairShare,MaxJobs,MaxNodes,MaxSubmit,MaxWall,QOS%25s";
	$line .= " user=$user"  if ($user);
	$line .= " cluster=$host_name"  if ($host_name);

	execute($line);
}


#
# Show all or one.
#
sub do_qos
{
	my $line = "sacctmgr show qos ";
	$line .= " qos=$qos" if ($qos);
	$line .= " cluster=$host_name"  if ($host_name);

	execute($line);
}


#
# Use arrays, when appropriate, for the options, since somemany
# will have two or more elements.
# 
sub GetOpts
{

	pod2usage(2) if ($#ARGV <= 0);

	GetOptions (
		'h|help'	=> \$help,
		'man'		=> \$man,
		'f'		=> \$fairshare,
		'p'		=> \$prio,
		'v'		=> \$verbose,
		'a:s'		=> \$account,
		'n:s'		=> \$nodes,
		'j:s'		=> \$jobs,
		'c:s'		=> \$class,
		'q:s'		=> \$qos,
		'r:s'		=> \$resv,
		't:s'		=> \$host_name,
		'u:s'		=> \$user,
	) or pod2usage(2);

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

#
#	Show help.
#

	if ($help) {
		pod2usage(-verbose => 0, -exit => 'NOEXIT', -output => \*STDOUT);
		print "Report problems to LC Hotline.\n\n";
		exit(0);
	}

	return;
}


#
# Execute and return status and results.
#
sub execute
{
	my ($line) = @_;

	if ($debug) {
		printf("\n exeline: $line\n\n");
		exit(0);
	}

	my $result = `$line 2>&1`;
	if ($result =~ /not running a supported accounting_storage plugin/) {
		printf("\n Optio not supported on this system.\n\n");
		exit(1);
	}
	my $status = $?;

	printf("\n$result\n");

	exit($status);
}


__END__

=head1 NAME

B<mdiag> - Display information about aspects of the cluster. 

=head1 SYNOPSIS

 mdiag -a [accountid] [-t host]
 mdiag -c [classid]
 mdiag -f [-v]
 mdiag -j [jobid] [-v]
 mdiag -n [nodeid] [-v]
 mdiag -p [-v] 
 mdiag -q [qosid] [-t host]
 mdiag -r [reservationid] 
 mdiag -u [userid] [-t host]


=head1 DESCRIPTION

 The B<mdiag> utility is used to display information about aspects of the cluster. 

=head1 OPTIONS

=over 4

=item B<--help>

Show help.

=item B<-a> I[<accountid>]

Display information about user account names.

=item B<-c> I[<classid>]

Display information aobut classes.

=item B<-f> 

Display fairshare information.

=item B<-j> I[<jobid>]

Display job information.

=item B<-n>

Display node information.

=item B<-p>

Display job priorities.

=item B<-r> I[<reservationid>]

Requeue a job.

=item B<-t> I[<host name>]

specify a host name (only applies to -a, -q and -u options).

=item B<-u> I[<user>]

Display user information.

=item B<--man>

Display man page for this utility.

=back

=head1 REPORTING BUGS

Report problems to LC Hotline.

=cut


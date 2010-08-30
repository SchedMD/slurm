#!/usr/bin/perl 
############################################################################
# Purpose: Wrapper for Slurm to a format similar to Moab checkjob
#
#
############################################################################
# Copyright (C) 2010 Lawrence Livermore National Security.
# Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
# Written by Joseph Donaghy <donaghy1@llnl.gov>
# CODE-OCEC-09-009. All rights reserved..
############################################################################
use strict;
use Getopt::Long;
use autouse 'Pod::Usage' => qw(pod2usage);


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

Main:
	my ($jid,
	    @jid,
	    $usage,
	    $man,
	    $slurmout,
	    @slurmout);

if(!GetOptions( "help|usage|?"=>\$usage,
	       "jid=i"=>\@jid,
	       "v"=>\@jid,
	       "man"=>\$man,
	       "s=i"=>\$slurmout,)) {
	exit(1);
}

############### Display the man page.
#
if ($man)
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

############### Display command usage information.
#
if ($usage)
{
	my $eval = shift(@_);

#
#	Print usage instructions and exit.
#
	print STDERR "\nUsage: checkjob [-h] p\[-man] [-s] jobID\n";

	printf("\
   -h	displays command usage information.
   -man	displays man page.
   -s	displays output in familiar SLURM format.\n

	\n\n");
	
	exit($eval);
}


############### Display the SLURM output.
#
if ($slurmout)
{
	my $eval = shift(@_);
	my @sin = `scontrol show job $slurmout`;
	printf("@sin\n");
	exit($eval);
}
#
############### Display in a Moab-like output.
#
if (@ARGV[0] =~ /^[0-9]+$/)
{
	my $jid = @ARGV[0];
	my $sin = `scontrol -o show job $jid 2>&1`;

	if ($sin =~ "error: Invalid job id specified") { 
			   die "error: Invalid job id specified\n";
		       }

	my ($JobId, $Name, $UserId, $GroupId, $Priority, $Account, $QOS, 
	    $JobState, $Reason, $Dependency, $TimeLimit, $Requeue, $Restarts, 
	    $BatchFlag, $ExitCode, $SubmitTime, $EligibleTime, $StartTime, 
	    $EndTime, $SuspendTime, $SecsPreSuspend, $Partition, $AllocNode, 
	    $ReqNodeList, $ExcNodeList, $NodeList, $NumNodes, $NumCPUs, 
	    $CPUsTask, $ReqSCT, $MinCPUsNode, $MinMemoryNode, $MinTmpDiskNode, 
	    $Features, $Reservation, $Shared, $Contiguous, $Licenses, $Network, 
	    $Command, $WorkDir, $Comment);

	        ($JobId) = ($sin =~ m/^JobId=(\S+)/);
		($Name) = ($sin =~ m/ Name=(\S+)/);
		($UserId) = ($sin =~ m/ UserId=(\S+)/);
		($GroupId)  = ($sin =~ m/ GroupId=(\S+)/);
		($Priority)   = ($sin =~ m/ Priority=(\S+)/);
		($Account)  = ($sin =~ m/ Account=(\S+)/);
		($QOS) = ($sin =~ m/ QOS=(\S+)/);
		($JobState)  = ($sin =~ m/ JobState=(\S+)/);
		($Reason)  = ($sin =~ m/ Reason=(\S+)/);
		($Dependency)  = ($sin =~ m/ Dependency=(\S+)/);
		($TimeLimit)  = ($sin =~ m/ TimeLimit=(\S+)/);
		($Requeue)  = ($sin =~ m/ Requeue=(\S+)/);
		($Restarts)  = ($sin =~ m/ Restarts=(\S+)/);
		($BatchFlag)   = ($sin =~ m/ BatchFlag=(\S+)/);
		($ExitCode)  = ($sin =~ m/ ExitCode=(\S+)/);
		($SubmitTime)   = ($sin =~ m/ SubmitTime=(\S+)/);
		($EligibleTime)  = ($sin =~ m/ EligibleTime=(\S+)/);
		($StartTime)  = ($sin =~ m/ StartTime=(\S+)/);
		($EndTime)  = ($sin =~ m/ EndTime=(\S+)/);
		($SuspendTime)   = ($sin =~ m/ SuspendTime=(\S+)/);
		($SecsPreSuspend)  = ($sin =~ m/ SecsPreSuspend=(\S+)/);
		($Partition)  = ($sin =~ m/ Partition=(\S+)/);
		($AllocNode)  = ($sin =~ m/ AllocNode:(\S+)/);
		($ReqNodeList)   = ($sin =~ m/ ReqNodeList=(\S+)/);
		($ExcNodeList)  = ($sin =~ m/ExcNodeList=(\S+)/);
		($NodeList)  = ($sin =~ m/ NodeList=(\S+)/);
		($NumNodes)   = ($sin =~ m/ NumNodes=(\S+)/);
		($NumCPUs)   = ($sin =~ m/ NumCPUs=(\S+)/);
		($CPUsTask) = ($sin =~ m/ CPUs\/Task=(\S+)/);
		($ReqSCT)  = ($sin =~ m/ ReqS:C:T=(\S+)/);
		($MinCPUsNode)   = ($sin =~ m/ MinCPUsNode=(\S+)/);
		($MinMemoryNode)   = ($sin =~ m/ MinMemoryNode=(\S+)/);
		($MinTmpDiskNode)  = ($sin =~ m/ MinTmpDiskNode=(\S+)/);
		($Features)   = ($sin =~ m/ Features=(\S+)/);
		($Reservation)  = ($sin =~ m/ Reservation=(\S+)/);
		($Shared)  = ($sin =~ m/ Shared=(\S+)/);
		($Contiguous)   = ($sin =~ m/ Contiguous=(\S+)/);
		($Licenses)   = ($sin =~ m/ Licenses=(\S+)/);
		($Network)  = ($sin =~ m/ Network=(\S+)/);
		($Command) = ($sin =~ m/ Command=(\S+)/);
		($WorkDir)  = ($sin =~ m/ WorkDir=(\S+)/);
		($Comment)  = ($sin =~ m/ Comment=(\S+)/);

			   printf("job $JobId\n\n");
			   printf("AName: $Name\n");
			   printf("State: $JobState\n");
			   printf("Creds:  user:$UserId  group:$GroupId  account:$Account  class:$Partition  qos:$QOS\n");
			   printf("TimeLimit:  $TimeLimit\n");
			   printf("SubmitTime: $SubmitTime\n");
			   printf("Eligible:   $EligibleTime\n\n");
			   printf("StartTime: $StartTime\n");
			   printf("Total Requested Nodes:  $NumNodes\n");
			   printf("Depend:    $Dependency\n\n");
			   printf("Partition: $AllocNode\n");
			   printf("Dedicated Resources Per Task: $Licenses\n");

	if ($Shared == 0) { 
			   printf("Node Access: SINGLEJOB\n");
		       }
	else {
			   printf("Node Access: SHARED\n");
	   }

			   printf("Number of Nodes: $NumNodes\n\n");
			   printf("Task Distribution: \n$NodeList\n\n");
			   printf("Working Directory:  $WorkDir\n");
			   printf("Executable: $Command\n\n");
			   printf("Priority: $Priority\n\n");
			   printf("NOTE: $Comment\n\n");
}
	else {
	    			   printf("No recognizable Job ID specified\n");
			       }

__END__

=head1 NAME

B<checkjob> - Displays job information through SLURM processing.

=head1 SYNOPSIS

B<checkjob> [<jobID>]

=head1 DESCRIPTION

The B<checkjob> command displays information of jobs using the SLURM utility 
scontrol. Information includes status, associations, various time parameters, 
and resources. The checkjob is based upon the SLURM utility scontrol show job
and the default output provides a Moab-like format. View the man pages for 
scontrol and use it for even more information and greater depth of understanding 
of the specified job.

=head1 OPTIONS

=over 4

=item B<-h>   Display a brief help message

=item B<-s>   Display in familiar SLURM scontrol format.

=item B<-man> Display the man page.


=head1 OUTPUT FIELD DESCRIPTIONS

=item B<account>

The accounting name.

=item B<AName>

The specified name given to the job.

=item B<class>

The class name is equivalent to the Slurm partition name.

=item B<Dedicated Resources Per Task>

Resources requirement options.

=item B<Depend>

Job deferred until another specified job acquires state.

=item  B<Eligible>

Predicition of the job initiation time.

=item B<Executable>

The name of the command to run.

=item B<Group>

The unix group ID under which the job was submitted.

=item B< job   >

The SLURM generated identifying (ID) value assigned to a job. 

=item B<Node Access>

The ability to share nodes with other jobs.

=item B<Note>

The system resources comments.

=item B<Number of Nodes>

The minimum and optionally maximum count of nodes to be allocated.

=item B<Partition>

The name of the partition.

=item B<Priority>

The priority value of the job.  
Note that a job priority of zero prevents the job from ever being scheduled.

=item B<qos>

The type of Quality of Service for the job.

=item B<Task Distribution>

The list of nodes allocated to the job.

=item B< Total Requested Nodes>

The minimum and optionally maximum count of nodes to be allocated.

=item B<StartTime>

The initiation time of the job.

=item B<SubmitTime>

The time that the job was first entered to SLURM for run consideration.

=item B<TimeLimit>

The time limit, or wall clock, of the job.

=item B<State>

The current state of the job.

=item B<user>

The unix user ID under which the job was submitted.

=item B<Working Directory>

The directory location that the executable runs.


=head1 EXAMPLE

 The following is the output for job number 349139.

    > checkjob 101528
 job 101528

 AName: job.sw6QiF
 State: RUNNING
 Creds:  user:juser(62222)  group:jgroup(62222)  account:science  class:pbatch  qos:normal
 TimeLimit:  08:00:00
 SubmitTime: 2010-07-14T07:47:24
 Eligible:   2010-07-14T07:47:24

 StartTime: 2010-07-14T07:47:49
 Total Requested Nodes:  4
 Depend:    (null)

 Partition: Sid=atlas1:31464
 Dedicated Resources Per Task: (null)
 Node Access: SINGLEJOB
 Number of Nodes: 4

 Task Distribution: 
 atlas[40,43-45]

 Working Directory:  /home/juser/Atoms/RUN8
 Executable: /var/spool/job.sw6QiF

 Priority: 100000000

 NOTE: 'SJID:101528?QOS:normal?gres=lscratcha:1cratchb:1cratchc:1cratchd:1'


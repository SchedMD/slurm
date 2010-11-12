#! /usr/bin/perl

#
# Convert msub commands in to slurm commands.
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
my $debug = 0;

use Getopt::Long 2.24 qw(:config no_ignore_case require_order);
use autouse 'Pod::Usage' => qw(pod2usage);
use Time::Local;
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
# standard options.
#
my (
	$account,	$class, 	$dpath, 	$epath,	
	$help,		$join,		$man, 		$nope, 		
	$jobname,	$mailoptions,	$mailusers,	$nodes,
	$opath,		$priority,	$rerun, 	$shellpath,
	$silent, 	$slurm,		$starttime, 	$variable,
	$vlist,		$wclim,		$minwclim
);

#
# -l extension options.
#
my (
	$advres, 	$dmem,		$ddisk,
	$depend, 	$feature,	$gres,
	$flags, 	$maxmem, 	$jobname, 
	$host, 		$procs,		$qos,
	$resfail, 	$signal,	$tpn,
	$ttc
);

my (@lreslist, @slurmArgs, @xslurmArgs, @nargv);
my ($scriptFile, $scriptArgs, $tmpScriptFile, $command, $flag);

#
# Slurm Version.
#
chomp(my $soutput = `sinfo --version`);
my ($sversion) = ($soutput =~ m/slurm (\d+\.\d+)/);

GetOpts(@ARGV);


#
# Display man page if requested.
#
usage() if ($help);

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
# Allow --slurm args after scriptname. Split ARGV into two arrays
# if there is a --slurm argument. The first array will replace
# ARGV and the second if any, will be arguments to sbatch.
#

foreach my $arg (@ARGV) {
	if ($arg eq "--slurm") {
		$flag = 1;
		next;
	}
	if (!$flag) {
		push @nargv, $arg;
	} else {
		push @xslurmArgs, $arg;
	}
}
@ARGV = @nargv;
	
#
# Save off ARGV so we can override the script directives later
#
my @SAVEDARGV = @ARGV;

if (@ARGV) {
	$scriptFile = shift;
	open SCRIPTIN, "< $scriptFile"
		or
		die("Unable to open job command file ($scriptFile) for reading: $!\n");
			$scriptArgs = join ' ', @ARGV if @ARGV;
}

#
# Otherwise read the job command file from STDIN
#
else {
	open SCRIPTIN, "< &STDIN";
	$tmpScriptFile = "/tmp/jobScript.lcrm.$$";
	open SCRIPTOUT, "> $tmpScriptFile"
		or die(
			"Unable to open temporary job command file ($tmpScriptFile) for writing: $!\n"
	);
}


#
# Parse job script
#
my @scriptDirectives = ();
my $assignedShell;
unless ($command) {
	my $lineNumber = 0;
	foreach my $line (<SCRIPTIN>) {
		$lineNumber++;
		print SCRIPTOUT $line if $tmpScriptFile;
		if ($lineNumber == 1 && $line =~ /^\s*#\s*!\s*(\S+)/) {
			$assignedShell = $1;
		} elsif ($line =~ s/^\s*#\s*MSUB\s+//) {
			chomp $line;
			$line =~ s/#.*//;    # Remove comments
			$line =~ s/\s+$//;   # Remove trailing whitespace
			my @args = split /\s+/, $line;
			my @args2 = ();
			my $quoter = 0;
			my $complete = "";
			foreach my $arg (@args) {
				my $first = substr $arg, 0, 1;
				if($quoter) {
					$complete .= " " . $arg;
					my $len = length $arg;
					my $last = substr $arg, $len-1, 1;

					if($last eq $quoter) {
						$complete =~ s/$quoter//g;
						push @args2, $complete;
						$quoter = 0;
						$complete = "";
					} 			    
				} elsif($first eq '\'' || $first eq '"') {
					$quoter = $first;
					$complete = $arg;
				} else {
					push @args2, $arg;			    
				} 
			}
			if($complete ne "") {
				$complete =~ s/$quoter//g;
				push @args2, $complete;
			}
			push @scriptDirectives, @args2;
		}
	}
}
close SCRIPTIN;
close SCRIPTOUT;

#
# Parse script directives
#
GetOpts(@scriptDirectives)
	or die("Invalid MSUB options found in job command file.\n");

#
# Parse command line arguments (overriding script directives)
#
GetOpts(@SAVEDARGV);

#
# Process all of the options from the -l lines.
#
if (@lreslist) {
	foreach my $tmp (@lreslist) {
		my @opts = split(/,/, $tmp);
		process(@opts);
	}
}

#
# Charge resources used by this job to specified account.
#
if ($account) {
	push @slurmArgs, "--account=$account";
}

#
# Charge resources used by this job to specified account.
#
if ($advres) {
	push @slurmArgs, "--reservation=$advres";
}

#
# Request a specific  partition (class, queue) for the resource allocation.
#
if ($class) {
	push @slurmArgs, "--partition=$class";
}

#
# Mimimum memory required per allocated CPU in MegaBytes.
#
if ($dmem) {
	push @slurmArgs, "--mem-per-cpu=$dmem";
}

#
# Equate ddisk option to tmp disk space.
#
if ($ddisk) {
	push @slurmArgs, "--tmp=$ddisk";
}

#
# If the depend is one we don't know, it is ignored..
#
if ($depend) {
	$depend =~ s/jobname/singleton/ if ($depend eq "jobname");
	push @slurmArgs, "--dependency=$depend" if (
		$depend =~ "after"      ||
		$depend =~ "afterany"   ||
		$depend =~ "afterok"    ||
		$depend =~ "afternotok" ||
		$depend =~ "afternotok" ||
		$depend =~ /^[0-9]+$/);
}

#
# The constraints are features that have been assigned to the
# nodes  by  the  slurm  administrator.
#
if ($feature) {
	push @slurmArgs, "--constraint=$feature";
}

#
# No real solution for going gres at this point.
# (waiting for 2.2 and lua)
#
#if ($gres) {
#        push @slurmArgs, "--licenses=$gres";
#} else {
#        my $tgres = GetGres();
#        push @slurmArgs, "--licenses=$tgres";
#}

#
# The host to run the job on, currently it can only
# be the local host, in the future one or more hsts
# can be specified.
#
# (Note: I commented this out, we will be in cluster
# mode only for the forseeable future, lets not worry
# about it now. -- Also, need to have the slurmacctd
# active for this to function.)
# 
if ($host) {
#	push @slurmArgs, "--clusters=$host";
}

#
# Specify  a name for the job allocation.
#
if ($jobname) {
	push @slurmArgs, "--job-name=$jobname";
}

#
# Specify the real memory required per node in MegaBytes.
#
if ($maxmem) {
	push @slurmArgs, "--mem=$maxmem";
}

#
# Request that a minimum of minnodes nodes be allocated to this job.
#
if ($nodes) {
	push @slurmArgs, "--nodes=$nodes";
}

#
# Priority (nice adjustment).
#
if ($priority) {
	push @slurmArgs, "--nice=$priority";
}

#
# Guarantees that SLURM will allocate enough resources for this number of tasks.
#
if ($procs) {
	push @slurmArgs, "--ntasks=$procs";
}

#
# Request a quality of service for the job.
#
if ($qos) {
	push @slurmArgs, "--qos=$qos";
}

#
# Tell SLURM how to handle a node failure.
# (Do no allow it to run again if rerun is set to no.)
#
if ($resfail) {
	$rerun = 'y' if (!defined $rerun);
	push @slurmArgs, "--requeue"    if ($resfail eq "requeue");
	push @slurmArgs, "--no-requeue" if ($resfail eq "cancel" || $rerun =~ /n/);
	push @slurmArgs, "--no-kill"    if ($resfail eq "ignore");
}

#
# When a job is within sig_time seconds of its end time, send it the signal sig_num. 
#
if ($signal) {
	push @slurmArgs, "--signal=$signal";
}

#
# Submit the batch script to the SLURM controller immediately, like normal, but
# tell the  controller to defer the allocation of the job until the specified time.
#
if ($starttime) {
	my $epoch_time = DateToEpoch($starttime);
	if ($sversion >= 2.2) {
		push @slurmArgs, "--begin=UTS$epoch_time";
	} else {
		my $newtime = fixtime($epoch_time);
		push @slurmArgs, "--begin=$newtime";
	}
}

#
# Specify a minimum number of logical cpus/processors per node.
#
if ($tpn) {
	push @slurmArgs, " --mincpus=$tpn";
}

#
# Guarantees that SLURM will allocate enough resources for this number of tasks.
#
if ($ttc) {
	push @slurmArgs, "--ntasks=$ttc";
}

#
# Set the working directory of the batch script to directory before it it executed.
#
if ($dpath) {
	push @slurmArgs, "--workdir=$dpath";
}

#
# Instruct SLURM to connect the batch script's standard output directly to the
# file name specified.
#
if ($opath) {
	push @slurmArgs, "--output=$opath";
}

#
# Instruct SLURM to connect the batch script's standard error directly to the
# file name specified.
#
if ($epath) {
	push @slurmArgs, "--error=$epath";
}

#
# Join stderr and stdout together.
# (If no stderr defined, all output goes to stdout.)
#
if ($join) {
	undefine $epath if ($epath);
}

#
# Handle mail options.
#
if ($mailoptions) {
	push @slurmArgs, "--mail-type=BEGIN" if ($mailoptions =~ /b/);
	push @slurmArgs, "--mail-type=END"   if ($mailoptions =~ /e/);
	push @slurmArgs, "--mail-type=ALL"   if ($mailoptions =~ /a/);
}

#
# Send mail, based on mail options, to the specified users.
#
if ($mailusers) {
	push @slurmArgs, "--mail-user=$mailusers";
}

#
# Request a minimum time limit for the job.
#
if ($vlist) {
	if ($sversion >= 2.2) {
		push @slurmArgs, "--export=$vlist";
	}
}

#
# Request a time limit for the job.
#
if ($wclim) {
	push @slurmArgs, "--time=$wclim";
}

#
# Request a minimum time limit for the job.
#
if ($minwclim) {
	if ($sversion >= 2.2) {
		push @slurmArgs, "--time-min=$minwclim";
	} else {
		printf("\n minwclim option not available in this version of SLURM.\n\n");
		exit(1);
	}
}



#
# Execute sbatch.
#
if ($debug == 1) {
	printf("sbatch args: '@slurmArgs' '@xslurmArgs' $scriptFile\n");
} else {
	my $rval;
	if ( @xslurmArgs) {
		$rval = `sbatch @slurmArgs @xslurmArgs $scriptFile 2>&1`;
	} else {
		$rval = `sbatch @slurmArgs $scriptFile 2>&1`;
	}

        my $rc = $?;
        printf("\n$rval\n") if ($rc);

	if (!$silent && !$rc) {
		chomp $rval;
		my ($jobid) = ($rval =~ m/Submitted batch job (\S+)/);
		printf("\n$jobid\n");
	}
}

exit;

#
# Run scontrol and get the license (gres).
#
sub GetGres
{
	my $line = `scontrol show config | grep -i "Licenses.*="`;
	chomp $line;

	$line =~ s/$/,/g;
	$line =~ s/\s+//g;
	$line =~ s/.*=//;
	$line =~ s/\*\d+,/,/g;
	$line =~ s/,$//g;

	return($line);
}

#
# Get user options.
#
sub GetOpts
{
	@ARGV = @_;

	return GetOptions(
		'man'	=> \$man,
		'help'	=> \$help,
		'a=s' 	=> \$starttime,
		'A=s' 	=> \$account,
		'slurm' => \$slurm,
		'l=s' 	=> \@lreslist,		
		'e=s' 	=> \$epath,
		'j=s' 	=> \$join,		
		'm=s' 	=> \$mailoptions,
		'o=s' 	=> \$opath,
		'p=s' 	=> \$priority,
		'q=s' 	=> \$class,
		'N=s' 	=> \$jobname,
		'S=s'   => \$shellpath,		
		'M=s' 	=> \$mailusers,		
		'r:s'	=> \$rerun,
		'd=s' 	=> \$dpath,		# execution directory.
		'W=s' 	=> \@lreslist,
		'v=s'	=> \$vlist,
		'V'	=> \$variable,
		'z'	=> \$silent,
	);

#
# Not supported in SLURM only systems.
#
#		'c=s' 	=> \$nope,		# n/a
#		'C=s' 	=> \$nope,		# n/a
#		'E'	=> \$nope,		# Moab environment variables.
#		'h'	=> \$nope,		# submit job as held.
#		'I'	=> \$nope,		# N/A
#		'k=s' 	=> \$nope,		# N/A
#		'K'	=> \$nope,		# N/A
#		'v=s' 	=> \$nope,		# N/A (but accept it anyway)

	return;
}


sub usage
{
	printf("\n");
	pod2usage(-verbose => 0, -exit => 'NOEXIT', -output => \*STDOUT);
	print "Report problems to LC Hotline.\n\n";
 
	exit(0);
}


sub DateToEpoch
{
	my ($wclim) = @_;

	my ($year, $mon, $mday, $hours, $min);

	my $now = time();
	my $daylen = (60 * 60 * 24);
#
#	Split and reverse the values for easy conversion.
#
	$wclim =~ s/\..*//g;
	$wclim =~ s/(..)/$1:/g;
	my @sv = reverse (split(/:/,$wclim));

	my $i = 0;
	foreach my $tmp (@sv) {
		$min   = $tmp if ($i == 0);
		$hours = $tmp if ($i == 1);
		$mday  = $tmp if ($i == 2);
		$mon   = $tmp if ($i == 3);
		$year  = $tmp if ($i == 4);
		$i++;
	}

	$hours =  (localtime) [2] if (!$hours);
	$mday  =  (localtime) [3] if (!$mday);
	if (!$mon) {
		$mon   =  (localtime) [4] if (!$mon);
	} else {
		$mon--;
	}
	$year  =  (localtime) [5] if (!$year);

	my $time = timelocal( 0, $min, $hours, $mday, $mon, $year);

#
#	If time specified is the past, and the user specified a day of the
#	month, it is an error. If they only supplied an hh:mm, then make it
#	24 hours intto thhe futture...

	if ($time < $now) {
		if ($time < $now &&  ($i > 2)) {
			printf("  Startime error, time is in the past.\n");
			exit(-1);
		} else {
			$time += $daylen;
		}
	}

	return($time);
}


sub fixtime
{
	my ($epoch_time) = @_;
#
#	Needs to be in the slurm format.
#
#	2010-01-20T12:34:00
#
	my ($sec, $min, $hour, $day, $month, $year) = (localtime($epoch_time))[0,1,2,3,4,5,6];

	$year += 1900;
	$month += 1;

	my $date = sprintf("%s-%2.2d-%2.2dT%2.2d:%2.2d:%2.2d",$year,$month,$day,$hour,$min,$sec);

	return($date);
}


#
# Warn about invalid options used.
#
sub invalidopt
{
	my ($opt) = @_;

	print STDERR "\n $opt is an invalid option.\n\n";
	exit(1);
}


#
# Proces the -l/-w extensions.
#
sub process
{
	my (@opts) = @_;

	foreach my $opt (@opts) {
#
#		Convert -W format to -l format.
#		(or at least an attempt to)
#
		if ($opt =~ /x=/) {
			$opt =~ s/x=//;
			$opt =~ s/:/=/;
		}
		my ($ot,$r) = split(/=/, $opt);
		my $o = lc($ot);
		$host	  = $r if ($o =~ /partition/);
		$feature  = $r if ($o =~ /feature/);
		$depend   = $r if ($o =~ /depend/);
		$gres     = $r if ($o =~ /gres/);
		$qos	  = $r if ($o =~ /qos/);
		$nodes	  = $r if ($o =~ /nodes/);
		$dmem	  = $r if ($o =~ /dmem/);
		$resfail  = $r if ($o =~ /resfail/);
		$ttc	  = $r if ($o =~ /ttc/);
		$tpn	  = $r if ($o =~ /tpn/);
		$wclim	  = $r if ($o =~ /walltime/);
		$minwclim = $r if ($o =~ /minwclim/);
		$signal	  = $r if ($o =~ /signal/);
		$ddisk    = $r if ($o =~ /ddisk/);
		$advres   = $r if ($o =~ /advres/);
		$maxmem   = $r if ($o =~ /maxmem/);

		invalidopt("flags")   if ($o =~ /flags/);
#		invalidopt("gres")    if ($o =~ /gres/);
		invalidopt("procs")   if ($o =~ /procs/);
		invalidopt("var")     if ($o =~ /var/);
	}

#
#	See if nodes is in "nn:ppn=pp" format,
#	if so, adjust.
#
	if ($nodes && $nodes =~ /ppn/) {
		my ($o,$r,$s,$t) = split(/=|:/, $nodes);
		$nodes = $r;
		$tpn = $t if ($t);
        }

	return;
}



__END__


=head1 NAME

B<msub> - submit batch jobs.

=head1 SYNOPSIS

       msub [-a datetime][-A account][-d path]
	     [-e path][-h][-j join][-l resourcelist][-m mailoptions]
	     [-M user_list][-N name][-o path][-p priority][-q class][-r y|n]
	     [-S pathlist][-V][-W additionalattributes] [-z][script]

=head1 DESCRIPTION

       msub allows users to submit jobs directly to SLURM.  When a job is submitted directly to a resource manager
       , it is constrained to run on only those nodes that the resource manager is directly monitoring. 

       Submitted jobs can then be viewed and controlled via the mjobctl command.



=head1 OPTIONS

       ==============================
       FLAG    -a

       NAME    Eligible Date

       FORMAT  [[[[CC]YY]MM]DD]hhmm[.SS]

       DEFAULT ---

       DESCRIPTION
	       Declares the time after which the job is eligible for execution

       EXAMPLE ----------------------
	       msub -a 12041300 cmd.pbs
	       ----------------------

	       Moab will not schedule the job until 1:00 pm on December 4, of the current year

       ==============================
       FLAG    -A

       NAME    Account

       FORMAT  <ACCOUNT NAME>

       DEFAULT ---

       DESCRIPTION
	       Defines the account associated with the job

       EXAMPLE ----------------------
	       msub -A research cmd.pbs
	       ----------------------

	       Moab will associate this job with account research

       ==============================
       FLAG    -d

       NAME    Execution Directory

       FORMAT  <path>

       DEFAULT $HOME

       DESCRIPTION
	       Specifies in which directory the job should begin executing

       EXAMPLE ----------------------
	       msub -d /home/test/job12 cmd.pbs
	       ----------------------

	       the job will begin execution in the specified directory

       ==============================
       FLAG    -e

       NAME    Error Path

       FORMAT  [<hostname>:]<path>

       DEFAULT $SUBMISSIONDIR/$JOBNAME.e$JOBID

       DESCRIPTION
	       Defines the path to be used for the standard error stream of the batch job

       EXAMPLE ----------------------
	       msub -e test12/stderr.txt
	       ----------------------

	       the stderr stream of the job will be placed in the relative (to execution) directory specified

       ==============================
       FLAG    -j

       NAME    Join

       FORMAT  [oe|n]

       DEFAULT n (not merged)

       DESCRIPTION
	       Declares if the standard error stream of the job will be merged with the standard output stream of
	       the job. If "oe" is specified, the error and output streams will be merged into the output stream.

       EXAMPLE ----------------------
	       msub -j oe cmd.sh
	       ----------------------

	       the stdout and stderr will be merged into one file

       ==============================
       FLAG    -l

       NAME    Resource List

       FORMAT  <STRING>

	       (either standard PBS/TORQUE options or resource manager extensions)

       DEFAULT ---

       DESCRIPTION
	       Defines the resources that are required by the job and establishes a limit to the amount of resource
	       that can be consumed.  Either resources native to the resource manager (see PBS/TORQUE resources) or
	       scheduler resource manager extensions may be specified. See the Resource Manager Extensions heading
	       below.

	       NOTE: gres not yet supported on SLURM only systems.

       EXAMPLES
	       ----------------------
	       msub -l partition=zeus|atlas,nodes=32,walltime=12:00:00,qos=expedite,gres=ignore cmd.sh
	       ----------------------

	       In this example which highlights a linux parallel cluster, the job submits to either the zeus or the
	       atlas machine, requires 32 nodes, a walltime of 12 hours, an expedite QoS, and ignores any
	       filesystem requirement. NOTE: The expedite QoS must be available.

	       ----------------------
	       msub -l partition=yana,ttc=4,feature=16GB,depend=jobID,walltime=600,qos=standby cmd.sh
	       ----------------------

	       The ttc option is used on processor scheduled machines like yana and hopi. In this example the job
	       requires four total tasks on machine yana, using one node with 16GB, a walltime of 600 seconds,
	       after the job dependency is complete. NOTE: The dependency jobID must exist.

	       ----------------------
	       msub -l partition=up,nodes=32:ppn=8,walltime=1:00:00,gres=lscratcha cmd.sh
	       ----------------------

	       In this example highlighting use on an AIX cluster, the job is for the up machine using eight
	       processors in each of the 32 nodes, a wall time of one hour and lscratcha as the filesystem
	       requirement.

	       ----------------------
	       msub -l partition=ubgl,nodes=8k,gres=lscratcha,gres=lscratchc,walltime=2:06:00:00 cmd.sh
	       ----------------------

	       Submit the job script to the ubgl machine using 8k nodes, the filesystem requirements of lscratcha
	       and lscratchc, and with a wall time of two days and six hours.

       ==============================
       FLAG    -m

       NAME    Mail Options

       FORMAT  [[n]|[a][b][e]]

       DEFAULT a

       DESCRIPTION
	       Defines the set of conditions (abort,begin,end) when the server will send a mail message about the
	       job to the user

       EXAMPLE ----------------------
	       msub -m be cmd.sh
	       ----------------------

	       mail notifications will be sent when the job begins and ends

       ==============================
       FLAG    -M

       NAME    Mail List

       FORMAT  <user>[@<host>][,<user>[@<host>],...]

       DEFAULT $JOBOWNER

       DESCRIPTION
	       Specifies the list of users to whom mail is sent by the execution server

       EXAMPLE ----------------------
	       msub -M jon@node01,bill@node01,jill@node02 cmd.sh
	       ----------------------

	       mail will be sent to the specified users if the job is aborted

       ==============================
       FLAG    -N

       NAME    Name

       FORMAT  <STRING>

       DEFAULT STDIN or name of job script

       DESCRIPTION
	       Specifies the user-specified job name attribute

       EXAMPLE ----------------------
	       msub -Nchemjob3 cmd.sh
	       ----------------------

	       (job will be associated with the name chemjob3)

       ==============================
       FLAG    -o

       NAME    Output Path

       FORMAT  [<hostname>:]<path>

       DEFAULT $SUBMISSIONDIR/$JOBNAME.o$JOBID

       DESCRIPTION
	       Defines the path to be used for the standard output stream of the batch job

       EXAMPLE ----------------------
	       msub -o test12/stdout.txt
	       ----------------------

	       the stdout stream of the job will be placed in the relative (to execution) directory specified

       ==============================
       FLAG    -p

       NAME    Priority

       FORMAT  <INTEGER> (between -10000 and 10000)

       DESCRIPTION
               Defines the priority of the job
	       Run the job with an adjusted scheduling priority within SLURM.  The adjustment range is from
	       -10000 (highest priority)  to  10000  (lowest priority). Only privileged users can specify a
	       negative adjustment.

       EXAMPLE ----------------------
               msub -p 25 cmd.sh
               ----------------------

               the job will have a user priority of 25

       ==============================
       FLAG    -q

       NAME    Destination Queue (Class)

       FORMAT  [<queue>][@<server>]

       DEFAULT [<DEFAULT>]

       DESCRIPTION
	       Defines the destination queue of the job

       EXAMPLE ----------------------
	       msub -q pbatch cmd.sh
	       ----------------------

	       the job will be submitted to the pbatch queue

       ==============================
       FLAG    -r

       NAME    Rerunable

       FORMAT  [y|n]

       DEFAULT y

       DESCRIPTION
	       Declares whether the job is rerunable

       EXAMPLE ----------------------
	       msub -r n cmd.sh
	       ----------------------

	       the job cannot be rerun

       ==============================
       FLAG    -S

       NAME    Shell Path

       FORMAT  <path>[@<host>][,<path>[@<host>],...]

       DEFAULT $SHELL

       DESCRIPTION
	       Declares the shell that interprets the job script

       EXAMPLE ----------------------
	       msub -S /bin/bash
	       ----------------------

	       the job script will be interpreted by the /bin/bash shell

       ==============================
       FLAG    -V

       NAME    All Variables

       FORMAT
       DEFAULT ---

       DESCRIPTION
	       Declares that all environment variables in the msub environment are exported to the batch job
	       (SLURM only systems will always pass all environment variables.)

       EXAMPLE ----------------------
	       msub -V cmd.sh
	       ----------------------

       ==============================
       FLAG    -W

       NAME    Additional Attributes

       FORMAT  <STRING>

       DEFAULT ---

       DESCRIPTION
	       Allows the for the specification of additional job attributes (See Resource Manager Extension)

       EXAMPLE ----------------------
	       msub -W x=GRES:matlab:1 cmd.sh
	       ----------------------

	       the job requires one resource of "matlab"

       ==============================
       FLAG    -z

       NAME    Silent Mode

       FORMAT
       DEFAULT ---

       DESCRIPTION
	       The job's identifier will not be printed to stdout upon submission

       EXAMPLE ----------------------
	       msub -z cmd.sh
	       ----------------------

	       no job identifier will be printout the stdout upon successful submission

       Resource Manager Extensions

       The -l flag supports scheduler resource manager extensions, specifically, DEPEND.

       ==============================
       FLAG
	   depend=

       NAME
	   DEPEND

       FORMAT
	   [<DEPENDTYPE>:][{jobname|jobid}.]<ID>[:[{jobname|jobid}.]<ID>]...

       DEFAULT
	   ---

       DESCRIPTION
	   Allows specification of job dependencies. It is recommended to use the jobid because the use of jobnames
	   may result in unintended outcome if other jobs have the same name.

       EXAMPLE
	   ----------------------
	   msub -l depend=1301:1304 test.cmd
	   ----------------------

	   # submit job which will run after job 1301 and 1304 complete

	   The DEPEND types are:

	   after   Format: after:<jobid>[:<jobid>]...

		   Description: Job may start at any time after specified jobs have started execution.

	   afterany
		   Format:   afterany:<jobid>[:<jobid>]...

		   Description: Job may start at any time after all specified jobs have completed regardless of
		   completion status.

	   afterok Format: afterok:<jobid>[:<jobid>]...

		   Description: Job may be start at any time after all specified jobs have successfully completed.

	   afternotok
		   Format: afternotok:<jobid>[:<jobid>]...

		   Description: Job may start at any time after any specified jobs have completed unsuccessfully.


=head1 REPORTING BUGS

Report problems to LC Hotline.

=cut


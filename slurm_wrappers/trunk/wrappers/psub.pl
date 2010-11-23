#! /usr/bin/perl -w
#
# psub - submit slurm jobs in familar lcrm format.
#
# Modified:	07/14/2010
# By:		Phil Eckert
#

#
# For debugging.
#
my $debug = 0;

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

use strict;
use Getopt::Long 2.24 qw(:config no_ignore_case);
use Time::Local;
use autouse 'Pod::Usage' => qw(pod2usage);
use Cwd;

our $SAVEDPATH;

my @slurmArgs;

#
# Save off ARGV so we can override the script directives later
#
my @SAVEDARGV = @ARGV;

my (
	$startTime, 	  $bankName,         $bglAttributes,
	$creds,           $cpuLimit,         $command,
	$coreLimit,       $dataLimit,        $dependencyJobId,
	$dm,              $errorFile,        $exempt,
	$expedite,        $featureList,      $fileSizeLimit,
	$fileSpaceLimit,  $flushError,       $flushOutput,
	$geometry,        $help,             $jobName,
	$keepError,       $keepOutput,       $mailJobStart,
	$mailJobEnd,      $mailNever,        $mailUser,
	$man,             $mergeError,       $memoryLimit,
	$networkProtocol, $networkType,      $noBulkXfer,
	$noKill,          $nodeDistribution, $nonCheckpointable,
	$nonRestartable,  $openFilesLimit,   $outputFile,
	$poolList,        $priority,   	     $procsPerNode,
	$projectName,     $requiredMemory,   $scriptFile,
	$scriptArgs,      $shellPath,        $signal,
	$stackLimit,      $standby,          $taskCpuLimit,
	$tmpScriptFile,   $wallclockLimit,   $verbose,
	$export,	  $adapter,          $geometry_options,
	$wcKey
);

#
# Slurm Version.
#
chomp(my $soutput = `sinfo --version`);
my ($sversion) = ($soutput =~ m/slurm (\d+\.\d+)/);

#
# Check usage and extract job script file and its args
#
$Getopt::Long::order = $REQUIRE_ORDER;
getopt(@ARGV) or pod2usage(2);

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
# Handle unsupported options
#
NoOptionSupport("-creds")  if ($creds);
NoOptionSupport("-dm")     if ($dm);
NoOptionSupport("-ke")     if ($keepError);
NoOptionSupport("-ko")     if ($keepOutput);
NoOptionSupport("-nc")     if ($nonCheckpointable);
NoOptionSupport("-net")    if ($networkProtocol);
NoOptionSupport("-nr")     if ($nonRestartable);
NoOptionSupport("-re")     if ($flushError);
NoOptionSupport("-ro")     if ($flushOutput);

#
# Determine os.
#
chomp(my $os = `uname -s`);

#
# If -i is specified, read the job command file from the argument
#
if ($command) {
	 if (@ARGV) {
		$scriptFile = shift;
		die(
			"You specified a script file named $scriptFile, but also specified the -i option\n"
		);
	 }

	$tmpScriptFile = "/tmp/jobScript.lcrm.$$";
	open SCRIPTOUT, "> $tmpScriptFile"
		or die(
			"Unable to open temporary job command file ($tmpScriptFile) for writing: $!\n"
		);
	print SCRIPTOUT "#! /bin/sh\n";
	print SCRIPTOUT "$command\n";
}

#
# If there are remaining arguments, use the first as the job command file
# and the remaining arguments as script arguments
#
elsif (@ARGV) {
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
		print SCRIPTOUT $line if ($tmpScriptFile);
		if ($lineNumber == 1 && $line =~ /^\s*#\s*!\s*(\S+)/) {
			$assignedShell = $1;
		} elsif ($line =~ s/^\s*#\s*PSUB\s+//) {
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
getopt(@scriptDirectives)
	or die("Invalid PSUB options found in job command file.\n");

#
# Parse command line arguments (overriding script directives)
#
getopt(@SAVEDARGV);

#
# Parse the arguments
#
my %environment   = ();
my ($outputFileName, @outputFileRedirects, $errorFileName, @errorFileRedirects);

#
# Some setup for use the Extension element. This allows appending multiple
# extesnionElement uses, which is now needed.
#

if ($startTime) {
	my $epoch_time = `LCRM_date2epoch "$startTime"`;
	chomp $epoch_time;
	if ($epoch_time == 0) {
		die("Invalid time specification ($startTime).\n");
	}
	if ($sversion >= 2.2) {
		push @slurmArgs, "--begin=uts$epoch_time";
	} else {
		my $newtime = fixtime($epoch_time);
		push @slurmArgs, "--begin=$newtime";
	}
}

if ($bankName) {
	push @slurmArgs, "--account=$bankName";
}


if ($featureList) {
	push @slurmArgs, "--constraint=$featureList";
}

if ($procsPerNode) {
	push @slurmArgs, "--mincpus=$procsPerNode";
}

if ($dependencyJobId) {
	push @slurmArgs, "--dependency=$dependencyJobId";
}

if ($standby) {
	push @slurmArgs, "--qos=standby";
}

if ($exempt) {
	push @slurmArgs, "--qos=exempt";
}

if ($expedite) {
	push @slurmArgs, "--qos=expedite";
}

if ($coreLimit) {
# don't know yet.
}

if ($dataLimit) {
	NoOptionSupport("-ld <limit>");
}

if ($fileSpaceLimit) {
	NoOptionSupport("-lF <limit>");
}

if ($fileSizeLimit) {
	NoOptionSupport("-lf <limit>");
}

if ($requiredMemory) {
	push @slurmArgs, "--mem==$requiredMemory";
}

if (defined $nodeDistribution) {
	die(
           "Currently, the only supported format for this option is -ln <node_count> or -ln <min_node_count>-<max_node_count>.\n"
	) unless $nodeDistribution =~ /^(\d+)(?:-(\d+))?$/;

	my ($min, $max) = ($1, $2);
	if (defined $min && defined $max) {
		push @slurmArgs, "--nodes=$min-$max";
	} elsif (defined $min) {
		die("Invalid node count ($min).\n") if ($min <= 0);
		push @slurmArgs, "--nodes=$min";
	}
}

if ($geometry) {

	my $node_count = 1;
	my ($first, $second, $tasks, $tpn, $dist);

	if (defined $nodeDistribution) {
		$node_count = $nodeDistribution;
	}

	if ($geometry =~ /@/) {
		my @field = split(/\@/,$geometry);

		$first  = $field[0];
		$second = $field[1];

		($tpn)  = $second =~ m/tpn([0-9]+)/;
		($dist) = $second =~ m/([a-zA-Z]+)/;
	} else {
		$first = $geometry;
	}

	($tasks) = $first  =~ m/([0-9]+)/;
	($adapter) = $first  =~ m/([a-zA-Z]+)/;

#
#	Build the srun extension.
#
	if (defined $tpn) {
		my $total_tasks = $tpn * $node_count;
		if (defined $tasks) {
			if ($tasks != $total_tasks) {
				printf("\n  Invalid task count.\n\n");
				exit(1);
			}
		}
		$geometry_options .= "--ntasks=$total_tasks";
	} elsif (defined $tasks) {
		if ($tasks < $node_count) {
			printf("\n  Invalid task count.\n\n");
			exit(1);
		}
		$geometry_options .= "--ntasks=$tasks";
	} else {
			$geometry_options .= "--ntasks=$node_count";
	}

	if (defined $dist && $dist eq "dist") {
		$geometry_options .= " --distribution=cyclic";
	}
	push @slurmArgs, $geometry_options;
}

if ($openFilesLimit) {
	NoOptionSupport("-lo <limit>");
}

if ($memoryLimit) {
	NoOptionSupport("-lr <limit>");
}

if ($stackLimit) {
	NoOptionSupport("-ls <limit>");
}

if ($cpuLimit) {
	NoOptionSupport("-lt <limit>");
}

if ($mailJobStart) {
	push @slurmArgs, "--mail-type=BEGIN";
}

if ($mailJobEnd) {
	push @slurmArgs, "--mail-type=END";
}

if ($mailNever) {
	NoOptionSupport("-mn");
}

if ($noKill) {
	push @slurmArgs, "--no-kill";
}

if ($priority) {
	NoOptionSupport("-p");
}

if ($poolList) {
	push @slurmArgs, "--partition=$poolList";
}

#
# Had to modify to use unnamed an extensions 
#
if ($projectName) {
	die("The project name ($projectName) may not contain white space.\n") if ($projectName =~ /\s/);
	push @slurmArgs, "--comment=$projectName";
}

my $assignedJobName;
if ($jobName) {
	$assignedJobName = $jobName;
} elsif ($scriptFile) {
	my $baseName = $scriptFile;
	$baseName =~ s/.*\///;
	$assignedJobName = $baseName;
} else {
	my $userId = (getpwuid($<))[0];
	$assignedJobName = $userId;
}
push @slurmArgs, "--job-name=$assignedJobName";

#
# See if we need an output file name.
#
$outputFileName = $assignedJobName . ".o%j";
if ($outputFile) {
	die("Output file ($outputFile) may not contain a colon.\n") if ($outputFile =~ /:/);
	$outputFileName = $outputFile;
} else {
	$outputFile = $outputFileName;
}

if ($mergeError) {
	die("The -eo option may not be used with the -e option.\n") if ($errorFile || $flushError);
	push @outputFileRedirects, "Merge";
} else {
	$errorFileName = $assignedJobName . ".e%j";
	if ($errorFile) {
		die("Error file ($errorFile) may not contain a colon.\n") if ($errorFile =~ /:/);
		$errorFileName = $errorFile;
	} else {
		$errorFile = $errorFileName;
	}
}

if ($signal) {
	push @slurmArgs, "--signal=$signal";
}

if ($shellPath) {
	NoOptionSupport("-s");
}


if ($wallclockLimit || $taskCpuLimit) {
	my $lim = seconds($wallclockLimit ? $wallclockLimit : $taskCpuLimit);
	$lim = int ($lim / 60);
	push @slurmArgs, "--time=$lim";
}

if ($wcKey) {
	die("The wckey name ($wcKey) may not contain white space.\n") if ($wcKey =~ /\s/);
	push @slurmArgs, "--wckey=$wcKey";
}

if ($export) {
	push @slurmArgs, "--get-user-env";
}

#
# Add outputFile and errorFileElements
#
if ($outputFileName) {
	push @slurmArgs, "--output=$outputFileName";
}

if ($errorFileName) {
	push @slurmArgs, "--error=$errorFileName";
}


#
# Set the psub environment variables
#
chomp(my $hostname = `hostname`);

$environment{ENVIRONMENT}  = "BATCH";
$environment{PSUB_HOME}    = $ENV{HOME} if ($ENV{HOME});
$environment{PSUB_HOST}    = $hostname;
$environment{PSUB_LOGNAME} = $ENV{LOGNAME} if ($ENV{LOGNAME});
$environment{PSUB_PATH}    = $SAVEDPATH if ($SAVEDPATH);
$environment{PSUB_REQNAME} = $assignedJobName;
unless ($environment{PSUB_SUBDIR} = getcwd) {
        die " Can't get current working directory:$!\n";
}
$environment{PSUB_TZ}      = $ENV{TZ} if ($ENV{TZ});
$environment{PSUB_USER}    = $ENV{USER} if ($ENV{USER});
$assignedShell = $ENV{SHELL} if ($ENV{SHELL} && ! $assignedShell);
$environment{PSUB_SHELL}   = $assignedShell if ($assignedShell);

#
# Add command file
#
if ($scriptFile || $tmpScriptFile) {
	if ($scriptFile) {
		$command = $scriptFile;
	} else {
		$command = $tmpScriptFile;
	}

#
#	Add arguments
#
	if ($scriptArgs) {
		$environment{SESSARGS} = "$scriptArgs";
	}
}

#
# Add the environment variables
#
if (%environment) {
#
#	Build the job script environment file
#	Add it to the Environment element
#
	my @environmentVariables = ();
	foreach my $variable (sort keys %environment) {
		if (grep /;/, $environment{$variable}) {
			push @environmentVariables, "$variable=\"$environment{$variable}\"";
		} else {
			push @environmentVariables, "$variable=$environment{$variable}";
		}
	}
}

#
# Only process the following if os is AIX, this is
# POE related options and will only apply on an AIX
# platfrom. Note: this is not the best solution, but
# it is reasonable.
#
if ($os eq "AIX") {

#
#	Last minute things for passing info using --slurm option.
#
	my $nstring = "--network=";

	if (defined $adapter) {
		$nstring .= "$adapter,";
	} else {
		$nstring .= "us,";
	}
	

	if (!defined $noBulkXfer) {
		$nstring .= "bulk_xfer,";
	}
	

	if (defined $networkProtocol) { 
		$nstring .= "$networkProtocol,";
	}
	

	if (defined $networkType) {      
		$nstring .= "$networkType,";
	} else {
		$nstring .= "sn_all,";
	}
	

	if (defined $nstring) {      
		$nstring =~ s/,$//;
	}
	

	if (defined $geometry_options) {
		$nstring .= " $geometry_options ";
	}
	

	if (defined $nstring) {      
		push @slurmArgs, $nstring;
	}
}


#
# Build the command
#
printf("@slurmArgs  $command\n") if ($debug);

#
# Invoke the command
#
my $output = `sbatch  @slurmArgs $command 2>&1`;
my $rc     = $?;
die("Unable to submit moab job (sbatch @slurmArgs $command) [rc=$rc]: $output\n") if ($rc);

#
# Remove temporary job script
#
unlink $tmpScriptFile if ($tmpScriptFile);

#
# Parse the response
#
if (! $rc && $output =~ /Submitted batch job (\S+)/m) {
	my $msg = "Job $1 submitted to batch\n";
	print $msg;
} else {
	die("No job was submitted: $output\n");
}

#
# Exit with status code
#
exit(0);


#
# $result = getopt(@ARGV)
# Run getopt for a list of arguments
#
sub getopt
{
	@ARGV = @_;

	return GetOptions(
		'A=s'        => \$startTime,
		'b=s'        => \$bankName,
		'bgl=s'      => \$bglAttributes,
		'c=s'        => \$featureList,
		'np|cpn=i'   => \$procsPerNode,
		'creds'      => \$creds,
		'd=s'        => \$dependencyJobId,
		'dm'         => \$dm,
		'e=s'        => \$errorFile,
		'eo'         => \$mergeError,
		'exempt'     => \$exempt,
		'expedite'   => \$expedite,
		'g=s'        => \$geometry,
		'help|?|H'   => \$help,
		'i=s'        => \$command,
		'ke'         => \$keepError,
		'ko'         => \$keepOutput,
		'lc=s'       => \$coreLimit,
		'ld=s'       => \$dataLimit,
		'lF=s'       => \$fileSpaceLimit,
		'lf=s'       => \$fileSizeLimit,
		'lM=s'       => \$requiredMemory,
		'ln=s'       => \$nodeDistribution,
		'lo=s'       => \$openFilesLimit,
		'lr=s'       => \$memoryLimit,
		'ls=s'       => \$stackLimit,
		'lt=s'       => \$cpuLimit,
		'man'        => \$man,
		'mb'         => \$mailJobStart,
		'me'         => \$mailJobEnd,
		'mn'         => \$mailNever,
		'mu=s'       => \$mailUser,
		'nc'         => \$nonCheckpointable,
		'net=s'      => \$networkProtocol,
		'nettype=s'  => \$networkType,
		'nobulkxfer' => \$noBulkXfer,
		'nokill'     => \$noKill,
		'nr'         => \$nonRestartable,
		'o=s'        => \$outputFile,
		'p=s'        => \$priority,
		'pool=s'     => \$poolList,
		'prj=s'      => \$projectName,
		'r=s'        => \$jobName,
		're'         => \$flushError,
		'ro'         => \$flushOutput,
		's=s'        => \$shellPath,
		'S=s'        => \$signal,,
		'standby'    => \$standby,
		'tM=s'       => \$taskCpuLimit,
		'tW=s'       => \$wallclockLimit,
		'v'          => \$verbose,
		'wckey=s'    => \$wcKey,
		'x'          => \$export,
	);

	return;
}


#
# Take a epoch time and convert it into SLURM format.
# (Not needed in Slurm 2.2 or later, it takes a uts
#  time value.)
#
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
# Converts a duration in nnnn[dhms] or [[dd:]hh:]mm to seconds
#
sub seconds
{
	my ($duration) = @_;
	$duration = 0 unless $duration;
	my $seconds = 0;

#
#	Convert [[dd:]hh:]mm to duration in seconds
#
	if ($duration =~ /^(?:(\d+):)?(\d*):(\d+)$/) {
		my ($dd, $hh, $mm) = ($1 || 0, $2 || 0, $3);
		$seconds += $mm * 60;
		$seconds += $hh * 60 * 60;
		$seconds += $dd * 24 * 60 * 60;
	}

#
#	Convert nnnn[dhms] to duration in seconds
#
	elsif ($duration =~ /^(\d+)([dhms])$/) {
		my ($number, $metric) = ($1, $2);
		if    ($metric eq 's') { $seconds = $number; }
		elsif ($metric eq 'm') { $seconds = $number * 60; }
		elsif ($metric eq 'h') { $seconds = $number * 60 * 60; }
		elsif ($metric eq 'd') { $seconds = $number * 24 * 60 * 60; }
	}

#
#	Convert number in minutes to seconds
#
	elsif ($duration =~ /^(\d+)$/) {
		$seconds = $duration * 60;
	}

#
#	Unsupported format
#
	else {
		Die("Invalid time limit specified ($duration)\n");
	}

#
#	Time must be at least 1 minute (60 seconds)
#
	$seconds = 60 if $seconds < 60;

	return $seconds;
}


#
# NoOptionSupport($option, $warnOnly, $verbose)
# Fail with an error for unsupported options
#
sub NoOptionSupport
{
	my ($option) = @_;

	my $message =
		"This option ($option) is not currently supported.
		Type '$0 --help' for available options.
		Report problems to LC Hotline.\n";

	printf("Warning: $message\n");

	return;
}


# Target SYNOPSIS B<psub> [B<-A> I<date_time>] [B<-b> I<bank_name>] [B<-bgl> I<attributes>] [B<-c> I<constraints>] [B<-d> I<job_id>] [B<-e> I<file_name>] [B<-eo>] [B<-exempt>] [B<-expedite>] [B<-g> [I<tasks>][I<switch>][I<@layout>]] [B<-lc> I<limit>] [B<-ld> I<limit>] [B<-lF> I<limit>] [B<-lf> I<limit>] [B<-lM> I<jsize>] [B<-ln> I<node_count_range>] [B<-lo> I<limit>] [B<-lr> I<limit>] [B<-ls> I<limit>] [B<-lt> I<limit>] [B<-mb>] [B<-me>] [B<-mn>] [B<-nettype> I<network_type>] [B<-nobulkxfer>] [B<-nokill>] [B<-np> I<cpus_per_node>] [B<-o> I<file_name>] [B<-p> I<priority>] [B<-pool> I<pool_name>] [B<-prj> I<project_name>] [B<-r> I<job_name>] [B<-s> I<shell_name>] [B<-standby>] [B<-tM> I<time>] [B<-tW> I<time>] [B<-v>] [B<-wckey> I<wckey_name>] [B<-x>] [B<-H, -?, --help>] [B<--man>] [B<-i> <command> | {F<job_command_file> [I<session_args>]}]


##############################################################################

__END__

=head1 NAME

B<psub> - submits jobs in a familiar lcrm format

=head1 SYNOPSIS

B<psub> [B<-A> I<date_time>] [B<-b> I<bank_name>] [B<-c> I<constraints>] [B<-d> I<job_id>] [B<-e> I<file_name>] [B<-eo>] [B<-exempt>] [B<-expedite>] [B<-ln> I<node_count_range>] [B<-mb>] [B<-me>] [B<-mn>] [B<-nettype> I<network_type>] [B<-nokill>] [B<-np> I<cpus_per_node>] [B<-o> I<file_name>] [B<-pool> I<pool_name>] [B<-prj> I<project_name>] [B<-r> I<job_name>] [B<-S> I<signal>[@I<remaining_time>]] [B<-standby>] [B<-tW> I<time>] [B<-v>] [B<-wckey> I<wckey_name>] [B<-x>] [B<-H, -?, --help>] [B<--man>] [B<-i> <command> | {F<job_command_file> [I<session_args>]}]

=head1 DESCRIPTION

The B<psub> command is used to submit a job in lcrm format. The job id will be displayed on successful submission of the job.

Options (job directives) can be embedded within the job command file prior to the first executable command of the shell script by preceding them with the string "# PSUB". If the same option appears in both the job command file and the command line arguments, the command-line option takes precedence.

The job script including job directives will be read from STDIN if the I<job_command_file> is not specified on the command line.

An example of using job directives within the job command file is as follows:

   #
   #  Batch job command file example:
   #
   #  PSUB -b sci          # Set bank to sci
   #  PSUB -tW 4:00:00     # Send wallclock limit to 4 hours
   #  PSUB                 # No more embedded options.
   #
   nmake all

=head1 OPTIONS

=over 4

=item B<-A> I<date_time>

Specifies that the job is not to start until after the specified date and/or time. I<date_time> format
is [DD-MM-[CC]YY] [hh:mm[:ss]] ie. -A "23-01-2008 23:00:00".

=item B<-b> I<bank_name>

Specifies the bank name for the job.

=item B<-bgl> I<attributes>

This option is currently not implemented. Specifies BGL-specific attributes. This option is ignored on non-BGL machines.  It specifies the geometry and other characteristics required  for a job that is to run on the IBM BGL machine. The attributes must take the form: 
 
attributes := [size] [rigidity] [conntype]

where

=over 4

=item size := geometry=N[xM[xO]]

=item rigidity := [no]rotate

=item conntype := conn_type=mesh | torus

=back

The attributes may be in any order. None are required. The default values for M and O are 1. If the size attribute is not used, the default value for N is the value used in the -ln option. If the -ln option is also not used, the default size is 1x1x1. The default rigidity is to permit rotation. The default connection type is mesh.

=item B<-c> I<constraints>

Specifies the node features required for hosts allocated to the job.

=item B<-cpn> I<procs_per_node>

Specifies the number of processors per node for the job. Same as B<-np>. This option is currently not implemented.

=item B<-d> I<job_id>

Specifies a dependency job. The specified job must complete successfully before this job will start.

=item B<-e> I<error_file>

Specifies the name of file to which standard error will be directed.

=item B<-eo>

Merge stderr with the stdout file.  This option may not be used in conjunction with the B<-e> I<error_file> option.

=item B<-exempt>

Makes the job exempt from specified limits.

=over 4

=item NODE>MAX

job requires more nodes than is permitted

=item CPU&TIME

job requires more node time than is permitted

=item JRESLIM

maximum allowable jobs for the user or bank are already running

=item NRESLIM

maximum allowable nodes for the user or bank are already in use

=item NTRESLIM

maximum node-time limit for the user or bank has been reached

=item QTOTLIM

host is running as many jobs as is permitted

=item QTOTLIMU

host is running as many jobs for user as is permitted

=item TOOLONG

job requires more time than is permitted

=item WMEML

host load within target parameters

=back

=item B<-expedite>

Specifies the expedite quality of service for the job. All expedited jobs are exempt from administratively imposed limits and are priority scheduled ahead of jobs in either the normal or the standby class. This option may not be used in conjunction with the -standby option. If used, then use of the -exempt option adds no advantage for the job.

=item B<-g> [I<tasks>][I<switch>][I<@layout>]
 
This option is currently not implemented. Used in conjunction with the B<-ln> option on IBM SP (AIX) hosts (SLURM or LoadLeveler) to specify how tasks will be assigned to nodes. The tasks value is the total number of parallel tasks that will be started for the job. If a variable number of nodes is specified for the job via the -ln option, then the tasks value must not be specified. The switch value is the communications switch type and may be either "ip" or "us". layout may be either "tpn<number>" where <number> is the number of tasks per node or "dist" which specifies that tasks should be distributed as evenly as possible across nodes. The default switch is "us" and the default layout is "dist". If tasks is not specified, then if the layout is "dist", the number of tasks is equal to the number of nodes. But if the layout is "tpn<number>", then the number of tasks is the multiple of the number of nodes (specified with the -ln option) and the <number> of tasks per node specified. If both tasks is specified and the layout is specified as "tpn<number>", then the number of tasks per node multiplied by the number of nodes requested must equal the number of tasks specified.

=item B<-i> I<command>

Specifies an executable directly for the job to run (as opposed to running a job command file as a script or reading the script from stdin). When typing an immediate command, take care to isolate shell interpreted characters by quoting the command.

=item B<-lc> I<limit>

Specifies a per-process maximum core file size limit for the job. See the LIMITS subsection for more information. This option is currently not implemented.

=item B<-ld> I<limit>

Specifies a per-process maximum data-segment size limit for the job. See the LIMITS subsection for more information. This option is currently not implemented.

=item B<-lF> I<limit>

Specifies the maximum disk space limit for the job. See the LIMITS subsection for more information. This option is currently not implemented.

=item B<-lf> I<limit>

Specifies a per-process maximum file size limit for the job. See the LIMITS subsection for more information. This option is currently not implemented.

=item B<-lM> I<memory_size>

Specifies the minimum required memory for the job. User must specify units (ie. kb,mb,gb).

=item B<-ln> I<node_count>

Specifies the number of nodes required for the job.

=item B<-lc> I<limit>

Specifies a per-process maximum core file size limit for the job. See the LIMITS subsection for more information. This option is currently not implemented.

=item B<-lo> I<limit>

Specifies a per-process open file count limit for the job. See the LIMITS subsection for more information. This option is currently not implemented.

=item B<-ls> I<limit>

Specifies a per-process maximum stack size limit for the job. See the LIMITS subsection for more information. This option is currently not implemented.

=item B<-lt> I<limit>

Specifies a per-process cpu time limit for the job. See the LIMITS subsection for more information. This option is currently not implemented.

=item B<-mb>

Requests mail to be sent to the user when the job starts.

=item B<-me>

Requests mail to be sent to the user when the job ends.

=item B<-mn>

Requests that no mail be sent to the user for job scheduling events.

=item B<-nettype> I<network_type>

Specifies the network communication type that is to be used for this job. (Ignored on all but IBM SP hosts.) This option is currently not implemented.

=item B<-nobulkxfer>

Specifies that block transfer is to be turned off, the default setting is on. (Ignored on all but IBM SP hosts.) This option is currently not implemented.

=item B<-nokill>

Do not automatically terminate a job if one of the nodes it has been allocated fails.

=item B<-np> I<procs_per_node>

Specifies the number of processors per node for the job. Same as B<-cpn>. This option is currently not implemented.

=item B<-o> I<output_file>

Specifies the name of file to which standard output will be directed.

=item B<-p> I<priority>

Specifies the user priority to assign to the job.

=item B<-pool> I<pool_name>

Specifies the pool (class) for the job to run in.

=item B<-prj> I<project_name>

Specifies the project name (group name).

=item B<-r> I<job_name>

Specifies the job name.

=item B<-S> I<signal>[@I<remaining_time>]

Requests a signal to be sent to the job with optionally specified remaining time in seconds.

=item B<-s> I<shell_name>

Specifies the absolute path name of the shell used to interpret the batch job shell script. This option is currently not implemented.

=item B<-standby>

Specifies the standby quality of service for the job. This job will be exempt from the following administratively imposed limits.

=over 4

=item JRESLIM

maximum allowable jobs for the user or bank are already running

=item NRESLIM

maximum allowable nodes for the user or bank are already in use

=item NTRESLIM

maximum node-time limit for the user or bank has been reached

=back

However, the job will be removed (or otherwise stopped) when a non-standby job is eligible to run on the host and is in need of the resources being used by the standby job. If the job is registered to receive a signal, it will be signaled prior to removal within the configured grace time limit. The B<-standby> option may not be used in conjunction with either the B<-expedite> or the B<-exempt> options.

=item B<-tM> I<time>

Specifies the maximum cpu time per task. Since slurm does not currently support the specification of maximum cpu time, if it is specified and the wallclock limit is not specified, its value will be used as the wallclock limit as if it were specified with the B<-tW> option, otherwise it will be ignored.

=item B<-tW> I<time>

Specifies the wallclock limit for the job.

=item B<-v>

Warnings will be displayed for unimplemented options (verbose mode).

=item B<-wckey> I<wckey_name>

Specified the wckey for accounting purposes.

=item B<-x>

Exports all current environment variables to the job environment.

=item B<-H, -?, --help>

Display a brief help message

=item B<--man>

Display full documentation

=back

=head1 EXAMPLE

psub -b sci -tW 4:00:00 myjob.cmd

=head1 REPORTING BUGS

Report problems to LC Hotline.

=cut


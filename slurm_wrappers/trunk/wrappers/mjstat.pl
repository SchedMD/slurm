#!/usr/bin/perl 

#
# A reasonably simple spjstat emulator for use 
# with moab/slurm systems.
#
# This is an extended verison.
#
# Author: Phil Eckert
# Date:	  March 14, 2007	
# update: July 14, 2010
#

#
# Man page stuff.
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

#
#	Global Variables.
#
	my ($help, $man, $pool, $running, $verbose);
	my (%MaxNodes, %MaxTime);

#
#	Get user options.
#
	get_options();

#
#	Get partition information from scontrol, used
#	currently in conjunction with the sinfo data..
#
	do_scontrol_part();

#
#	Get and display the sinfo data.
#
	do_sinfo();

#
#	If the -c option was entered, stop here.
#
	exit if ($pool);

#
#	Get and display the squeue data.
#	
	do_squeue();

	exit;


#
# Get the SLURM partitions information.
#
sub do_sinfo
{
	my @SINFO;
#
#	Get the partition and node info.
#
	my $options = "\"%9P %6m %.4c %.22F %f\"";

	my $ct = 0;
	my @sin = `sinfo -h -e -o $options`;
	foreach my $tmp (@sin) {
		chomp $tmp;
		my ($part,$mem,$cpu,$fields,$feat) = split(' ',$tmp);
		$SINFO[$ct]->{part}   = $part;
		$SINFO[$ct]->{memory} = $mem;
		$SINFO[$ct]->{cpu}    = $cpu;
#
#		Split the status into various components.
#
		my ($active, $idle, $out, $total) = split(/\//, $fields);
		$SINFO[$ct]->{active} = $active;
		$SINFO[$ct]->{idle}   = $idle;
		$SINFO[$ct]->{out}    = $out;
		$SINFO[$ct]->{total}  = $total;
		$SINFO[$ct]->{usable} = $total - $out;
#			Handle "k" factor for Blue Gene.
#
#		$SINFO[$ct]->{usable} .= 'K' if ($SINFO[$ct]->{total} =~ /K/);
		$feat .= " ";
		$feat =~ s/\(null\)//g;
		$ct++;
	}

	printf("\nScheduling pool data:\n");
	if ($verbose) {
		printf("-----------------------------------------------------------------------------------\n");
		printf("                           Total  Usable   Free   Node        Time  Other          \n");
		printf("Pool         Memory  Cpus  Nodes   Nodes  Nodes  Limit       Limit  traits         \n");
		printf("-----------------------------------------------------------------------------------\n");
	} else {
		printf("-------------------------------------------------------------\n");
		printf("Pool        Memory  Cpus  Total Usable   Free  Other Traits  \n");
		printf("-------------------------------------------------------------\n");
	}

	for (my $k=0; $k < $ct; $k++) {
		my $part = $SINFO[$k]->{part};
		if ($verbose) {
			my $part2 = $part;
			$part2 =~ s/\*//;
			printf("%-9s  %6dMb %5s %6s %7s %6s %6s %11s  %-s\n",
				$part,
				$SINFO[$k]->{memory}, $SINFO[$k]->{cpu},
				$SINFO[$k]->{total},
				$SINFO[$k]->{usable},
				$SINFO[$k]->{idle},
				$MaxNodes{$part2}, $MaxTime{$part2},
				$SINFO[$k]->{feat});
		} else {
			printf("%-9s %6dMb %5s %6s %6s %6s  %-s\n",
				$part,
				$SINFO[$k]->{memory}, $SINFO[$k]->{cpu},
				$SINFO[$k]->{total},
				#$SINFO[$k]->{total} - $SINFO[$k]->{out},
				$SINFO[$k]->{usable},
				$SINFO[$k]->{idle},
				$SINFO[$k]->{feat});
		}
	}
	printf("\n");

	return;
}


#
# Get the SLURM queues.
#
sub do_squeue
{
	my %SQUEUE;
#
#	Base options on whether this partition is node or process scheduled.
#
	my ($type, $options);
	my $rval = system("scontrol show config | grep cons_res >> /dev/null");
	if ($rval) {
        	$type = "Nodes";
		$options =  "\"%8i  %8u %.6D %2t %S %.12l  %.9P %.11M  %1000R\"";
	} else {
        	$type = "Procs"; 
		$options =  "\"%8i  %8u %.6C %2t %S %.12l  %.9P %.11M  %1000R\"";
	}

#
#	Get the job information.
#
#	(Using a hash, but using a ctr as an index to maintain the order.)
#

	my $ct = 0;
	my @sout = `squeue -h -o $options`;
	foreach my $tmp (@sout) {
		next if ($running && $tmp =~ / PD /);
		chomp $tmp;
		my @line = split(' ', $tmp);
		my ($job,$user,$nodes,$status,$begin,$limit,$pool,$used,$master) = split(' ', $tmp);
		$begin =~ s/^.....//;
		$begin = "N/A" if ($status =~ /PD/);
		if ($limit ne "UNLIMITED") {
			$limit = convert_time($limit);
		}
#
#		Only keep the master node from the nodes list.
#
		$master =~ s/\[([0-9.]*).*/$1/;
		$SQUEUE{$ct}->{job}    = $job;
		$SQUEUE{$ct}->{user}   = $user;
		$SQUEUE{$ct}->{nodes}  = $nodes;
		$SQUEUE{$ct}->{status} = $status;
		$SQUEUE{$ct}->{begin}  = $begin;
		$SQUEUE{$ct}->{limit}  = $limit;
		$SQUEUE{$ct}->{pool}   = $pool;
		$SQUEUE{$ct}->{used}   = $used;
		$SQUEUE{$ct}->{master} = $master;
		$ct++;
	}


	printf("Running job data:\n");

	if ($verbose) {
		printf("---------------------------------------------------------------------------------------------------\n");
		printf("                                                 Time        Time            Time                  \n");
		printf("JobID    User      $type Pool      Status        Used       Limit         Started  Master/Other    \n");
		printf("---------------------------------------------------------------------------------------------------\n");
	} else {
		printf("----------------------------------------------------------------------\n");
		printf("JobID    User      $type Pool      Status        Used  Master/Other   \n");
		printf("----------------------------------------------------------------------\n");
	}

	for (my $k=0;$k < $ct; $k++) {
		if ($verbose) {
			printf("%-8s %-8s %6s %-9s %-7s %10s %11s  %14s  %.12s\n",
				$SQUEUE{$k}->{job},
				$SQUEUE{$k}->{user},  $SQUEUE{$k}->{nodes},
				$SQUEUE{$k}->{pool},  $SQUEUE{$k}->{status},
				$SQUEUE{$k}->{used},  $SQUEUE{$k}->{limit},
				$SQUEUE{$k}->{begin}, $SQUEUE{$k}->{master});
		} else {
			printf("%-8s %-8s %6s %-9s %-7s %10s  %.12s\n",
				$SQUEUE{$k}->{job},
				$SQUEUE{$k}->{user}, $SQUEUE{$k}->{nodes},
				$SQUEUE{$k}->{pool}, $SQUEUE{$k}->{status},
				$SQUEUE{$k}->{used}, $SQUEUE{$k}->{master});
		}
	}
	printf("\n");

	return;
}

#
# Get the SLURM partitions.
#
sub do_scontrol_part
{

#
#	Get All partition data Don't need it all now, but
#	it may be useful later.
#
	my @scon = `scontrol --oneliner show part`;
	my $part;
	foreach my $tmp (@scon) {
		chomp $tmp;
		my @line = split(' ',$tmp);
		($part) = ($tmp =~ m/PartitionName=(\S+)\s+/) if ($tmp =~ /PartitionName=/);
		($MaxTime{$part})  = ($tmp =~ m/MaxTime=(\S+)\s+/)  if ($tmp =~ /MaxTime=/);
		($MaxNodes{$part}) = ($tmp =~ m/MaxNodes=(\S+)\s+/) if ($tmp =~ /MaxNodes=/);
		$MaxTime{$part}  =~ s/UNLIMITED/UNLIM/ if ($MaxTime{$part});
		$MaxNodes{$part} =~ s/UNLIMITED/UNLIM/ if ($MaxNodes{$part}); 
printf("it is  $part mh:$MaxNodes{$part} mt:$MaxTime{$part}\n");

	}

	return;
}


#
# Show the man page.
#
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


#
# Convert the time to a better format.
#
sub convert_time
{
	my $val = shift(@_);

	my $tmp;
	my @field = split(/-|:/, $val);
	if (@field == 4) {
		$tmp = ($field[0]*24)+$field[1] . ':'.$field[2] . ':' . $field[3];
	} else {
		$tmp = sprintf("%8s",$val);
	}

	return($tmp);
}


#                 
# Get options.
#                 
sub get_options
{
	GetOptions(
		'help|h|?' => \$help,
		'man'      => \$man,
		'v'        => \$verbose,
		'r'        => \$running,
		'c'        => \$pool,
  	) or usage(1);

	show_man() if ($man);
	usage(0)   if ($help);

	return;
}


#
# Usage.
#
sub usage
{
	my $eval = shift(@_);

#
#	Print usage instructions and exit.
#
	print STDERR "\nUsage: mjstat [-h] [-c] p\[-man] [-r] [-v]\n";

	printf("\
   -h	shows usage.
   -c	shows computing resources info only.
   -man	shows man page.
   -r	show only running jobs.
   -v	is for the verbose mode.\n

   Output is very similar to that of squeue.
	\n\n");
	
	exit($eval);
}



__END__

=head1 NAME

B<mjstat> - List attributes of Moab jobs under the control of SLURM

=head1 SYNOPSIS

B<mjstat> [B<-h> ] [B<-c>] [B<-r> ] [B<-v>]

=head1 DESCRIPTION

The B<mjstat> command is used to display statistics of Moab jobs under control of 
SLURM. The output is designed to give information on the resource usage and availablilty,
as well as information about jobs that are currently active on the machine. This output
is built using the SLURM utilities, sinfo, squeue and scontrol, the man pages for these
utilites will provide more information and greater depth of understanding. 

=head1 OPTIONS

=over 4

=item B<-h> 

Display a brief help message

=item B<-c>

Display the computing resource information only.

=item B<-man> 

Show the man page.

=item B<-r>

Display only the running jobs.

=item B<-v>

Display more verbose information.

=back

=head1 EXAMPLE

The following is a basic request for status.

    > mjstat

     Scheduling pool data:
     ------------------------------------------------------------
     Pool         Memory  Cpus  Total Usable   Free  Other Traits
     ------------------------------------------------------------
     pdebug      15000Mb     8     32     32     24  (null)
     pbatch*     15000Mb     8   1072   1070    174  (null)


     Running job data:
     -------------------------------------------------------------------
     JobID    User      Nodes Pool       Status        Used Master/Other
     -------------------------------------------------------------------
     395      mary       1000 pbatch     PD            0:00 (JobHeld)
     396      mary       1000 pbatch     PD            0:00 (JobHeld)
     375      sam        1000 pbatch     CG            0:00 (JobHeld)
     388      fred         32 pbatch     R            25:27 atlas89
     361      harry       512 pbatch     R          1:01:12 atlas618
     1077742  sally         8 pdebug     R            20:16 atlas18


     The Scheduling data contains information pertaining to the:

 	Pool  	  a set of nodes
 	Memory	  the amount of memory on each node
 	Cpus	  the number of cpus on each node
 	Total	  the total number of nodes in the pool
 	Usable	  total usaable nodes in the pool
 	Free	  total nodes that are currently free

     The Running job data contains information pertaining to the:

 	JobID		either the Moab job id, or the SLURM interactive job id
 	User		owner of the job
 	Nodes		nodes required, or in use by the job
			(Note: On cpu scheduled machines, this field
			will be labled "Procs" show the number of processors
			the job is using.)
 	Pool 		the Pool  required or in use by the job
 	Status		current status of the job
 	Used 		Wallclick time used by the job
 	Master/Other 	Either the Master (head) node used by the job, or may
			indicate furhter status of a pending, or completing job.

     The common status values are:

 	R	The job is running
	PD	The job is Pending
	CG	The job is Completing

     These are states reproted by SLURM and more elaborate docuemntation
     can be found in the squeue/sinfo man pages.


 An example of the -v option.

     Scheduling pool data:
     -----------------------------------------------------------------------------
                                Total  Usable   Free   Node   Time  Other       
     Pool         Memory  Cpus  Nodes   Nodes  Nodes  Limit  Limit  Traits      
     -----------------------------------------------------------------------------
     pdebug      15000Mb     8     32      32     24     16     30  (null)
     pbatch*     15000Mb     8   1072    1070    174  UNLIM  UNLIM  (null)

     Running job data:
     ---------------------------------------------------------------------------------------------------
                                                      Time        Time            Time                  
     JobID    User      Nodes Pool      Status        Used       Limit         Started  Master/Other    
     ---------------------------------------------------------------------------------------------------
     38562    tom           4 pbatch    PD            0:00     1:00:00  01-14T18:11:22  (JobHeld)
     38676    sam         100 pbatch    R            27:27    12:00:00  01-19T07:11:27  triad34

     The added fields to the "Scheduling pool data" are:

 	Node Limit	SLURM imposed node limit.
 	Time Limit	SLURM imposed time limit, value in minutes.

     The added fields to the "Running job data" are:

 	Limit		Time limit of job.
 	Start		Start time of job.

=head1 REPORTING BUGS

Report problems to LC Hotline.

=cut

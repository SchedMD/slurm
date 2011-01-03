#! /usr/bin/perl -w
###############################################################################
#
# moab2slurm - change moab script to slurm script.
#
#
###############################################################################
#
# Copyright (C) 2010 Lawrence Livermore National Security.
# Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
# Written by Philip D. Eckert <eckert2@llnl.gov>
# CODE-OCEC-09-009. All rights reserved.
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
use vars qw(%optctl %requested_options);

#
# Define what each one of these means from moab key 
# and slurm value (0) and inputexpected (1).
#
my %options_list;

$options_list{"a"} = ["--begin=","=s"];
$options_list{"A"} = ["--account=","=s"];
$options_list{"d"} = ["--workdir=","=s"];
$options_list{"e"} = ["--error=","=s"];
$options_list{"j"} = ["join",""];
$options_list{"m"} = ["--mail-type=","=s"];
$options_list{"M"} = ["--mail-user=","=s"];
$options_list{"N"} = ["--job-name=","=s"];
$options_list{"o"} = ["--output=","=s"];
$options_list{"p"} = ["--nice=","=s"];
$options_list{"q"} = ["--partition=","=s"];
$options_list{"r"} = ["","=s"]; # special
$options_list{"v"} = ["--export=","=s"];
$options_list{"V"} = ["export",""]; # special


$options_list{"depend"}     = ["--dependency=","=s"];
$options_list{"feature"}    = ["--constraint=","=s"];
$options_list{"nodes"}      = ["--nodes=","=s"];
$options_list{"dmem"}       = ["--mem-per-cpu=","=s"];
$options_list{"resfail"}    = ["--resfail=","=s"];
$options_list{"signal"}     = ["--signal=","=s"];
$options_list{"ddisk"}      = ["--tmp=","=s"];
$options_list{"maxmem"}     = ["--mem=","=s"];
$options_list{"walltime"}   = ["--time=","=s"];
$options_list{"minwclimit"} = ["--time-min=","=s"];
$options_list{"qos"}        = ["--qos=","=s"];
$options_list{"ttc"}        = ["--ntasks=","=s"];


my %lcrm2slurm_env;

#
# SLURM ENV VARS
#
$lcrm2slurm_env{"MSUB_DEP_JOBID"} = "SLURM_DEPENDENCY";
$lcrm2slurm_env{"MSUB_HOST"} = "SLURM_SRUN_COMM_HOST";
#define for 1.2 $lcrm2slurm_env{"MSUB_HOST"} = "SLURM_STEP_LAUNCHER_HOSTNAME";
$lcrm2slurm_env{"MSUB_JOBID"} = "SLURM_JOBID";
#define for 1.2 $lcrm2slurm_env{"MSUB_JOBID"} = "SLURM_JOB_ID";
$lcrm2slurm_env{"MSUB_REQNAME"} = "SLURM_JOB_NAME";
$lcrm2slurm_env{"MSUB_WORKDIR"} = "SLURM_WORKING_DIR";

#
# Regular ENV VARS
#
$lcrm2slurm_env{"MSUB_HOME"} = "HOME";
$lcrm2slurm_env{"MSUB_LOGNAME"} = "LOGNAME";
$lcrm2slurm_env{"MSUB_SHELL"} = "SHELL";
$lcrm2slurm_env{"MSUB_TZ_ENV"} = "TZ";
$lcrm2slurm_env{"MSUB_USER"} = "USER";

#
# Unknown
#
$lcrm2slurm_env{"MSUB_SUBDIR"} = ""; #don't know what to do here

my $opt;
my $val;
my @output_lines;
my $line;
my $count = 0;
my $join = -1;
my $e_file;
my $e_found = -1;
my $Vflag = -1;


#
# Set up the hash from lcrm to moab based on the $options_list set above
#
while(($opt, $val) = each(%options_list)) {
	my @tmp = @$val;
	$optctl{"$opt$tmp[1]"} = \$requested_options{$opt};
}

#
# Convert_moabtime
#
# At most, only the day and hour need to be seperated by a '-' instead of a ':'.
#
sub convert_moabtime
{
	my ($duration) = @_;

	$duration = 0 unless $duration;

	$duration =~ s/^?:/-/ if ($duration =~ /.*:.*:.*:/);

	return($duration);
}

#
# Convert_moab2slurm will take a lcrm line and give back a line that is
# moab compliant.
#
sub convert_moab2slurm
{
    (@ARGV)  = @_;

    my $option;
    my $val;
     
    GetOptions(%optctl)
	    or die("Invalid MSUB options found in job command file.\n");
    
    my $out;
    my $previous;
    my $new_line;
    
    while(($option, $val) = each(%requested_options)) {

#
#		User want's input/oputput merged.
#

#
#		This one wasn't set don't use
#
		next if(!defined($val));

#		print "$option $val\n";
	    
#
#		Not supported by moab
#
		if($options_list{$option}[0] eq "") {
			print "option '-$option' is ignored in MOAB\n";
			undef($requested_options{$option});
			next;
		}


#
#		Handle error file in a special way. We will
#		save the path and add it at the end, if the
#		join option is not used.
#
		if ($option eq "e") {
			$e_found = 1;
			$e_file = $val;
			next;
		}

#
#		Check for the join option. Mention it at the
#		end as an explanation of the behavior.
#
		if ($option eq "j") {
			$join = 1;
			next;
		}

#
#		Check for the V option, which if Moab, will
#		inform the batch system to export all environment
#		variables. SLURM always does this, so it is a
#		moot point.  Mention it at the 	end as an 
#		explanation of the behavior.
#
		if ($option eq "V") {
			$Vflag = 1;
			next;
		}

#
#		Check to see if there was an arg or not
#
		if($options_list{$option}[1] ne "") { 
			$previous = "-$option $val";
			if ($option =~ /walltime|minwclimit/) {
				$val = convert_moabtime($val);
			} 
			$out = "$options_list{$option}[0]$val";
		    
		} else {
			$out = "$options_list{$option}[0]";
			$previous = "-$option";
		}

                if ($option eq "m") {
			$out .= "--mail-type=BEGIN " if ($val =~ /b/);
			$out .= "--mail-type=END "   if ($val =~ /e/);
			$out .= "--mail-type=FAIL "  if ($val =~ /a/);
                }

                if ($option eq "r") {
			if ($val =~ /n/) {
				$out = "--no-requeue";
			} else {
				$out = "--requeue";
			}
		}

		if ($option eq "resfail") {
			$out = "--requeue"    if ($option eq "requeue");
			$out = "--no-requeue" if ($option eq "cancel");
			$out = "--no-kill"    if ($option eq "ignore");
		}

#
#		Change the -l options to hide the fact that we have mangled
#		them a bit to make them more parseable.

		$previous =~ s/\-/-l / if ($previous !~ /\-. /);
		$output_lines[$count++] = 
			sprintf("##MSUB #%s\n#SLURM %s\n", 
				$previous, $out);
#
#		Since this function could be used multiple times we want
#		to unset this so we don't print it out multiple times
#
		undef($requested_options{$option});
	}

	return;
}


sub convert_lcrm2slurm
{
# MSUB_DEP_JOBID    (when a dependent job exists) (NEW) SLURM_DEPENDENCY
# MSUB_HOME             set to $HOME on the submission host n/a
# MSUB_HOST              the submission host name 1.1 SLURM_SRUN_COMM_HOST 1.2 SLURM_STEP_LAUNCHER_HOSTNAME
# MSUB_JOBID             the Moab job identifier 1.1 SLURM_JOBID 1.2 SLURM_JOB_ID
# MSUB_LOGNAME     set to $LOGNAME on the submission host n/a
# MSUB_PATH              set to $PATH on the submission host n/a
# MSUB_REQNAME     the job's specified (-r) or assigned job name (NEW) SLURM_JOB_NAME
# MSUB_SHELL the path name of the shell that interprets the job script n/a
# MSUB_SUBDIR          the directory from which you invoke psub on the submitting host n/a
# MSUB_TZ_ENV          set to $TZ on the submission host only when it was defined n/a
# MSUB_USER               set to $USER on the submission host n/a
# MSUB_WORKDIR      the directory from which you invoke psub on the submitting host (NEW) SLURM_WORKING_DIR
	(my $new_line) = @_;

	while(($opt, $val) = each(%lcrm2slurm_env)) {
		$new_line =~ s/$opt/$val/;
	}
	
	return $new_line;
}


my $script_file;
my $tmp_script_file;
my $help;
    
GetOptions("i=s"=>\$script_file,
	   "o=s"=>\$tmp_script_file,
	   "H|help|?|man"=>\$help)
	or die("Invalid moab2slurm option found in command line.\n");

if($help) {
	print "moab2slurm Usage:\n";
	print "  moab2slurm is used to convert legacy moab scripts with\n";
	print "  \#MSUB options in to the currect MOAB infrastructure.\n";
	print "  The old \#MSUB options will remain in the file, only commented out.\n";
	print "\t-i (--input)  moab_script\n";
	print "\t-o (--output) output_script\n";
	print "\t--help - This screen\n";
	print "\nnote: If no output_script is given the new script will\n";
	print "be named something like ./jobScript.moab.$$.\n";

	exit(0);
}    

if(!$script_file) {
	die("No input file given use -i option to specify.\n");
}

if(!$tmp_script_file) {
	$tmp_script_file = "./jobScript.moab.$$";
}

open SCRIPTIN, " $script_file" or
	die("Unable to open job command file ($script_file) " .
	    "for reading: $!\n");

open SCRIPTOUT, "> $tmp_script_file" or 
	die("Unable to open temporary job command file ($tmp_script_file) " . 
	    "for writing: $!\n");

# Parse job script
my @script_directives = ();
my $assigned_shell;


foreach $line (<SCRIPTIN>) {
	if ($line =~ s/^\s*\#\s*MSUB\s+//) {
		chomp $line;
		$line =~ s/#.*//;    # Remove comments
		$line =~ s/\s+$//;   # Remove trailing whitespace
		$line =~ s/l /-/g;   # Remove trailing whitespace
                $line =~ s/,/ -/g;   # Remove trailing whitespace

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
		convert_moab2slurm(@args2);
	} elsif ($line =~ "MSUB_") {
		$output_lines[$count++] = convert_moab2slurm($line);
	} elsif ($line =~ s/^ENVIRONMENT\s+//) {
		# ENVIRONMENT       set to "BATCH"
		$output_lines[$count++] = "ENVIRONMENT=BATCH\n";
	} elsif ($line =~ s/^SESSARGS\s+//) {
		# SESSARGS                 the arguments to psub
		chomp $line;
		$line =~ s/#.*//;    # Remove comments
		$line =~ s/\s+$//;   # Remove trailing whitespace
		my @args = split /\s+/, $line;
		convert_moab2slurm(@args);
	} else {
		$output_lines[$count++] = $line;
	}
}
close SCRIPTIN;

#
# Add these at the end, as they depend on more than one option, or they are
# sepical.
#
if ($Vflag) {
	$output_lines[$count++] = 
		sprintf("##MSUB #-V # This is default behavior in SLURM.\n"); 
}

if ($join) {
	$output_lines[$count++] = 
		sprintf("##MSUB #-j # Removing -e line (if it exists) , slurm will combine.\n"); 
}

if ($e_found ne -1 && $join == -1) {
	$output_lines[$count++] =
		sprintf("##MSUB -e %s\n#SLURM --error=%s\n", $e_file, $e_file);
}

print SCRIPTOUT @output_lines if $tmp_script_file;
close SCRIPTOUT;
print "wrote moab file $tmp_script_file\n";


__END__

=head1 NAME

B<moab2slurm> - change maob script to slurm script.

=head1 SYNOPSIS

B<moab2slurm> -i <moab_script> [-o output_script>]

=head1 DESCRIPTION

The B<moab2slurm> command is used to convert legacy moab scripts with \#MSUB options in to the currect MOAB infrastructure.  The old \#MSUB options will remain in the file, only commented out.altersubmit a job in slurm format. The job id will be displayed on successful submission of the job.

If no output_script is given the new script will be named something like ./jobScript.moab.$$.

An example of this is as follows:

   #
   #  Batch job command file example:
   #
   #  MSUB -A sci                  # Set bank to sci
   #  MSUB -l walltime=4:00:00     # Send wallclock limit to 4 hours
   #  MSUB                         # No more embedded options.
   #
   nmake all

will be changed to 

   #
   #  Batch job command file example:
   #
   #SBATCH -A sci                          ##MSUB -b sci
   #SBATCH --time=4:00:00                  ##MSUB -tW 4:00:00
   #
   nmake all



=head1 OPTIONS

=over 4

=item B<-i> I<moab_script>

Specifies the script you want to change (This actual file will not be altered, only opened).

=item B<-o> I<output_file>

Specifies the name of the script you to output as.  This script will have all the modifications to make this run with native msub.

=item B<-H, -?, --help>

Display a brief help message

=item B<--man>

Display full documentation

=back

=head1 EXAMPLE

moab2slurm -i moab.sh -o slurm.sh

=head1 REPORTING BUGS

Report problems to LC Hotline.

=cut

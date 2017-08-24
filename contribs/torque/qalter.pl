#! /usr/bin/perl -w
###############################################################################
#
# qalter - PBS wrapper for changing job status using scontrol
#
###############################################################################

use strict;
use FindBin;
use Getopt::Long 2.24 qw(:config no_ignore_case);
use lib "${FindBin::Bin}/../lib/perl";
use autouse 'Pod::Usage' => qw(pod2usage);
use Slurm ':all';
use Slurmdb ':all'; # needed for getting the correct cluster dims
use Switch;

# ------------------------------------------------------------------
# This makes the assumption job_id will always be the last argument
# -------------------------------------------------------------------
my $job_id = $ARGV[$#ARGV];
my (
	$err,
	$new_name,
	$output,
	$rerun,
	$resp,
	$slurm,
	$man,
	$help
);

# Remove this
my $scontrol = "/usr/slurm/bin/scontrol";

# ------------------------------
# Parse Command Line Arguments
# ------------------------------
GetOptions(
	'N=s'    => \$new_name,
	'r=s'    => \$rerun,
	'o=s'    => \$output,
	'help|?' => \$help,
	'man'    => \$man
	)
	or pod2usage(2);

pod2usage(0) if $help;

if ($man)
{
	if ($< == 0)    # Cannot invoke perldoc as root
	{
		my $id = eval { getpwnam("nobody") };
		$id = eval { getpwnam("nouser") } unless defined $id;
		$id = -2			  unless defined $id;
		$<  = $id;
	}
	$> = $<;			# Disengage setuid
	$ENV{PATH} = "/bin:/usr/bin";	# Untaint PATH
	delete @ENV{'IFS', 'CDPATH', 'ENV', 'BASH_ENV'};
	if ($0 =~ /^([-\/\w\.]+)$/) {
		$0 = $1;		# Untaint $0
	} else {
		die "Illegal characters were found in \$0 ($0)\n";
	}
	pod2usage(-exitstatus => 0, -verbose => 2);
}

# ----------------------
# Check input arguments
# ----------------------
if (@ARGV < 1) {
	pod2usage(-message=>"Missing Job ID", -verbose=>0);
} else {
	$slurm = Slurm::new();
	if (!$slurm) {
		die "Problem loading slurm.\n";
	}
	$resp = $slurm->get_end_time($job_id);
	if (not defined($resp)) {
		pod2usage(-message=>"Job id $job_id not valid!", -verbose=>0);
	}
	if ((not defined($new_name)) and (not defined($rerun)) and (not defined($output))) {
		pod2usage(-message=>"no argument given!", -verbose=>0);
	}
}

# --------------------------------------------
# Use Slurm's Perl API to change name of a job
# --------------------------------------------
if ($new_name) {
	my %update = ();

	$update{job_id}  = $job_id;
	$update{name}    = $new_name;
	if (Slurm->update_job(\%update)) {
		$err = Slurm->get_errno();
		$resp = Slurm->strerror($err);
		pod2usage(-message=>"Job id $job_id name change error: $resp", -verbose=>0);
		exit(1);
	}
}

# ---------------------------------------------------
# Use Slurm's Perl API to change the requeue job flag
# ---------------------------------------------------
if ($rerun) {
	my %update = ();

	$update{job_id}  = $job_id;
	if (($rerun eq "n") || ($rerun eq "N")) {
		$update{requeue} = 0;
	} else {
		$update{requeue} = 1;
	}
	if (Slurm->update_job(\%update)) {
		$err = Slurm->get_errno();
		$resp = Slurm->strerror($err);
		pod2usage(-message=>"Job id $job_id requeue error: $resp", -verbose=>0);
		exit(1);
	}
}

# ------------------------------------------------------------
# Use Slurm's Perl API to change Comment string
# Comment is used to relocate an output log file
# ------------------------------------------------------------
if ($output) {
	# Example:
	# $comment="on:16337,stdout=/gpfsm/dhome/lgerner/tmp/slurm-16338.out,stdout=~lgerner/tmp/new16338.out";
	#
	my $comment;
	my %update = ();

	# ---------------------------------------
	# Get current comment string from job_id
	# ---------------------------------------
	my($job) = $slurm->load_job($job_id);
	$comment = $$job{'job_array'}[0]->{comment};

	# ----------------
	# Split at stdout
	# ----------------
	if ($comment) {
		my(@outlog) = split("stdout", $comment);

		# ---------------------------------
		# Only 1 stdout argument add a ','
		# ---------------------------------
		if ($#outlog < 2) {
			$outlog[1] .= ","
		}

		# ------------------------------------------------
		# Add new log file location to the comment string
		# ------------------------------------------------
		$outlog[2] = "=".$output;
		$comment = join("stdout", @outlog);
	} else {
		$comment = "stdout=$output";
	}

	# -------------------------------------------------
	# Make sure that "%j" is changed to current $job_id
	# -------------------------------------------------
	$comment =~ s/%j/$job_id/g ;

	# -----------------------------------------------------
	# Update comment and print usage if there is a response
	# -----------------------------------------------------
	$update{job_id}  = $job_id;
	$update{comment} = $comment;
	if (Slurm->update_job(\%update)) {
		$err = Slurm->get_errno();
		$resp = Slurm->strerror($err);
		pod2usage(-message=>"Job id $job_id comment change error: $resp", -verbose=>0);
		exit(1);
	}
}
exit(0);

##############################################################################

__END__

=head1 NAME

B<qalter> - alter a job name, the job rerun flag or the job output file name.

=head1 SYNOPSIS

qalter [-N Name]
       [-r y|n]
       [-o output file]
       <job ID>

=head1 DESCRIPTION

The B<qalter> updates job name, job rerun flag or job output(stdout) log location.

It is aimed to be feature-compatible with PBS' qsub.

=head1 OPTIONS

=over 4

=item B<-N>

Update job name in the queue

=item B<-r>

Alter a job rerunnable flag. "y" will allow a qrerun to be issued. "n" disable qrerun option.

=item B<-o>

Alter a job output log file name (stdout).

The job log will be move/rename after the job has B<terminated>.

=item B<-?> | B<--help>

brief help message

=item B<-man>

full documentation

=back

=head1 SEE ALSO

qrerun(1) qsub(1)
=cut


#! /usr/bin/perl -w
###############################################################################
#
# qsub - submit a batch job in familar pbs format.
#
#
###############################################################################
#  Copyright (C) 2007 The Regents of the University of California.
#  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
#  Written by Danny Auble <auble1@llnl.gov>.
#  LLNL-CODE-402394.
#  
#  This file is part of SLURM, a resource management program.
#  For details, see <http://www.llnl.gov/linux/slurm/>.
#  
#  SLURM is free software; you can redistribute it and/or modify it under
#  the terms of the GNU General Public License as published by the Free
#  Software Foundation; either version 2 of the License, or (at your option)
#  any later version.
#
#  In addition, as a special exception, the copyright holders give permission 
#  to link the code of portions of this program with the OpenSSL library under
#  certain conditions as described in each individual source file, and 
#  distribute linked combinations including the two. You must obey the GNU 
#  General Public License in all respects for all of the code used other than 
#  OpenSSL. If you modify file(s) with this exception, you may extend this 
#  exception to your version of the file(s), but you are not obligated to do 
#  so. If you do not wish to do so, delete this exception statement from your
#  version.  If you delete this exception statement from all source files in 
#  the program, then also delete it here.
#  
#  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
#  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
#  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
#  details.
#  
#  You should have received a copy of the GNU General Public License along
#  with SLURM; if not, write to the Free Software Foundation, Inc.,
#  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
#  
###############################################################################

use strict;
use FindBin;
use Getopt::Long 2.24 qw(:config no_ignore_case require_order);
use lib "${FindBin::Bin}/../lib/perl";
use autouse 'Pod::Usage' => qw(pod2usage);
use Slurm ':all';
use Switch;

my ($start_time,
    $account,
 #   $checkpoint_interval,
    $directive_prefix,
    $err_path,
    $interactive,
    $hold,
#    $join,
#    $keep,
    $resource_list,
    $mail_options,
    $mail_user_list,
    $job_name,
    $out_path,
    $priority,
    $destination,
#    $rerunable,
#    $script_path,
#    $running_user_list,
#    $variable_list,
#    $all_env,
    $additional_attributes,
#    $no_std,
    $help,
    $man);

my $sbatch = "${FindBin::Bin}/sbatch";
my $salloc = "${FindBin::Bin}/salloc";
my $srun = "${FindBin::Bin}/srun";

GetOptions('a=s'      => \$start_time,
	   'A=s'      => \$account,
#	   'c=i'      => \$checkpoint_interval,
	   'C=s'      => \$directive_prefix,
	   'e=s'      => \$err_path,
	   'h'        => \$hold,
	   'I'        => \$interactive,
#	   'j:s'      => \$join,
#	   'k=s'      => \$keep,
	   'l=s'      => \$resource_list,
	   'm=s'      => \$mail_options,
	   'M=s'      => \$mail_user_list,
	   'N=s'      => \$job_name,
	   'o=s'      => \$out_path,
	   'p=i'      => \$priority,
	   'q=s'      => \$destination,
#	   'r=s'      => \$rerunable,
#	   'S=s'      => \$script_path,
#	   'u=s'      => \$running_user_list,
#	   'v=s'      => \$variable_list,
#	   'V'        => \$all_env,
	   'W'        => \$additional_attributes,
#	   'z'        => \$no_std,
	   'help|?'   => \$help,
	   'man'      => \$man,
	   )
	or pod2usage(2);

# Display usage if necessary
pod2usage(0) if $help;
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

# Use sole remaining argument as jobIds
my $script;
if ($ARGV[0]) {
	foreach (@ARGV) {
	        $script .= "$_ ";
	}
} else {
        pod2usage(2);
}
my %res_opts;
my %node_opts;

if($resource_list) {
	%res_opts = %{parse_resource_list($resource_list)};
		
# 	while((my $key, my $val) = each(%res_opts)) {
# 		print "$key = ";
# 		if($val) {
# 			print "$val\n";
# 		} else {
# 			print "\n";
# 		}
# 	}
	
	if($res_opts{nodes}) {
		%node_opts =  %{parse_node_opts($res_opts{nodes})};
	}
}

my $command;

if($interactive) {
	$command = "$salloc";
	
} else {
	$command = "$sbatch";
	
	$command .= " -D $directive_prefix" if $directive_prefix;
	$command .= " -e $err_path" if $err_path;
	$command .= " -o $out_path" if $out_path;
}

$command .= " -N$node_opts{node_cnt}" if $node_opts{node_cnt};
$command .= " -n$node_opts{task_cnt}" if $node_opts{task_cnt};
$command .= " -w$node_opts{hostlist}" if $node_opts{hostlist};

if($res_opts{walltime}) {
	$command .= " -t$res_opts{walltime}";
} elsif($res_opts{cput}) {
	$command .= " -t$res_opts{cput}";
} elsif($res_opts{pcput}) {
	$command .= " -t$res_opts{pcput}";
}

$command .= " --tmp=$res_opts{file}" if $res_opts{file};
$command .= " --mem=$res_opts{mem}" if $res_opts{mem};
$command .= " --nice=$res_opts{nice}" if $res_opts{nice};


$command .= " --begin=$start_time" if $start_time;
$command .= " --account=$account" if $account;
$command .= " -H" if $hold;

if($mail_options) {
	$command .= " --mail-type=FAIL" if $mail_options =~ /a/;
	$command .= " --mail-type=BEGIN" if $mail_options =~ /b/;
	$command .= " --mail-type=END" if $mail_options =~ /e/;
}
$command .= " --mail-user=$mail_user_list" if $mail_user_list;
$command .= " -J $job_name" if $job_name;
$command .= " --nice=$priority" if $priority;
$command .= " -p $destination" if $destination;
$command .= " -C $additional_attributes" if $additional_attributes;


$command .= " $script";

system($command);


sub parse_resource_list {
	my ($rl) = @_;
	my %opt = ('arch' => "",
		   'cput' => "",
		   'file' => "",
		   'host' => "",
		   'mem' => "",
		   'nice' => "",
		   'nodes' => "",
		   'opsys' => "",
		   'other' => "",
		   'pcput' => "",
		   'pmem' => "",
		   'pvmem' => "",
		   'software' => "",
		   'vmem' => "",
		   'walltime' => ""
		   );
	my @keys = keys(%opt);
	
	foreach my $key (@keys) {
		#print "$rl\n";
		($opt{$key}) = $rl =~ m/$key=([\w:\+=+]+)/;
		
	}
	if($opt{cput}) {
		$opt{cput} = get_minutes($opt{cput});
	}

	if($opt{mem}) {
		$opt{mem} = convert_mb_format($opt{mem});
	}

	if($opt{file}) {
		$opt{file} = convert_mb_format($opt{file});
	}
	
	return \%opt;
}

sub parse_node_opts {
	my ($node_string) = @_;
	my %opt = ('node_cnt' => 0,
		   'hostlist' => "",
		   'task_cnt' => 0
		   );
	while($node_string =~ /ppn=(\d+)/g) {
		$opt{task_cnt} += $1;
	}

	my $hl = Slurm::Hostlist::create("");

	my @parts = split(/\+/, $node_string);
	foreach my $part (@parts) {
		my @sub_parts = split(/:/, $part);
		foreach my $sub_part (@sub_parts) {
			if($sub_part =~ /ppn=(\d+)/) {
				next;
			} elsif($sub_part =~ /^(\d+)/) {
				$opt{node_cnt} += $1;
			} else {
				if(!Slurm::Hostlist::push($hl, $sub_part)) {
					print "problem pushing host $sub_part onto hostlist\n";
				}
			}
		}
	}
	
	$opt{hostlist} = Slurm::Hostlist::ranged_string($hl);

	my $hl_cnt = Slurm::Hostlist::count($hl);
	$opt{node_cnt} = $hl_cnt if $hl_cnt > $opt{node_cnt};
	
	# we always want at least one here 
	if(!$opt{node_cnt}) {
		
		$opt{node_cnt} = 1;
	}
	
	# figure out the amount of tasks based of the node cnt and the amount 
	# of ppn's in the request
	if($opt{task_cnt}) {
		$opt{task_cnt} *= $opt{node_cnt};
	}
	
	return \%opt;
}

sub get_minutes {
    my ($duration) = @_;
    $duration = 0 unless $duration;
    my $minutes = 0;

    # Convert [[HH:]MM:]SS to duration in minutes
    if ($duration =~ /^(?:(\d+):)?(\d*):(\d+)$/) {
        my ($hh, $mm, $ss) = ($1 || 0, $2 || 0, $3);
	$minutes += 1 if $ss > 0;
        $minutes += $mm;
        $minutes += $hh * 60;
    } elsif ($duration =~ /^(\d+)$/) {  # Convert number in minutes to seconds
	    my $mod = $duration % 60;
	    $minutes = int($duration / 60);
	    $minutes++ if $mod;
    } else { # Unsupported format
        die("Invalid time limit specified ($duration)\n");
    }

    return $minutes;
}

sub convert_mb_format {
	my ($value) = @_;
	my ($amount, $suffix) = $value =~ /(\d+)($|[KMGT])/i;
	return if !$amount;
	$suffix = lc($suffix); 

	if (!$suffix) {
		$amount /= 1048576;
	} elsif ($suffix eq "k") {
		$amount /= 1024;
	} elsif ($suffix eq "m") {
		#do nothing this is what we want.
	} elsif ($suffix eq "g") {
		$amount *= 1024;
	} elsif ($suffix eq "t") {
		$amount *= 1048576;
	} else { 
		print "don't know what to do with suffix $suffix\n";
		return;
	}

	$amount .= "M";

	return $amount;
}
##############################################################################

__END__

=head1 NAME

B<qsub> - submit a batch job in a familiar pbs format

=head1 SYNOPSIS

qsub  [-a date_time] [-A account_string] [-b secs] [-c interval] 
      [-C directive_prefix] [-e path] [-h] [-I]
      [-j join] [-k keep] [-l resource_list] [-m mail_options]
      [-M  user_list] [-N name] [-o path] [-p priority] [-q destination]
      [-r c] [-S path_list] [-u user_list] [-v variable_list] [-V]
      [-W additional_attributes] [-z] [script]
    
=head1 DESCRIPTION

The B<qsub> command displays information about nodes.

=head1 OPTIONS

=over 4

=item B<-a>

Display information for all nodes. This is the default if no node name is specified.

=item B<-? | --help>

brief help message

=item B<--man>

full documentation

=back

=cut


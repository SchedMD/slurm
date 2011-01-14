#! /usr/bin/perl 

use strict;

# A utility to emulate the pshare utility
# which displays share information for 
# users and accountrs.
#
# Author:        Phil Eckert
# Date:          09/06/2007
# Last Modified: 01/14/2011
#
# Now, we use sshare data to create the
# functionallity for SLURM.
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

use lib "/opt/freeware/lib/site_perl"; # for AIX at LLNL
use strict;
use FindBin;
use Getopt::Long 2.24 qw(:config no_ignore_case);
use lib "${FindBin::Bin}/../tools";
use autouse 'Pod::Usage' => qw(pod2usage);
use File::Basename;


my $pct = 0;
my $level = 0;
my (@field, @save_acct, @partition);
my (%ACCT , %USER);
use vars qw($help $T_account_name $t_account_name $a_account_name $r_account_name
    	    $host_name $noheader $user_name $level_arg $zero_arg $man);
my ($part, $account, $parent, $indx);
my ($acct, $na, $name, $usage, $alloc, $type, $ts);

#
# Get options.
#
getoptions();

build_data();
print_data();


sub build_data
{

	my ($level, $type);

	my @out;
	if ($host_name) {
		@out = `sshare -a -p -M $host_name`;
	} else {
		@out = `sshare -a -p`;
	}

	
	my $once = 1;
		
#	my ($ns, $nu);
#
#	Everytime we find a new account bump
#	the level indicator.
#


	foreach my $line (@out) {
		my ($blanks) = ($line =~ m/(^\s+)/);
		my $level = length($blanks);
		$line =~ s/^\s+//;
		my ($account,$user,$rs,$ns,$ru,$eu,$fs) = split /\|/, $line;
		$type = "user";
		$type = "acct" if ($user eq "");
		if ($type eq "acct") { $level++; }

#
#		Partition changes come as part of the root account record.
#		The totalshare field is not set, so set it to share. Also,
#		initialize some variables since it is a new partition.			

		if ($once) {
			$partition[$pct++] = $host_name;
			$part = $host_name;
			@save_acct = ();
			$indx = 0;
			$once = 0;
		}
#
#		Loop through the data and build the acct/user data.
#
		if ($type eq "acct") {
			$acct = $account;
			if ($level) { $parent = $save_acct[$level-1]; }
			else { $parent = $name; }
			$ACCT{$indx}{$part}->{index}        = $level;
			$ACCT{$indx}{$part}->{parent}       = $parent;
			$ACCT{$indx}{$part}->{acct}         = $account;
			$ACCT{$indx}{$part}->{alloc}        = $rs;
			$ACCT{$indx}{$part}->{norm_alloc}   = $ns;
			$ACCT{$indx}{$part}->{norm_usage}   = $ru;
			$ACCT{$indx}{$part}->{effect_usage} = $eu;
			$ACCT{$indx}{$part}->{rair_share}   = $fs;

#
#			The normalized share and usage is handled differently for
#			the root level account.
#

			$ACCT{$part}->{count}++;
			$save_acct[$level] = $acct;
			$indx++;
		} elsif ($type eq "user") {
			$name = $user;
			my $pacct = $account;
			$USER{$pacct}{$part}{$name}->{user}         = $name;
			$USER{$pacct}{$part}{$name}->{alloc}        = $rs;
			$USER{$pacct}{$part}{$name}->{acct}         = $pacct;
			$USER{$pacct}{$part}{$name}->{index}        = $level;
			$USER{$pacct}{$part}{$name}->{norm_alloc}   = $ns;
			$USER{$pacct}{$part}{$name}->{norm_usage}   = $ru;
			$USER{$pacct}{$part}{$name}->{effect_usage} = $eu;
			$USER{$pacct}{$part}{$name}->{fair_share}   = $fs;
		}

	}
}

#
# Trace the tree from child to highest level parent (root).
#
sub do_tree
{
	my $a_count = shift(@_);
	my $part    = shift(@_);
#
#	While we don't indent the output, the indentation is
#	a guide to parent child relationships.
#
	my $save_indent = 100;
	for (my $i = $a_count; $i > 0; $i--) {
		my $indent = $ACCT{$i}{$part}->{index};
		next if ($indent >= $save_indent);
		$save_indent = $indent;
		printf("%-12s   %-12s         %8d    %6.6f    %14d   %6.6f   %9.6f\n",
			$ACCT{$i}{$part}->{acct},
			$ACCT{$i}{$part}->{parent},
			$ACCT{$i}{$part}->{alloc},
			$ACCT{$i}{$part}->{norm_alloc},
			$ACCT{$i}{$part}->{norm_usage},
			$ACCT{$i}{$part}->{effect_usage},
			$ACCT{$i}{$part}->{fair_share},
		);
	}
	printf("\n");

	return;
}

#
# Various headers, depending on the option selected.
#
sub headings
{


	my $t1 = "USERNAME       ACCOUNT                          --------SHARES-------        ---------USAGE-------";
	my $t2 = "                                                ALLOCATED  NORMALIZED        NORMALIZED  EFFECTIVE   FAIRSHARE";

	my $u1 = "USERNAME       ACCOUNT             --------SHARES-------        ---------USAGE--------";
	my $u2 = "                                   ALLOCATED  NORMALIZED        NORMALIZED   EFFECTIVE  FAIRSHARE";

	my $a1 = "ACCOUNT        PARENT              --------SHARES-------        ---------USAGE--------";
	my $a2 = "                                   ALLOCATED  NORMALIZED        NORMALIZED   EFFECTIVE   FAIRSHARE";


	if ($T_account_name || $t_account_name) {
		printf("%s\n", $t1);
		printf("%s\n", $t2);
	} elsif ($a_account_name || $r_account_name) {
		printf("%s\n",$a1);
		printf("%s\n",$a2);
	} elsif ($user_name) {
		printf("%s\n",$u1);
		printf("%s\n",$u2);
	}

	return;
}

#
# Print output depending on options.
#
sub print_data
{

#
# Print out the tree(s).
#
  for (my $p = 0; $p < $pct; $p++) {
	$part = $partition[$p];
	printf("\nPartition $host_name\n\n");
	headings() if (!$noheader);
	my $found_user = 0;
	my $found_account = 0;
	my $relative_index = 0;
        for (my $j = 0; $j < $ACCT{$part}->{count}; $j++) {
		if ($account) {
			next if (defined $level_arg && $ACCT{$j}{$part}->{index} > $level_arg);
			my $aval = $ACCT{$j}{$part}->{acct};
			next if ($zero_arg && ($ACCT{$j}{$part}->{norm_usage} == 0.0));
#
#			The level of indentation is reduced by the level that
#			the requested account starts at.
#			
			last if ($found_account &&
				($ACCT{$j}{$part}->{index} <= $relative_index));
			if ($account eq $aval) {
				$relative_index = $ACCT{$j}{$part}->{index};
				$found_account = 1;
				if ($r_account_name) {
					do_tree($j, $part);
					last;
				}
			}
#
#			If not the requested account look at the next one.
#
			next if (!$found_account);
#
#			If -a and we have already found our account,
#			then we are done.
#
			last if ($a_account_name && $account ne $aval);
		
			my $indent = $ACCT{$j}{$part}->{index} -
					$relative_index;
			my $part1 = sprintf("%*s%-12s   %-12s",
				$indent,"",
				$ACCT{$j}{$part}->{acct},
				$ACCT{$j}{$part}->{parent}
			);
#
#			Spacing is different different depending on option.
#
			my $gap = 40;
			my $aflag = "<A";
			if ($a_account_name) {
				$gap = 27;
				$aflag ="";
			}
			printf("%-*s         %8d    %6.6f    %14d   %6.6f   %9.6f  %s\n",
				$gap,
				$part1,
				$ACCT{$j}{$part}->{alloc},
				$ACCT{$j}{$part}->{norm_alloc},
				$ACCT{$j}{$part}->{norm_usage},
				$ACCT{$j}{$part}->{effect_usage},
				$ACCT{$j}{$part}->{fairshare},
				$aflag
			);

#
#			If -a or -T there is no need to deal with user
#			information, but continue on if -t.
#
			next if ($a_account_name || $T_account_name || 
				 !defined $USER{$aval}{$part});

			foreach my $k (sort keys %{$USER{$aval}{$part}}) {
				next if ($zero_arg && ($USER{$aval}{$part}{$k}->{norm_usage} == 0.0));
				my $indx = ($USER{$aval}{$part}{$k}->{index}+2);
				my $indent = $indx - $relative_index;
				my $part1 = sprintf("%*s%-12s   %-12s",
					$indent,"",
					$USER{$aval}{$part}{$k}->{user},
					$USER{$aval}{$part}{$k}->{acct},
				);
				printf("%-40s         %8d    %6.6f    %14d   %6.6f   %9.6f  <U\n",
					$part1,
					$USER{$aval}{$part}{$k}->{alloc},
					$USER{$aval}{$part}{$k}->{norm_alloc},
					$USER{$aval}{$part}{$k}->{norm_usage},
					$USER{$aval}{$part}{$k}->{effect_usage},
					$USER{$aval}{$part}{$k}->{fairshare},
				);
			}
#
#		Handle -u option.
#
		} elsif ($user_name) {
			my $aval = $ACCT{$j}{$part}->{acct};
			next if (!defined $USER{$aval}{$part});
			foreach my $k (sort keys %{$USER{$aval}{$part}}) {
				if ($user_name eq $USER{$aval}{$part}{$k}->{user}) {
					$found_user = 1;
					printf("%-12s   %-12s         %8d    %6.6f    %14d    %6.6f  %9.6f\n",
						$USER{$aval}{$part}{$k}->{user},
						$USER{$aval}{$part}{$k}->{acct},
						$USER{$aval}{$part}{$k}->{alloc},
						$USER{$aval}{$part}{$k}->{norm_alloc},
						$USER{$aval}{$part}{$k}->{norm_usage},
						$USER{$aval}{$part}{$k}->{effect_usage},
						$USER{$aval}{$part}{$k}->{fair_share}
					);
				}
			}
		}
	}
#
#	Report if user/account not found.
#
	printf("Unknown user: $user_name\n")  if ($user_name && !$found_user);
	printf("Unknown account: $account\n") if ($account && !$found_account);
  }

  return;
}



sub getoptions
{
	my $argct = $#ARGV;

#
#	Set default partition name.
#
	GetOptions(
		'H'          => \$noheader,
		'help|h|?'   => \$help,
		'T=s'        => \$T_account_name,
		't=s'        => \$t_account_name,
		'a=s'        => \$a_account_name,
		'l=s'        => \$level_arg,
		'r=s'        => \$r_account_name,
		'p=s'        => \$host_name,
		'u=s'        => \$user_name,
		'man'        => \$man,
		'0|O'        => \$zero_arg,
#
# 	Grandfathered in the old options for host and account.
#
		'b=s'        => \$a_account_name,
		'm=s'        => \$host_name,
	) or usage();

#
#	Display usage/man pages.
#
	show_man() if ($man);
	usage()    if ($help);

#
#	Determine host name if one is not provided.
#
	if (!$host_name) {
		my $tmp = `scontrol show config  | grep ClusterName`;
		($host_name) = ($tmp =~ m/ = (\S+)/);
	}

#
#	Only one option in the following list can be selected at a time, if more than
#	one is set, then complain and exit.
#
	if (((defined $T_account_name) + (defined $t_account_name) +
	     (defined $a_account_name) + (defined $r_account_name) +
	     (defined $user_name)) > 1) {
		printf("\n Only one accout/user option permitted at a time.\n\n");
		usage();
	}

#
#	The level argument can only be used with -t and -T.
#
	if (defined $level_arg) {
		if (!$T_account_name && !$t_account_name) {
			printf("-l may only be used with -t or -T\n");
			exit;
		}
	}

#
#	Make sure there aren't too many arguments. There should
#	only be 1, or two if there is a host specified.
#
#	if (($argct == 3 && $host_name eq "") || ($argct > 3)) {
#		usage();
#	}

	return if (defined $user_name);

#
#	Only one account name can be set, so this will relsult in getting
#	the account name based on the options.
#
	$account = $t_account_name if (defined $t_account_name); 
	$account = $a_account_name if (defined $a_account_name); 
	$account = $r_account_name if (defined $r_account_name);
	$account = $T_account_name if (defined $T_account_name);

	$user_name = getpwuid($>) if (!defined $account);

	return;
}

sub usage
{
	my $base = basename($0);

	printf("\
 Usage: $base [-u user] [-p partition] [-0]
        $base [-a account] [-p partition] [-0]
        $base [-T account] [-p partition] [-0] [-l value]
        $base [-t account] [-p partition] [-0] [-l value]
        $base [-r account] [-p partition] [-0]
        $base [-h]

	-u <user name>		Show user and all accounts and share usage for named user.
	-a <account name>	Show account and share usage for named account.
	-l <levels>		Number of levels of a sub-tree to display if the -T or -t option is used. 
	-T <account name>	Show share usage for the named account and all sub-accounts and users.
	-t <account name>	Show share usage for named account and all sub-accounts.
	-r <account name>	Show named account, and share usage for accounts up the tree.
	-p <partition name>	Show nformation for this partition only.
	-0			Do not show entries which have no usage.
	-h 			Show usage.
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

B<mshare> - Displays resource allocation and usage information from the Moab database.
While this utility is similar to the LCRM pshare utility, it does not support all the same options.

=head1 SYNOPSIS

       mshare	 [-a account] [-u user] [-p partition] [-0]
       mshare	 -[t|T] account [-p partition]  [-0] [-l levels]
       mshare	 -r account [-p hostname] [-0]
       mshare	 -h
       mshare	 -man

=head1 DESCRIPTION

mshare reads the Moab fairshare and generates share and usage reports to stdout.
Information written depends on options specified on the command line.

If the -a option is used, information about accounts is written. If
the -u option is used, information about user allocations is written.
If the -t or -T option is used, information about accounts and (in the
case of the -t option) user allocations is written in such a manner as
to show the tree structure of the database. If the -r option is used,
information about the specified account and all its parents up through
and including the root account is written. If none of the above options
are used, then information about the calling user's allocations is
written.

Moab implements a "Fair Share" allocation scheme. The fair share system
attempts to regulate short term "rate-of-delivery" to help assure that
users get good response time and turn-around to the extent that they have
been allocated access to compute resources. Users and groups of users (that
is, within the context of accounts) are granted shares that represent their
"claim" on the computing resources being managed by Moab.

Each machine within the Moab domain is a member of one and only one resource
partition. A resource partition may have one member computer or it may have
many. Shares are allocated to each user and account for each resource partition.
A share represents a user's or an account's claim on the computational resources
available in a resource partition. A user or account may apply the shares
allocated to it for a resource partition on any of the hosts in a resource
partition.

When an account is created or a user is permitted to use the resources granted
to an account, shares are allocated to that account or user for that resource
partition. The shares a user or account has, when divided by the total number of
shares allocated to all its siblings, represents the portion of the shares
granted to the parent account that are claimed by the user or account.

Normalized shares represent the portion of the entire resource partition that
the user or account has been allocated.

Each user allocation and account has associated with it a "usage" value. This
value is incremented whenever a user uses the resources of a computer. It is
also "decayed" on a regular basis based on a "half-life decay" algorithm. If a
usage value is not incremented during the half-life decay period, its value will
be cut in half after that period.

Moab regulates resource usage by assigning queued job priorities based on the
discrepancy between the normalized shares and the actual usage.

The information written is displayed in multi-column format. The headings for
the columns are as follows (depending on whether the -u, the -a or -r, or
the -t or -T option is used, respectively):

=head1 FIELDS

RESOURCE PARTITION:  partition_name

USERNAME ACCOUNT   --------SHARES--------      ------USAGE------
                   ALLOCATED   NORMALIZED         NORMALIZED 

ACCOUNT  PARENT    --------SHARES--------      ------USAGE------
                   ALLOCATED   NORMALIZED         NORMALIZED 

U/A NAME A/P NAME  --------SHARES--------      ------USAGE------
                   ALLOCATED   NORMALIZED         NORMALIZED 

The columns are relatively self-explanatory:

=over 2

=item SHARES ALLOCATED

the share granted to the user or account which may be applied to the resource
partition of which the specified or implied hostname is a member.

=item SHARES NORMALIZED

the share of the resource partition expressed as a portion of the total shares
assigned to the resource partition.

=item USAGE NORMALIZED

the half-life decayed usage value expressed as a portion of the total usage
on the resource partition being reported.

The decay rate is set by a Moab manager and represents the amount of time
which if an account or user allocation is not used, it will decay to half
its starting value.

=item U/A

user name or account as appropriate

=item A/P

account or parent as appropriate

=head1 OPTIONS

=over 4

=item B<-0>

Do not list records that have 0 raw usage.

=item B<-a> I<account>

Information about the account is displayed. This option may not be used with the -r, -T or -t options.

=item B<-h>

A usage summary message is displayed, and mshare terminates.

=item B<-l> I<levels>

The number of levels of a sub-tree to display if the -T or -t option is used. This option may not
be used if neither the -t option nor the -T option is used.

=item B<-p> I<partition>

Will show allocation information for the resource partition of which hostname
is a member. If this option is not used, the value for hostname is the host
on which mshare is being executed. To see allocation information for all
resource partitions in the Moab domain, use ALL as the hostname.

=item B<-r> I<account> 

Records are written for the specified account and each of its parents up
through and including the root account. This option may not be used with
the -a, -T, -t or -u options.

=item B<-T> I< account>

Records are written for the account specified. This option may not be used
with the -a, -r, -t or -u options.

=item B<-t> I<account>

This option is identical to the -T option except that, in addition, user allocations to the listed accounts are also listed. This option may not be used with the -a, -r, -T or -u options.

=item B<-u> I <user>

Information about the users in the list is displayed. This option may not be used with the -r, -T or -t options.

=back

=head1 EXAMPLES

mshare report the status of the calling user's allocations to his/her current account on the resource partition of which the host on which mshare is being executed is a member.

mshare -u joan,
report the status of user joan's access to all their accounts

mshare -a sab
report the status of the account sab.

mshare -t fll
report the status of all child nodes of fll.

mshare -T root -0
report the status of all accounts from which some time has been used. Do not report user allocations.

mshare -t root -0
report the status of all accounts and user allocations from which some time was used.

mshare -r sab
report the status of account sab and all of its parents up through the root account.

mshare -u joan -a sab
report all allocations for user joan and report the status of account sab

=head1 EXIT CONDITIONS

If there is an error, mshare returns an exit status of 1.

=head1 AUTHOR

Written by Philip D. Eckert

=head1 REPORTING BUGS

Report bugs to <pdesr@llnl.gov>

=head1 SEE ALSO

(Understanding Job Priority Calculation on Moab Scheduled Machines -  UCRL-SM-230043)



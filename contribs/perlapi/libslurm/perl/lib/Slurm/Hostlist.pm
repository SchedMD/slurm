package Slurm::Hostlist;

1;

__END__

=head1 NAME

Slurm::Hostlist - Hostlist functions in libslurm

=head1 SYNOPSIS

 use Slurm;

 $hostnames = "node1,node[2-5,12]";
 $hl = Slurm::Hostlist::create($hostnames);

 $cnt = $hl->count;

 $hl->push("node21,node[27-34]");

 while($host = $hl->shift()) {
	print $host, "\n";
 }

 print $hl->ranged_string(), "\n";

=head1 DESCRIPTION

The Slurm::Hostlist class is a wrapper of the hostlist functions in libslurm. This package is loaded and bootstrapped with package Slurm.

=head1 METHODS

=head2 $hl = Slurm::Hostlist::new($str);

Create a new hostlist from a string representation. Returns an opaque hostlist object. This is a B<CLASS METHOD>. 

The string representation ($str) may contain one or more hostnames or bracketed hostlists separated by either `,' or whitespace. A bracketed hostlist is denoted by a common prefix followed by a list of numeric ranges contained within brackets: e.g. "tux[0-5,12,20-25]".

To support systems with 3-D topography, a rectangular prism may be described using two three digit numbers separated by "x": e.g. "bgl[123x456]". This selects all nodes between 1 and 4 inclusive in the first dimension, between 2 and 5 in the second, and between 3 and 6 in the third dimension for a total of 4*4*4=64 nodes.

If $str is omitted, and empty hostlist is created and returned.

=head2 $cnt = $hl->count();

Return the number of hosts in the hostlist.

=head2 $pos = $hl->find($hostname);

Searches hostlist $hl for the first host matching $hostname and returns position in list if found.

Returns -1 if host is not found.

=head2 $cnt = $hl->push($hosts);

Push a string representation of hostnames onto a hostlist. The $hosts argument may take the same form as in create(). 

Returns the number of hostnames inserted into the list,

=head2 $cnt = $hl->push_host($hostname);

Push a single host onto the hostlist hl.

This function is more efficient than slurm_hostlist_push() for a single hostname, since the argument does not need to be checked for ranges.

Return value is 1 for success, 0 for failure.

=head2 $str = $hl->ranged_string();

Return the string representation of the hostlist $hl. ranged_string() will write a bracketed hostlist representation where possible.

=head2 $host = $hl->shift();

Returns the string representation of the first host in the hostlist or `undef' if the hostlist is empty or there was an error allocating memory. The host is removed from the hostlist.

=head2 $hl->uniq();

Sort the hostlist $hl and remove duplicate entries.


=head1 SEE ALSO

Slurm

=head1 AUTHOR

This library is created by Hongjia Cao, E<lt>hjcao(AT)nudt.edu.cnE<gt> and Danny Auble, E<lt>da(AT)llnl.govE<gt>. It is distributed with SLURM.

=head1 COPYRIGHT AND LICENSE

This library is free software; you can redistribute it and/or modify
it under the same terms as Perl itself, either Perl version 5.8.4 or,
at your option, any later version of Perl 5 you may have available.

=cut

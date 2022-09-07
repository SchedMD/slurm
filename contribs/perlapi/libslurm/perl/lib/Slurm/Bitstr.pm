package Slurm::Bitstr;

1;

__END__

=head1 NAME

Slurm::Bitstr - Bitstring functions in libslurm

=head1 SYNOPSIS

 use Slurm;

 $bitmap = Slurm::Bitstr::alloc(32);
 if ($bitmap->test(10)) {
 	print "bit 10 is set\n";
 }

=head1 DESCRIPTION

The Slurm::Bitstr class is a wrapper of the bit string functions in libslurm. This package is loaded and bootstrapped with package Slurm.

=head1 METHODS

=head3 $bitmap = Slurm::Bitstr::alloc($nbits);

Allocate a bitstring object with $nbits bits. An opaque bitstr object is returned. This is a B<CLASS METHOD>.

=head3 $bitmap->realloc($nbits);

Reallocate a bitstring(expand or contract size). $nbits is the number of bits in the new bitstring.

=head3 $len = $bitmap->size();

Return the number of possible bits in a bitstring.

=head3 $cond = $bitmap->test($n);

Check if bit $n of $bitmap is set.

=head3 $bitmap->set($n);

Set bit $n of $bitmap.

=head3 $bitmap->clear($n);

Clear bit $n of $bitmap.

=head3 $bitmap->nset($start, $stop);

Set bits $start .. $stop in $bitmap.

=head3 $bitmap->nclear($start, $stop);

Clear bits $start .. $stop in $bitmap.

=head3 $pos = $bitmap->ffc();

Find first bit clear in $bitmap.

=head3 $pos = $bitmap->nffc($n)

Find the first $n contiguous bits clear in $bitmap.

=head3 $pos = $bitmap->noc($n, $seed);

Find $n contiguous bits clear in $bitmap starting at offset $seed.

=head3 $pos = $bitmap->nffs($n);

Find the first $n contiguous bits set in $bitmap.

=head3 $pos = $bitmap->ffs();

Find first bit set in $bitmap;

=head3 $pos = $bitmap->fls();

Find last bit set in $bitmap;

=head3 $bitmap->fill_gaps();

Set all bits of $bitmap between the first and last bits set(i.e. fill in the gaps to make set bits contiguous).

=head3 $cond = $bitmap1->super_set($bitmap2);

Return 1 if all bits set in $bitmap1 are also set in $bitmap2, 0 otherwise.

=head3 $cond = $bitmap1->equal($bitmap2);

Return 1 if $bitmap1 and $bitmap2 are identical, 0 otherwise.

=head3 $bitmap1->and($bitmap2);

$bitmap1 &= $bitmap2.

=head3 $bitmap->not();

$bitmap = ~$bitmap.

=head3 $bitmap1->or($bitmap2);

$bitmap1 |= $bitmap2.

=head3 $new = $bitmap->copy();

Return a copy of the supplied bitmap.

=head3 $dest_bitmap->copybits($src_bitmap);

Copy all bits of $src_bitmap to $dest_bitmap.

=head3 $n = $bitmap->set_count();

Count the number of bits set in bitstring.

=head3 $n = $bitmap1->overlap($bitmap2);

Return number of bits set in $bitmap1 that are also set in $bitmap2, 0 if no overlap.

=head3 $n = $bitmap->clear_count();

Count the number of bits clear in bitstring.

=head3 $n = $bitmap->nset_max_count();

Return the count of the largest number of contiguous bits set in $bitmap.

=head3 $sum = $bitmap->inst_and_set_count($int_array);

And $int_array and $bitmap and sum the elements corresponding to set entries in $bitmap.

=head3 $new = $bitmap->rotate_copy($n, $nbits);

Return a copy of $bitmap rotated by $n bits. Number of bit in the new bitmap is $nbits.

=head3 $bitmap->rotate($n);

Rotate $bitmap by $n bits.

=head3 $new = $bitmap->pick_cnt($nbits);

Build a bitmap containing the first $nbits of $bitmap which are set.

=head3 $str = $bitmap->fmt();

Convert $bitmap to range string format, e.g. 0-5,42

=head3 $rc = $bitmap->unfmt($str);

Convert range string format to bitmap.

=head3 $array = Slurm::Bitstr::bitfmt2int($str);

Convert $str describing bitmap (output from fmt(), e.g. "0-30,45,50-60") into an array of integer (start/edn) pairs terminated by -1 (e.g. "0, 30, 45, 45, 50, 60, -1").

=head3 $str = $bitmap->fmt_hexmask();

Given a bit string, allocate and return a string in the form of:
    "0x0123ABC\0"
       ^     ^
       |     |
      MSB   LSB

=head3 $rc = $bitmap->unfmt_hexmask($str);

Give a hex mask string "0x0123ABC\0", convert to a bit string.
                          ^     ^
                          |     |
                         MSB   LSB

=head3 $str = $bitmap->fmt_binmask();

Given a bit string, allocate and return a binary string in the form of:
                            "0001010\0"
                             ^     ^
                             |     |
                            MSB   LSB

=head3 $rc = $bitmap->unfmt_binmask($str);

Give a bin mask string "0001010\0", convert to a bit string.
                        ^     ^
                        |     |
                       MSB   LSB

=head3 $pos = $bitmap->get_bit_num($n);

Find position of the $n-th set bit(0 based, i.e., the first set bit is the 0-th) in $bitmap. Returns -1 if there are less than $n bits set.

=head3 $n = $bitmap->get_pos_num($pos);

Find the number of bits set minus one in $bitmap between bit position [0 .. $pos]. Returns -1 if no bits are set between [0 .. $pos].


=head1 SEE ALSO

L<Slurm>

=head1 AUTHOR

This library is created by Hongjia Cao, E<lt>hjcao(AT)nudt.edu.cnE<gt> and Danny Auble, E<lt>da(AT)llnl.govE<gt>. It is distributed with Slurm.

=head1 COPYRIGHT AND LICENSE

This library is free software; you can redistribute it and/or modify
it under the same terms as Perl itself, either Perl version 5.8.4 or,
at your option, any later version of Perl 5 you may have available.

=cut

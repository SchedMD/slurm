#!/usr/bin/perl
use IO::Socket;
my $sock1 = new IO::Socket::INET (
		LocalHost => 'localhost',
		LocalPort => '7071',
		Proto => 'tcp',
		Listen => 1,
		Reuse => 1,
		);
while ( true )
{
	my $new_sock1 = $sock1->accept();
	$pid = fork ();
	if ($pid)
	{

		while(<$new_sock1>) 
		{
			print STDOUT $_;
		}
		close($sock1);
		print "CLOSED STD OUT SOCKET" ;
	}
}

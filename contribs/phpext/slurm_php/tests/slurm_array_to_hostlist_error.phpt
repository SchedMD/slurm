--TEST--
Test function slurm_array_to_hostlist() by calling it more than or less than its expected arguments
--CREDIT--
Jimmy Tang <jtang@tchpc.tcd.ie>
--SKIPIF--
<?php
extension_loaded("slurm") or die("skip slurm extension not loaded\n");
function_exists("slurm_array_to_hostlist") or die("skip function slurm_array_to_hostlist unavailable");
?>
--FILE--
<?php

echo "*** Test by calling method or function with incorrect numbers of arguments ***\n";

$extra_arg = array();

$ret = slurm_array_to_hostlist( $extra_arg );
if ($ret < 0)
	echo "! ret $ret < 0";

/* this needs to be implemented better */
/*
$ret = slurm_array_to_hostlist(  );
if ($ret < 0)
	echo "! ret $ret < 0";
*/
?>
--EXPECTF--
*** Test by calling method or function with incorrect numbers of arguments ***
! ret -2 < 0

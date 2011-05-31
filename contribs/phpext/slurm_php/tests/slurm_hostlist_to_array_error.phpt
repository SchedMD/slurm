--TEST--
Test function slurm_hostlist_to_array() by calling it more than or less than its expected arguments
--CREDIT--
Jimmy Tang <jtang@tchpc.tcd.ie>
--SKIPIF--
<?php
extension_loaded("slurm") or die("skip slurm extension not loaded\n");
function_exists("slurm_hostlist_to_array") or die("skip function slurm_hostlist_to_array unavailable");
?>
--FILE--
<?php

echo "*** Test by calling method or function with incorrect numbers of arguments ***\n";

$extra_arg = NULL;

var_dump(slurm_hostlist_to_array( $extra_arg ) );

// var_dump(slurm_hostlist_to_array(  ) );

?>
--EXPECTF--
*** Test by calling method or function with incorrect numbers of arguments ***
int(-3)

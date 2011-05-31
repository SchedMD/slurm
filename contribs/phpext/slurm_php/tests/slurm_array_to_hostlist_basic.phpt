--TEST--
Test function slurm_array_to_hostlist() by calling it with its expected arguments
--CREDIT--
Jimmy Tang <jtang@tchpc.tcd.ie>
--SKIPIF--
<?php
extension_loaded("slurm") or die("skip slurm extension not loaded\n");
function_exists("slurm_array_to_hostlist") or die("skip function slurm_array_to_hostlist unavailable");
?>
--FILE--
<?php

echo "*** Test by calling method or function with its expected arguments ***\n";

$hosts=array();

array_push($hosts, "host01");
array_push($hosts, "host02");
array_push($hosts, "another-host02");

var_dump(slurm_array_to_hostlist($hosts));

?>
--EXPECT--
*** Test by calling method or function with its expected arguments ***
array(1) {
  ["HOSTLIST"]=>
  string(26) "host[01-02],another-host02"
}

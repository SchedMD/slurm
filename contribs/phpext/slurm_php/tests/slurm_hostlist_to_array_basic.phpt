--TEST--
Test function slurm_hostlist_to_array() by calling it with its expected arguments
--CREDIT--
Jimmy Tang <jtang@tchpc.tcd.ie>
--SKIPIF--
<?php
extension_loaded("slurm") or die("skip slurm extension not loaded\n");
function_exists("slurm_hostlist_to_array") or die("skip function slurm_hostlist_to_array unavailable");
?>
--FILE--
<?php
echo "*** Test by calling method or function with its expected arguments ***\n";

$hosts = "host[01-02],another-host02";
var_dump(slurm_hostlist_to_array($hosts));

?>
--EXPECT--
*** Test by calling method or function with its expected arguments ***
array(3) {
  [0]=>
  string(6) "host01"
  [1]=>
  string(6) "host02"
  [2]=>
  string(14) "another-host02"
}

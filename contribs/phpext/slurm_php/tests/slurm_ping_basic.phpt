--TEST--
Test function slurm_ping() by calling it with its expected arguments
--SKIPIF--
<?php
extension_loaded("slurm") or die("skip slurm extension not loaded\n");
function_exists("slurm_ping") or die("skip function slurm_ping unavailable");
?>
--FILE--
<?php
$value=slurm_ping();
var_dump($value["Prim. Controller"]);
?>
--EXPECT--
int(0)

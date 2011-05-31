--TEST--
Test function slurm_get_partition_node_names() by calling it with its expected arguments
--FILE--
<?php


echo "*** Test by calling method or function with its expected arguments ***\n";

$ret = slurm_get_partition_node_names("debug");

if ($ret)
	echo "! slurm_get_partition_node_names ok";

?>
--EXPECT--
*** Test by calling method or function with its expected arguments ***
! slurm_get_partition_node_names ok

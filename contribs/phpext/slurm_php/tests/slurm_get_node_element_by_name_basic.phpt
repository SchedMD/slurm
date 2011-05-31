--TEST--
Test function slurm_get_node_element_by_name() by calling it with its expected arguments
--CREDIT--
Peter Vermeulen <nmb_pv@hotmail.com>
--SKIPIF--
<?php
extension_loaded("slurm") or die("skip slurm extension not loaded\n");
function_exists("slurm_get_node_element_by_name") or die("skip function slurm_get_node_element_by_name() unavailable");
?>
--FILE--
<?php
echo "*** Test by calling method or function without any arguments ***\n";

$node_info = slurm_get_node_element_by_name(5);
if($node_info == -1) {
	echo "[SLURM:ERROR] -1 : No node by that name was found on your system\n";
}

$node_info = slurm_get_node_element_by_name(NULL);
if($node_info == -3) {
	echo "[SLURM:ERROR] -3 : Faulty variables ( or no variables ) where passed on\n";
}

$node_info = slurm_get_node_element_by_name('');
if($node_info == -3) {
	echo "[SLURM:ERROR] -3 : Faulty variables ( or no variables ) where passed on\n";
}

?>
--EXPECT--
*** Test by calling method or function without any arguments ***
[SLURM:ERROR] -1 : No node by that name was found on your system
[SLURM:ERROR] -3 : Faulty variables ( or no variables ) where passed on
[SLURM:ERROR] -3 : Faulty variables ( or no variables ) where passed on

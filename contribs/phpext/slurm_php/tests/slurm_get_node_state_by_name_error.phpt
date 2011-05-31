--TEST--
Test function slurm_get_node_state_by_name() by calling it with its expected arguments
--CREDIT--
Peter Vermeulen <nmb_pv@hotmail.com>
--SKIPIF--
<?php
extension_loaded("slurm") or die("skip slurm extension not loaded\n");
function_exists("slurm_get_node_state_by_name") or die("skip function slurm_get_node_state_by_name() unavailable");
?>
--FILE--
<?php
echo "*** Test by calling method or function with faulty arguments ***\n";

$state = slurm_get_node_state_by_name("ez79e8ezaeze46aze1ze3aer8rz7r897azr798r64f654ff65ds4f56dsf4dc13xcx2wc1wc31c");
if($state == -1) {
	echo "[SLURM:ERROR] -1 : No node by that name was found on your system\n";
} else if($state == -2) {
	echo "[SLURM:ERROR] -2 : Daemons not online";
}

?>
--EXPECT--
*** Test by calling method or function with faulty arguments ***
[SLURM:ERROR] -1 : No node by that name was found on your system

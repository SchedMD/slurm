--TEST--
Test function slurm_get_node_states() by calling it with its expected arguments
--CREDIT--
Peter Vermeulen <nmb_pv@hotmail.com>
--SKIPIF--
<?php
extension_loaded("slurm") or die("skip slurm extension not loaded\n");
function_exists("slurm_get_node_states") or die("skip function slurm_get_node_states() unavailable");
?>
--FILE--
<?php
echo "*** Test by calling method or function with correct arguments ***\n";

$state = slurm_get_node_states();

if(is_array($state)) {
	if(count($state)==0) {
		echo "[SLURM:ERROR] -1 : No nodes found on your system\n";
	} else {
		echo "[SLURM:SUCCESS] : slurm_get_node_states() succesfully returned it's data";
	}
} else if($state == -2) {
	echo "[SLURM:ERROR] -2 : Daemons not online";	
}
?>
--EXPECT--
*** Test by calling method or function with correct arguments ***
[SLURM:SUCCESS] : slurm_get_node_states() succesfully returned it's data

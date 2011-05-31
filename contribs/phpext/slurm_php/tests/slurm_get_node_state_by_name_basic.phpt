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

$state = slurm_get_node_state_by_name(5);
if($state == -1) {
	echo "[SLURM:ERROR] -1 : No node by that name was found on your system\n";
} else if($state == -2) {
	echo "[SLURM:ERROR] -2 : Daemons not online";
	exit();
}

$state = slurm_get_node_state_by_name(NULL);
if($state == -3) {
	echo "[SLURM:ERROR] -3 : Faulty variables ( or no variables ) where passed on\n";
} else if($state == -2) {
	echo "[SLURM:ERROR] -2 : Daemons not online";
}

$state = slurm_get_node_state_by_name('');
if($state == -3) {
	echo "[SLURM:ERROR] -3 : Faulty variables ( or no variables ) where passed on\n";
} else if($state == -2) {
	echo "[SLURM:ERROR] -2 : Daemons not online";
}

?>
--EXPECT--
*** Test by calling method or function with faulty arguments ***
[SLURM:ERROR] -1 : No node by that name was found on your system
[SLURM:ERROR] -3 : Faulty variables ( or no variables ) where passed on
[SLURM:ERROR] -3 : Faulty variables ( or no variables ) where passed on

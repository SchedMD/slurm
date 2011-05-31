--TEST--
Test function slurm_get_control_configuration_values() by calling it with its expected arguments
--CREDIT--
Peter Vermeulen <nmb_pv@hotmail.com>
--SKIPIF--
<?php
extension_loaded("slurm") or die("skip slurm extension not loaded\n");
function_exists("slurm_get_control_configuration_values") or die("skip function slurm_get_control_configuration_values unavailable");
?>
--FILE--
<?php
echo "*** Test by calling method or function with its expected arguments ***\n";


$config_values_arr = slurm_get_control_configuration_values();

if(is_array($config_values_arr)){
	if(count($config_values_arr)==0) {
		$config_values_arr = -1;
	}
}

if((gettype($config_values_arr)=="array") && ($config_values_arr != NULL)) {
	echo "! slurm_get_control_configuration_values	:	SUCCESS";
} else if($config_values_arr == -3) {
	echo "[SLURM:ERROR] -3 : Faulty variables ( or no variables ) where passed on";
} else if($config_values_arr == -2) {
	echo "[SLURM:ERROR] -2 : Daemons not online";
} else if($config_values_arr == -1) {
	echo "[SLURM:ERROR] -1 : No configuration data was found on your system";
}  else {
	echo "[SLURM:ERROR] -4 : ?Unknown?";
}


?>
--EXPECT--
*** Test by calling method or function with its expected arguments ***
! slurm_get_control_configuration_values	:	SUCCESS

############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import re

def test_usage():
    """Verify sstat -e displays a list of format fields"""

    output = atf.run_command_output(f"sstat -e", fatal=True)
            
    assert re.search(r'^(?:\w+\s+){50,}$', output) is not None
    assert re.search(r'AveDiskRead', output) is not None
    assert re.search(r'MinCPU', output) is not None
    assert re.search(r'MaxVMSize', output) is not None
    assert re.search(r'TRESUsageInTot', output) is not None
    assert re.search(r'Nodelist', output) is not None

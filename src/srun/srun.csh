#!/bin/csh
@ i = 0
while ($i < 2000)
./srun -n1 /bin/hostname
@ i++
end

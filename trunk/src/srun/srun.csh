#!/bin/csh
@ i = 0
while ($i < 200)
./srun -n4 -N1 -O -b tst
sleep 1
@ i++
end

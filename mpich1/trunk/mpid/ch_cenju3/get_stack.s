#include <sys/regdef.h>

          .text
          .align 2
          .globl MPID_CENJU3_Get_Stack
          .ent MPID_CENJU3_Get_Stack 2
MPID_CENJU3_Get_Stack:
          move v0, sp   /* return stack pointer */
          j    ra
          .end MPID_CENJU3_Get_Stack

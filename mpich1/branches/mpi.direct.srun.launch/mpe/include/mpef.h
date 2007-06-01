C
C  Fortran includes for MPE
C
      integer MPE_WHITE, MPE_BLACK, MPE_RED, MPE_YELLOW, MPE_GREEN,
     *        MPE_CYAN, MPE_BLUE, MPE_MAGENTA, MPE_AQUAMARINE,
     *        MPE_FORESTGREEN, MPE_ORANGE, MPE_MAROON, MPE_BROWN,
     *        MPE_PINK, MPE_CORAL, MPE_GRAY
      parameter( MPE_WHITE = 0, MPE_BLACK = 1, MPE_RED = 2,
     *           MPE_YELLOW = 3, MPE_GREEN = 4, MPE_CYAN = 5,
     *           MPE_BLUE = 6, MPE_MAGENTA = 7, MPE_AQUAMARINE = 8,
     *           MPE_FORESTGREEN = 9, MPE_ORANGE = 10, MPE_MAROON = 11,
     *           MPE_BROWN = 12, MPE_PINK = 13, MPE_CORAL = 14,
     *           MPE_GRAY = 15 )
C
C A large number of features have been setup to be C-Callable only,
C through the use of global arrays.  An example are the logic and button
C ops.
C
C
C  MPE Error codes
C
      integer MPE_SUCCESS, MPE_ERR_NOXCONNECT, MPE_ERR_BAD_ARGS,
     *        MPE_ERR_LOW_MEM
      parameter (MPE_SUCCESS = 0, MPE_ERR_NOXCONNECT = 1, 
     *           MPE_ERR_BAD_ARGS = 2, MPE_ERR_LOW_MEM = 3)

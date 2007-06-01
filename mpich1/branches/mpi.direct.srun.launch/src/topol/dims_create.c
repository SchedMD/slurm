/*
 *  $Id: dims_create.c,v 1.15 2002/04/24 19:34:34 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

/* 
 * Donated by Tom Henderson
 * Date:     1/19/94
*/

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Dims_create = PMPI_Dims_create
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Dims_create  MPI_Dims_create
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Dims_create as PMPI_Dims_create
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif
#include <stdio.h>
#include <math.h>
#include "mpimem.h"

/* Guess at the 1/bth root of a */
/*#define MPIR_guess(a,b)  ((a)/(b))*/
/*#define MPIR_guess(a,b)  pow((a),(1.0/(b))*/
#define MPIR_guess(a,b)  MPIR_root((a),(b))

/* Prototype to suppress warnings about missing prototypes */
int MPIR_root( double, double);
static int getFirstBit ( int, int * );
static int factorAndCombine ( int, int, int * );
 
/* Simple function to make a guess at the root of a number */
#define ROOT_ITERS 10
int MPIR_root(double x_in, double n_in)
{
  int      n = (int)n_in;
  int      x = (int)x_in;
  unsigned long i, j, r ;
  unsigned long guess, high, low ;
 
  if (n == 0 || x == 0)
    return (1);
 
  r = n ;
  for(i=1;i<(unsigned long)n;i++)
    r*=n ;
  r = x/r ;
  guess = 1<<(31/n) ;
  guess-- ;
  if(r<guess)
    guess = r ;
  high = guess ;
  low = 1 ;
  for(j=0;j<ROOT_ITERS;j++) {
    r = guess ;
    for(i=1;i<(unsigned long)n;i++)
      r *= guess ;
    if(r > (unsigned long)x) {
      high = guess ;
      guess = (guess - low)/2 + low ;
    } else {
      low = guess ;
      guess = (high-guess)/2 + guess ;
    }
  }
 
  if (guess > 0)
    return (int)(guess) ;
  else
    return (1);
}

/*-------------------------------------------------------------------------- 
** getFirstBit()
** 
** The getFirstBit() function finds the least significant non-zero 
** bit in int inputInt, sets all bits more significant than this bit to zero, 
** and returns the modified value.  If no bit in inputInt is set, the function 
** returns 0.  The contents of int *bitPositionPtr is set to the position of 
** the bit.  Bit position is numbered from 0.  Bit position will be set to 
** -1 if no bit in inputInt is set.  
** 
** Return Values
** 
** The getFirstBit() function will only return a negative number if 
** the most significant bit is the only bit set in inputInt.  Note that all 
** possible values of inputInt are valid so no error conditions are returned 
** from this function.  
** 
** Author:   Tom Henderson
** 
** Date:     2/8/93
** 
** Vendor-dependent?:  NO
** 
** Functions called by this function:  
**     None.
** 
**--------------------------------------------------------------------------*/ 

static int getFirstBit(int inputInt, int *bitPositionPtr)
    {
    int mask, saveMask, bitPosition;

    /* Check for zero input.  (This avoids endless while loop). */
    if (inputInt == 0)
        {
        *bitPositionPtr = -1;
        return(0);
        }

    /* Find least significant bit set in inputInt. */
    mask = 1;
    saveMask = 0;
    bitPosition = 0;
    while (saveMask == 0)
        {
        if ((mask & inputInt) != 0)
            {
            saveMask = mask;
            }
        mask <<= 1;
        bitPosition++;
        }  /* end of saveMask while loop */

    *bitPositionPtr = bitPosition - 1;                      /* LSB == bit 0 */
    return(saveMask);
    }    /* end of function getFirstBit() */


/*-------------------------------------------------------------------------- 
** factorAndCombine()
** 
** This function finds numFactors factors of factorMe and returns them in 
** array factors.  Array factors must be large enough to hold numFactors 
** values.  Factors are sorted in descending order (factors[0] will be the 
** largest, factors[numFactors - 1] will be the smallest).  In cases where 
** the number of prime factors is less than numFactors, remaining entries in 
** array factors will be set to 1.  When numFactors is less than the number of 
** prime factors, the prime factors will be re-combined and resorted in such a 
** way that the values in the output array are as close together as possible.  
** Factors are ordered from maximum to minimum value.  
** 
** The factoring algorithm is the simple "factoring by division" algorithm 
** from D. Knuth's Seminumerical Algorithms p. 364.  It will factor any 
** positive number less than one million.  It uses a look-up table containing 
** the first 168 prime numbers.  
** 
** The recombination algorithm is a tree search with pruning.  
** 
** Return Values
** 
** The factorAndCombine() function returns 0 if the function 
** returns without error.  Any other return value indicates an error.  
** 
** Restrictions
** 
** Input values factorMe and numFactors must be positive.  factorMe must be 
** less than (MAX_PRIME * MAX_PRIME).  MAX_PRIME is the last prime in the 
** look-up table.  
** 
** Author:   Tom Henderson
** 
** Date:     2/8/93
** 
** Modifications:
**   1/19/94:  Tom Henderson
**   Fixed bug.  See "BUGFIX".  This bug was producing bad factoring for a 
**   few cases (like (60,2), (96,2), etc.).  Actually, it's amazing that this 
**   worked at all with the bug!  
** 
** Vendor-dependent?:  NO
** 
** Functions called by this function:  
**     getFirstBit()
** 
**--------------------------------------------------------------------------*/ 

static int factorAndCombine(int factorMe, int numFactors, int *factors)
{
  typedef struct BranchInfo	{
	int currentBranch;                 /* encoded branch identification */
	int nextBranch;                              /* next branch to take */
	int currentValue;                                /* value at branch */
  } BranchInfo;

#define NUM_PRIMES 168
  static int primes[NUM_PRIMES] = 
	   {2,    3,    5,    7,   11,   13,   17,   19,   23,   29, 
	   31,   37,   41,   43,   47,   53,   59,   61,   67,   71, 
	   73,   79,   83,   89,   97,  101,  103,  107,  109,  113, 
	  127,  131,  137,  139,  149,  151,  157,  163,  167,  173, 
	  179,  181,  191,  193,  197,  199,  211,  223,  227,  229, 
	  233,  239,  241,  251,  257,  263,  269,  271,  277,  281, 
	  283,  293,  307,  311,  313,  317,  331,  337,  347,  349, 
	  353,  359,  367,  373,  379,  383,  389,  397,  401,  409, 
	  419,  421,  431,  433,  439,  443,  449,  457,  461,  463, 
	  467,  479,  487,  491,  499,  503,  509,  521,  523,  541, 
	  547,  557,  563,  569,  571,  577,  587,  593,  599,  601, 
	  607,  613,  617,  619,  631,  641,  643,  647,  653,  659, 
	  661,  673,  677,  683,  691,  701,  709,  719,  727,  733, 
	  739,  743,  751,  757,  761,  769,  773,  787,  797,  809, 
	  811,  821,  823,  827,  829,  839,  853,  857,  859,  863, 
	  877,  881,  883,  887,  907,  911,  919,  929,  937,  941, 
	  947,  953,  967,  971,  977,  983,  991,  997};
#define MAX_PRIME primes[NUM_PRIMES-1]

    int treeIndex, firstNonZeroBit, bitPosition, tmp, mask, remainingFactorMe;
    BranchInfo *searchTree, bestBranch;
    int *primeFactors;
    int status, i, j, maxNumFactors, t, k, n, q, r, testing;
    int numPrimeFactors, factorCount, insertIndex = 0;
    int numPrimeLeft;
    double nthRoot, distance, minDistance = 0.0;
    int mpi_errno;

    /* Check for wacky input values. */
    if ((factorMe <= 0) || (factorMe >= (MAX_PRIME * MAX_PRIME)) || 
        (numFactors <= 0))
        {
	    mpi_errno = MPIR_Err_setmsg( MPI_ERR_INTERN, MPIR_ERR_FACTOR,
					 "MPI_DIMS_CREATE",
	 "Internal MPI error! Invalid data for factorAndcombine", (char *)0 );
	    return mpi_errno;
        }

    /* Check for trivial numFactors case. */
    if (numFactors == 1)
        {
        factors[0] = factorMe;
        status = MPI_SUCCESS;
        return(status);
        }

    /* Initialize output array. */
    for (i=0; i<numFactors; i++)
        {
        factors[i] = 1;
        }

    /* Check for trivial factorMe case. */
    if (factorMe == 1)
        {
        status = MPI_SUCCESS;
        return(status);
        }

    /* Allocate temporary array to store maximum number of prime factors. */
/*    log2() is NOT a standard library function! */
/*    xtmp = log2((double)factorMe); */
/*    maxNumFactors = ((int)xtmp) + 1; */
    tmp = factorMe;
    i = 0;
    while (tmp > 0)
        {
        i++;
        tmp >>= 1;
        }
    maxNumFactors = i + 1;               /* a bit more than log2(factorMe) */
    primeFactors = (int *)CALLOC(maxNumFactors, sizeof(int));
    if (primeFactors == ((int *)NULL))
        {
        status = MPI_ERR_EXHAUSTED;
        return(status);
        }

    /* Find prime factors using "factoring by division" and store in array */
    /* primeFactors. */
    t = 0;
    k = 0;
    n = factorMe;
    while (n != 1)
        {
        testing = 1;
        while (testing == 1)
            {
            q = n / primes[k];
            r = n % primes[k];
            if (r == 0)
                {            /* found a factor, store and go on to the next */
                t++;
                primeFactors[t - 1] = primes[k];
                n = q;
                testing = 0;
                }  /* end of r if */
            else if (q > primes[k])
                {                       /* check the next prime in the list */
                k++;
                }
            else
                {                 /* n is prime, store and terminate search */
                t++;
                primeFactors[t - 1] = n;
                n = 1;
                testing = 0;
                }
            }  /* end of testing while loop */
        }  /* end of n while loop */
    numPrimeFactors = t;

    /* Modify the number of factors if necessary.  Factors emerge from the */
    /* previous algorithm in order MIN --> MAX.  They must be stored in */
    /* array factors in order MAX --> MIN. */
    if (numFactors >= numPrimeFactors)
        {                               /* Re-order factors to MAX --> MIN. */
        for (i=0; i<numPrimeFactors; i++)
            {           /* All factors[i] have already been set to 1 above. */
            factors[i] = primeFactors[(numPrimeFactors - 1) - i];
            }
        }  /* end of ">=" if */
    else
        {
        /* Allocate memory for search tree. */
        searchTree = (BranchInfo *)CALLOC(maxNumFactors, 
          sizeof(BranchInfo));
        if (searchTree == ((BranchInfo *)NULL))
		  {
            FREE(primeFactors);
	    return MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, 
							  "MPI_DIMS_CREATE" );
		  }
        remainingFactorMe = factorMe;
        factorCount = 0;              /* Track # of output factors created. */
        numPrimeLeft = numPrimeFactors;   /* Track # of prime factors used. */

        /* nthRoot is used as a threshold to optimize factor selection. */
        nthRoot = MPIR_guess((double)remainingFactorMe, 
          ((double)(numFactors - factorCount)));

        tmp = 0;
        i = numPrimeLeft - 1;
        while ((i >= 0) && ((numFactors - factorCount) > 1))
            {     /* this depends on MIN --> MAX ordering of primeFactors[] */
            /* If prime factor is >= nthRoot, remove from primeFactors and */
            /* put in factors[]. */
            if (primeFactors[i] >= nthRoot)
                {
                factors[factorCount] = primeFactors[i];
                remainingFactorMe /= primeFactors[i];
                factorCount++;
                if (numFactors > factorCount)
                    {
                    nthRoot = MPIR_guess((double)remainingFactorMe, 
                      ((double)(numFactors - factorCount)));
                    }
                else
                    {
                    nthRoot = 0.0;
                    }
                tmp++;            /* Count number of prime factors removed. */
                }  /* end of primeFactor[] if */
            else
                {
                i = 0;   /* exit while loop (all remaining values are less) */
                }
            i--;
            }  /* end of i while loop */
        numPrimeLeft -= tmp;
        while ((numPrimeLeft > (numFactors - factorCount)) && 
          ((numFactors - factorCount) > 1))
/* $$$ Is it possible to run out of primeFactors[] before all factors[] */
/* $$$ are filled??  I don't think so... */
            {                         /* primeFactors are ordered MIN-->MAX */
            /* Initialize root of search tree. */
            treeIndex = 0;     /* Points to current location in searchTree. */
            searchTree[treeIndex].currentBranch = 1 << (numPrimeLeft - 1);
            searchTree[treeIndex].currentValue = 
              primeFactors[numPrimeLeft - 1];
            if ((searchTree[treeIndex].currentBranch & 3) != 0)
                {
                searchTree[treeIndex].nextBranch = 0;
                }
            else
                {
                searchTree[treeIndex].nextBranch = 
                  searchTree[treeIndex].currentBranch + 
                  (1 << (numPrimeLeft - 3));
                }
            /* Initialize "best" branch found so far. */
            bestBranch.currentBranch = searchTree[treeIndex].currentBranch;
            bestBranch.currentValue = searchTree[treeIndex].currentValue;
            /* Avoid search if current value == nthRoot. */
            if ((double)bestBranch.currentValue == nthRoot)
                {
                searchTree[0].currentBranch = 0;
                }
            else
                {
                /* Initialize squared difference between "best" value and */
                /* threshold. */
                minDistance = nthRoot - (double)bestBranch.currentValue;
                minDistance *= minDistance;
                }  /* end of nthRoot else */

            /* Find product of factors that is closest to nthRoot. */
            while (searchTree[0].currentBranch != 0)
                {
                /* Go to next branch. */
                if ((searchTree[treeIndex].currentBranch & 1) == 1)
                    {               /* at the bottom, ascend to next branch */
                    /* Ascend to next branch. */
                    while (
                      (searchTree[treeIndex].nextBranch == 0) && 
                      (treeIndex > 0))
                        {
                        treeIndex--;
                        }  /* end of 0 while loop */
                    /* Avoid out-of-range treeIndex at top of tree. */
                    if (searchTree[treeIndex].nextBranch == 0)
                        {
                        treeIndex--;
                        }  /* end of 0 if */
                    /* If at the top, shift to next main branch. */
                    if (treeIndex == -1)
                        {
                        searchTree[treeIndex + 1].currentBranch >>= 1;
                        /* Calculate value at new branch if not done. */
                        if (searchTree[treeIndex + 1].currentBranch > 0)
                            {
                            tmp = getFirstBit(
                              searchTree[treeIndex + 1].currentBranch, 
                              &bitPosition);
                            searchTree[treeIndex + 1].currentValue = 
                              primeFactors[bitPosition];
                            }  /* end of "not done" if */
                        }  /* end of (treeIndex == -1) if */
                    else           /* If not at the top, go to next branch. */
                        {
                        searchTree[treeIndex + 1].currentBranch = 
                          searchTree[treeIndex].nextBranch;
                        /* Calculate value at new branch.  tmp should always */
                        /* be positive. */
                        tmp = 
                          getFirstBit(
                          searchTree[treeIndex + 1].currentBranch, 
                          &bitPosition);
                        searchTree[treeIndex + 1].currentValue = 
                          searchTree[treeIndex].currentValue * 
                          primeFactors[bitPosition];
                        /* Point to the next branch if it exists. */
                        if ((searchTree[treeIndex].nextBranch & 1) == 1)
                            {
                            searchTree[treeIndex].nextBranch = 0;
                            }
                        else
                            {
                            /* Shift least significant nonzero bit right one */
                            /* place. */
                            firstNonZeroBit = 
                              getFirstBit(
                              searchTree[treeIndex].nextBranch, 
                                &bitPosition);
                            /* Clear bit. */
                            searchTree[treeIndex].nextBranch &= 
                              ~firstNonZeroBit;
                            /* Shift and add it back. */
                            searchTree[treeIndex].nextBranch += 
                              firstNonZeroBit >> 1;
                            }
                        }  /* end of (treeIndex == -1) else */
                    /* Set up nextBranch for new branch. */
                    treeIndex++;
                    if ((searchTree[treeIndex].currentBranch & 3) != 0)
                        {
                        searchTree[treeIndex].nextBranch = 0;
                        }
                    else
                        {
                        firstNonZeroBit = 
                          getFirstBit(
                          searchTree[treeIndex].currentBranch, 
                            &bitPosition);
                        searchTree[treeIndex].nextBranch = 
                          searchTree[treeIndex].currentBranch + 
                          (firstNonZeroBit >> 2);
                        }
                    }  /* end of "ascend" if */
                else
                    {     /* not at the bottom, keep descending this branch */
                    firstNonZeroBit = 
                      getFirstBit(
                      searchTree[treeIndex].currentBranch, 
                      &bitPosition);
                    firstNonZeroBit >>= 1;
/* $$$ BUGFIX */
                    bitPosition -= 1;
/* $$$ END BUGFIX */
                    searchTree[treeIndex + 1].currentBranch = 
                      searchTree[treeIndex].currentBranch + firstNonZeroBit;
                    searchTree[treeIndex + 1].currentValue = 
                      searchTree[treeIndex].currentValue * 
                      primeFactors[bitPosition];
                    treeIndex++;
                    if ((searchTree[treeIndex].currentBranch & 3) != 0)
                        {
                        searchTree[treeIndex].nextBranch = 0;
                        }
                    else
                        {
                        searchTree[treeIndex].nextBranch = 
                          searchTree[treeIndex].currentBranch + 
                          (firstNonZeroBit >> 2);
                        }
                    }  /* end of "descend" else */
                /* Find difference between current value and threshold. */
                distance = nthRoot - 
                  (double)searchTree[treeIndex].currentValue;
                /* If currentValue > nthRoot then set nextBranch to */
                /* 0 (pruning). */
                if (distance < 0.0)
                    {
                    searchTree[treeIndex].nextBranch = 0;
                    }
                /* Find squared difference between current value and */
                /* threshold. */
                distance *= distance;
                /* Check if current value is better than "best" value. */
                if (distance < minDistance)
                    {
                    minDistance = distance;
                    bestBranch.currentBranch = 
                      searchTree[treeIndex].currentBranch;
                    bestBranch.currentValue = 
                      searchTree[treeIndex].currentValue;
                    /* Terminate search if current value == nthRoot. */
                    if (minDistance == 0.0)
                        searchTree[0].currentBranch = 0;
                    }  /* end of distance if */
                }  /* end of "Find product of factors" while loop */
            /* Remaining factors should factor a new smaller value. */
            remainingFactorMe /= bestBranch.currentValue;

            /* Number of factors combined is number of bits set in */
            /* bestBranch.currentBranch.  Remove these factors from */
            /* primeFactors and decrement numPrimeLeft.  Add new factor */
            /* to factors[] and increment factorCount. */
            mask = 1;
            tmp = 0;              /* count number of prime factors removed. */
            for (bitPosition=0; bitPosition<numPrimeLeft; bitPosition++)
                {
                if ((bestBranch.currentBranch & mask) != 0)
                    {
                    /* Set primeFactors[bitPosition] to 0 for later removal.*/
                    primeFactors[bitPosition] = 0;
                    tmp++;
                    }  /* end of mask if */
                mask <<= 1;
                }  /* end of bitPosition for loop */
            /* Remove prime factors and shrink list of prime factors. */
            i = 0;
            while (tmp > 0)
                {
                if (primeFactors[i] == 0)
                    {
                    for (j=i; j<(numPrimeLeft - 1); j++)
                        {
                        primeFactors[j] = primeFactors[j+1];
                        }
                    tmp--;
                    numPrimeLeft--;
                    i--;             /* recheck in case two consecutive 0's */
                    }
                i++;
                }  /* end of tmp while loop */
            /* Search for the right place to insert the new factor */
            /* (MAX --> MIN). */
            i = 0;
	    insertIndex = factorCount; /* Default value if none found */
            while (i < factorCount)
                {
                if (bestBranch.currentValue > factors[i])
                    {
                    insertIndex = i;
		    break;
                    /* i = factorCount; */
                    }
                i++;  /* This is also needed for if below on "normal" exit. */
                }  /* end of i while loop */

            /* Insert new factor in factor list and shift factor list. */
            for (i=factorCount; i>insertIndex; i--)
                {
                factors[i] = factors[i-1];
                }
            factors[insertIndex] = bestBranch.currentValue;
            factorCount++;

            /* Calculate new nthRoot threshold. */
            if (numFactors > factorCount)
                {
                nthRoot = MPIR_guess((double)remainingFactorMe, 
                  ((double)(numFactors - factorCount)));
                }
            else
                {
                nthRoot = 0.0;
                }
            /* Remove any primeFactors larger than threshold. */
            tmp = 0;
            i = numPrimeLeft;
            while ((i >= 0) && ((numFactors - factorCount) > 1))
                {      /* depends on MIN --> MAX ordering of primeFactors[] */
                /* If prime factor is > nthRoot, remove from */
                /* primeFactors and put in factors[]. */
                if (primeFactors[i] >= nthRoot)
                    {
                    factors[factorCount] = primeFactors[i];
                    remainingFactorMe /= primeFactors[i];
                    factorCount++;
                    if (numFactors > factorCount)
                        {
                        nthRoot = MPIR_guess((double)remainingFactorMe, 
                          ((double)(numFactors - factorCount)));
                        }
                    else
                        {
                        nthRoot = 0.0;
                        }
                    tmp++;             /* Count # of prime factors removed. */
                    }  /* end of primeFactor[] if */
                else
                    {
                    i = 0;                               /* exit while loop */
                    }  /* end of primeFactor[] else */
                i--;
                }  /* end of i while loop */
            numPrimeLeft -= tmp;
            }  /* end of numPrimeLeft while loop */

        /* If only one factor is left, take product of remaining prime */
        /* factors and use them as the last factor!! */
        if (factorCount == (numFactors - 1))
            {
            tmp = primeFactors[0];
            for (j=1; j<numPrimeLeft; j++)
                {
                tmp *= primeFactors[j];
                }  /* end of j for loop */
            numPrimeLeft = 0;
            /* Search for the right place to insert the new factor */
            /* (MAX --> MIN). */
            i = 0;
            while (i < factorCount)
                {
                if (tmp > factors[i])
                    {
                    insertIndex = i;
                    i = factorCount;
                    }
                i++;      /* Also needed for if below on "normal" exit. */
                }  /* end of i while loop */
            if (i == factorCount)
                {
                insertIndex = i;
                }
            /* Insert new factor in factor list and shift factor list. */
            for (i=factorCount; i>insertIndex; i--)
                {
                factors[i] = factors[i-1];
                }
            factors[insertIndex] = tmp;
            factorCount++;
            }  /* end of factorCount if */
        /* Free memory. */
        FREE(searchTree);
        }  /* end of else */

    /* Free memory. */
    FREE(primeFactors);

    status = MPI_SUCCESS;
    return(status);
    }    /* end of function factorAndCombine() */


/*-------------------------------------------------------------------------- 
** MPI_DIMS_CREATE()
** 
** The MPI_DIMS_CREATE() function is described in detail in section 6.5.1 
** "Topology Inquiry Functions" of the MPI draft standard.  I'm NOT 
** repeating all that here...  
** 
** Return Values
** 
** The MPI_DIMS_CREATE() function will only return a negative number if an 
** error occurs.  Other return values indicate successful completion.  
** 
** Author:   Tom Henderson
** 
** Date:     11/24/93
** 
** Vendor-dependent?:  NO
** 
** Functions called by this function:  
**     factorAndCombine()  
** 
**--------------------------------------------------------------------------*/ 

/*@

    MPI_Dims_create - Creates a division of processors in a cartesian grid

Input Parameters:
+ nnodes - number of nodes in a grid (integer) 
- ndims - number of cartesian dimensions (integer) 

In/Out Parameter:   
. dims - integer array of size  'ndims' specifying the number of nodes in each 
dimension  

.N fortran
@*/
int MPI_Dims_create(
	int nnodes, 
	int ndims, 
	int *dims)
{
  int i, *newDims, newNdims;
  int testProduct, freeNodes, stat, ii;
  int mpi_errno = MPI_SUCCESS;
  static char myname[] = "MPI_DIMS_CREATE";

  /* Check for wacky input values. */
#ifndef MPIR_NO_ERROR_CHECKING
  if (nnodes <= 0) mpi_errno = MPI_ERR_ARG;
  if (ndims <= 0) mpi_errno = MPI_ERR_ARG;
    if (mpi_errno)
	return MPIR_ERROR(MPIR_COMM_WORLD, mpi_errno, myname );
#endif

  newNdims = 0;                        /* number of zero values in dims[] */
  for (i=0; i<ndims; i++) {
      if (dims[i]<0) {
	  mpi_errno = MPIR_Err_setmsg( MPI_ERR_DIMS, MPIR_ERR_DIMS_ARRAY, 
				       myname, (char *)0, (char *)0, 
				       i, dims[i] );
	  return MPIR_ERROR(MPIR_COMM_WORLD,mpi_errno,myname );
      }
	if (dims[i]==0)
	  newNdims++;
  }

  /* If all values of dims[] are non-zero, check that the product of */
  /* dims[i] == nnodes... */
    if (newNdims == 0)  {
	  testProduct = 1;
	  for (i=0; i<ndims; i++) {
		testProduct *= dims[i];
	  }
	  if (testProduct != nnodes) {
	      mpi_errno = MPIR_Err_setmsg( MPI_ERR_DIMS, MPIR_ERR_DIMS_SIZE,
					   myname, 
                 "Tensor product size does not match nnodes",
		 "Tensor product size (%d) does not match nnodes (%d)", 
					   testProduct, nnodes );
		return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
	  }
	  else
		return(MPI_SUCCESS);
	}

  /* freeNodes is nnodes divided by each non-zero value of dims[i] */
  freeNodes = nnodes;
  for (i=0; i<ndims; i++) {
	if (dims[i]>0) {
	    if (freeNodes%dims[i] != 0) {
		mpi_errno = MPIR_Err_setmsg( MPI_ERR_DIMS, 
					     MPIR_ERR_DIMS_PARTITION, myname,
			"Can not partition nodes as requested", (char *)0);
		return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
	    }
	    freeNodes /= dims[i];
	}
  }

  /* newDims will contain all dimensions not specified by the user. */
  newDims = (int *)CALLOC(newNdims, sizeof(int));
  if (newDims == ((int *)0)) {
      return MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_EXHAUSTED, myname );
  }

  /* Factor freeNodes into newDims */
  stat = factorAndCombine(freeNodes, newNdims, newDims);
  if (stat != 0) {
	FREE(newDims);
	return MPIR_ERROR( MPIR_COMM_WORLD, stat, myname );
  }
  
  /* Insert newDims into dims */
  for (i=0, ii=0; i<ndims; i++) {
	if (dims[i]==0) {
	  dims[i] = newDims[ii];
	  ii++;
	}
  }

  FREE(newDims);

  return(MPI_SUCCESS);
}    /* end of function MPI_DIMS_CREATE() */





/*
 *  $Id: util_hbt.c,v 1.5 2000/07/03 21:30:25 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

/*

  util_util - height balanced tree source code

*/

#include "mpiimpl.h"
#include "attr.h"
#include "sbcnst2.h"
#define MPIR_SBinit MPID_SBinit
#define MPIR_SBfree MPID_SBfree
#define MPIR_SBalloc MPID_SBalloc
#define MPIR_SBdestroy MPID_SBdestroy

/*===========================================================================*
  Interface for height balanced trees

 int MPIR_HBT_new_tree  ( MPIR_HBT *tree ) 
 int MPIR_HBT_free_tree ( MPIR_HBT tree )
 int MPIR_HBT_free_subtree ( MPIR_HBT_node *node )

 int MPIR_HBT_new_node  ( int keyval, void *value, MPIR_HBT_node **node_out ) 
 int MPIR_HBT_free_node ( MPIR_HBT_node *node )

 int MPIR_HBT_lookup ( MPIR_HBT tree, int keyval, MPIR_HBT_node **node )
 int MPIR_HBT_insert ( MPIR_HBT tree, MPIR_HBT_node *node )
 int MPIR_HBT_delete ( MPIR_HBT tree, int keyval )

 *===========================================================================*/

/*===========================================================================*/
/* The stack size for the deletion algorithm.                                */
/*===========================================================================*/
#define MPIR_STACK_SIZE 100

/*===========================================================================*/
/* Each node has flag indicating whether it is balanced or not.              */
/* NOTE: comparisons are done using ">" and "<" so sign is important         */
/*===========================================================================*/
#define MPIR_BALANCED 0
#define MPIR_UNBALANCED_LEFT -1
#define MPIR_UNBALANCED_RIGHT 1

/*===========================================================================*/
/* Some macros for easy access to fields in the HBT_obj structure            */
/*===========================================================================*/
#define LEFT(e) (e) -> left
#define RIGHT(e) (e) -> right
#define B(e) (e) -> balance

/*===========================================================================*/
/* Macro to compare keyval's with                                            */
/*===========================================================================*/
#define MPIR_COMPARE(a,b)  ((a)-(b))

/* Temporary */
#ifndef MPIR_TRUE
#define MPIR_TRUE  1
#define MPIR_FALSE 0
#endif

/*===========================================================================*/

void *MPIR_hbts;   /* sbcnst height balanced tree roots for cacheing */
void *MPIR_hbt_els;/* sbcnst height balanced tree nodes for cacheing */

/*+
  For efficient allocation of tree elements, this package uses the fast fixed
  block allocators.  This routine initializes the allocators.
+*/
void MPIR_HBT_Init()
{
    MPIR_hbts       = MPIR_SBinit( sizeof( struct _MPIR_HBT ), 5, 5 );
    MPIR_hbt_els    = MPIR_SBinit( sizeof( MPIR_HBT_node ), 20, 20);
}

void MPIR_HBT_Free()
{
    MPIR_SBdestroy( MPIR_hbts );
    MPIR_SBdestroy( MPIR_hbt_els );
}

/*+

MPIR_HBT_new_tree -

+*/
int MPIR_HBT_new_tree ( 
	MPIR_HBT *tree_out)
{
  MPIR_HBT new_tree;

  TR_PUSH("MPIR_HBT_new_tree");
  new_tree = (*tree_out) = (MPIR_HBT) MPIR_SBalloc ( MPIR_hbts );
  if (!new_tree)
      return MPI_ERR_EXHAUSTED;
  MPIR_SET_COOKIE(new_tree,MPIR_HBT_COOKIE);
  new_tree->root = (MPIR_HBT_node *)0;
  new_tree->height = 0;
  TR_POP;
  return (MPI_SUCCESS);
}

/*+

MPIR_HBT_new_node -

+*/
int MPIR_HBT_new_node ( 
	MPIR_Attr_key *keyval,
	void *value,
	MPIR_HBT_node **node_out)
{
  MPIR_HBT_node *new_node;

  TR_PUSH("MPIR_HBT_new_node");
  new_node = (*node_out) = (MPIR_HBT_node *) MPIR_SBalloc (MPIR_hbt_els);
  if (!new_node)
      return MPI_ERR_EXHAUSTED;
  MPIR_SET_COOKIE(new_node,MPIR_HBT_NODE_COOKIE);
  new_node->keyval       = keyval;
  new_node->value        = value;
  new_node->balance      = MPIR_BALANCED;
  new_node->left         = new_node->right = (MPIR_HBT_node *)0;
  TR_POP;
  return (MPI_SUCCESS);
}

/*+

MPIR_HBT_free_node -

+*/
int MPIR_HBT_free_node (
	MPIR_HBT_node *node)
{
    TR_PUSH("MPIR_HBT_free_node");
    if (node != (MPIR_HBT_node *)0) {
	/* Free memory used by node */
	MPIR_SBfree( MPIR_hbt_els, node );
    }
    TR_POP;
    return (MPI_SUCCESS);
}

/*+

MPIR_HBT_free_subtree -

+*/
int MPIR_HBT_free_subtree ( 
	MPIR_HBT_node *subtree)
{
    TR_PUSH("MPIR_HBT_free_subtree");

    if(subtree != (MPIR_HBT_node *)0) {
	(void) MPIR_HBT_free_subtree ( subtree -> left );
	(void) MPIR_HBT_free_subtree ( subtree -> right );
	(void) MPIR_HBT_free_node ( subtree );
    }
    TR_POP;
    return (MPI_SUCCESS);
}

/*+

MPIR_HBT_free_tree -

+*/
int MPIR_HBT_free_tree ( 
	MPIR_HBT tree)
{
    TR_PUSH("MPIR_HBT_free_tree");
    if ( tree != (MPIR_HBT)0 ) {
	if ( tree->root != (MPIR_HBT_node *)0 )
	    (void) MPIR_HBT_free_subtree ( tree->root );
	MPIR_SBfree ( MPIR_hbts, tree );
    }
    TR_POP;
    return (MPI_SUCCESS);
}

/*+

MPIR_HBT_lookup( -

+*/
int MPIR_HBT_lookup( 
	MPIR_HBT      tree,
	int           keyval,
	MPIR_HBT_node **node_out)
{
  int test;
  MPIR_HBT_node *temp = tree->root;

  TR_PUSH("MPIR_HBT_lookup");
  while(temp)
    if ( (test = MPIR_COMPARE( keyval, temp->keyval->self )) < 0 )
      temp = LEFT(temp);
	else if ( test > 0)
      temp = RIGHT(temp);
	else {
      (*node_out) = temp;
      TR_POP;
      return (MPI_SUCCESS);
    }
  (*node_out) = (MPIR_HBT_node *)0;
  TR_POP;
  return (MPI_SUCCESS);
}

/*+

MPIR_HBT_insert( -

+*/
int MPIR_HBT_insert( 
	MPIR_HBT      tree,
	MPIR_HBT_node *node)
{
  MPIR_HBT_node *temp, *inserted=0, *rebalance_son, *rebalance, *rebalance_father;
  int done = 0;
  int test, test_rebalance, rebalance_B;

  TR_PUSH("MPIR_HBT_insert");

  /* If tree is currently empty then just add new node. */
  if ( tree->root == (MPIR_HBT_node *)0 ) {
    tree->root = node;
    tree->height = 1;
    TR_POP;
    return (MPI_SUCCESS);
  }
  
  /* Initialize start location for balance adj and rebalancing efforts*/
  rebalance_father = (MPIR_HBT_node *)tree;
  rebalance = temp = tree->root;
  
  /* Find the place where the new node should go. */
  while(!done) {
    if( (test = MPIR_COMPARE( node->keyval->self, temp->keyval->self )) == 0) {
	  /* The key already exists in the tree can't add. */
	TR_POP;
	return (MPI_SUCCESS);
    }
    else if (test < 0) {
	  /* Go left. */
	  if( (inserted = LEFT(temp)) == (MPIR_HBT_node *)0 ) {
	      /* Found place to insert the new node */
	      inserted = LEFT(temp) = node;
	      done = 1;
	  }
	  else {
	      /* If not balanced at this point the move the starting location*/
	      /* for rebalancing effort here. */
	      if ( B(inserted) != MPIR_BALANCED ) {
		  rebalance_father = temp;
		  rebalance = inserted;
	      }
	      temp = inserted;
	  }
    }
    else if ( test > 0) {
	  /* Go left. */
	  if( (inserted = RIGHT(temp) ) == NULL) {
        /* Found place to insert the new node */
        inserted = RIGHT(temp) = node;
        done = 1;
	  }
	  else {
        /* If not balanced at this point the move the starting location*/
        /* for rebalancing effort here. */
        if ( B(inserted) != MPIR_BALANCED ) {
		  rebalance_father = temp;
		  rebalance = inserted;
        }
        temp = inserted;
	  }
    }
  }
    
  /* Adjust the balance factors along the path just traversed.  Only need to */
  /* do this on part of the path. */
  if( (test_rebalance = MPIR_COMPARE(node->keyval->self, 
				     rebalance->keyval->self)) < 0 )
    rebalance_son = temp = LEFT(rebalance);
  else
    rebalance_son = temp = RIGHT(rebalance);

  while( temp != inserted) {
    if ( (test = MPIR_COMPARE( node->keyval->self, temp->keyval->self )) < 0 ) {
	  B(temp) = MPIR_UNBALANCED_LEFT;
	  temp = LEFT(temp);
	}
    else if (test > 0) {
	  B(temp) = MPIR_UNBALANCED_RIGHT;
	  temp = RIGHT(temp);
	}
  }

  /* Rebalance the tree.  There is only one point where rebalancing might    */
  /* be needed. */
  rebalance_B = (test_rebalance<0)?MPIR_UNBALANCED_LEFT:MPIR_UNBALANCED_RIGHT;

  if( B(rebalance) == MPIR_BALANCED) {
    /* Tree was balanced, adding new node simply unbalances it.            */
    B(rebalance) = rebalance_B;
    tree -> height++;
  }
  else if ( B(rebalance) == -rebalance_B )
    /* Tree was unbalanced towards the opposite side of insertion so       */
    /* with new node it is balanced. */
    B(rebalance) = MPIR_BALANCED;
  else {
    /* Tree needs rotated. */
    /* See Knuth or Reingold for picture of the rotations much easier and  */
    /* clearer than any word description. */
    if (B(rebalance_son) == rebalance_B) {
	  /* Single rotation. */
	  temp = rebalance_son;
	  if ( rebalance_B == MPIR_UNBALANCED_LEFT) {
        LEFT(rebalance) = RIGHT(rebalance_son);
        RIGHT(rebalance_son) = rebalance;
	  }
	  else {
        RIGHT(rebalance) = LEFT(rebalance_son);
        LEFT(rebalance_son) = rebalance;
	  }
	  B(rebalance) = ( B(rebalance_son) = MPIR_BALANCED );
    }
    else if ( B(rebalance_son) == -rebalance_B) {
	  /* double rotation */
	  if ( rebalance_B == MPIR_UNBALANCED_LEFT) {
        temp = RIGHT(rebalance_son);
        RIGHT(rebalance_son) = LEFT(temp);
        LEFT(temp) = rebalance_son;
        LEFT(rebalance) = RIGHT(temp);
        RIGHT(temp) = rebalance;
	  }
	  else {
        temp = LEFT(rebalance_son);
        LEFT(rebalance_son) = RIGHT(temp);
        RIGHT(temp) = rebalance_son;
        RIGHT(rebalance) = LEFT(temp);
        LEFT(temp) = rebalance;
	  }
	  
	  if ( B(temp) == rebalance_B ) {
        B(rebalance) = -rebalance_B;
        B(rebalance_son) = MPIR_BALANCED;
	  }
	  else if ( B(temp) == 0 )
        B(rebalance) = (B(rebalance_son) = MPIR_BALANCED);
	  else {
        B(rebalance) = MPIR_BALANCED;
        B(rebalance_son) = rebalance_B;
	  }
      B(temp) = MPIR_BALANCED;
    }

    /* Need to adjust what the father of the this subtree points at since  */
    /* we rotated. */
    if( rebalance_father == (MPIR_HBT_node *)tree )
	  tree -> root = temp;
    else {
	  if ( rebalance == RIGHT(rebalance_father) )
        RIGHT(rebalance_father) = temp;
	  else
        LEFT(rebalance_father) = temp;
    }
  }
  TR_POP;
  return (MPI_SUCCESS);
}

/*+

MPIR_HBT_delete -

+*/
int MPIR_HBT_delete(
	MPIR_HBT      tree,
	int           keyval,
	MPIR_HBT_node **node_out)
{
  /* The stack keeps elements from root to the node to be deleted.         */
  /* element_stack has pointers to the nodes.  dir_stack has the direction */
  /* taken down the path.                                                  */
  int size = 0;                    
  MPIR_HBT_node  *element_stack[MPIR_STACK_SIZE];
  short       dir_stack[MPIR_STACK_SIZE];
  short       father_dir;
  MPIR_HBT_node  *del, *successor, *father, *current,*son, *grandson, *temp;
  int test, done,dir, top_of_stack;
  
  TR_PUSH("MPIR_HBT_delete");
  /* Find the node to be deleted.  Keep track of path on the stack.        */

  /* put the tree as indicator on the top of the stack.  This simplifies   */
  /* logic a little. */
  element_stack[size++] = (MPIR_HBT_node *) tree;
  element_stack[size] = (del = tree -> root);

  while( del && (test = MPIR_COMPARE(keyval, del->keyval->self)) ) {
	if(test < 0) {
      dir_stack[size] = MPIR_UNBALANCED_LEFT;
      del = LEFT(del);
	}
	else {
      dir_stack[size] = MPIR_UNBALANCED_RIGHT;
      del = RIGHT(del);
	}
	element_stack[++size] = del;
  }
    
  /* Node was not found so can't delete it. */
  if (del == NULL) {
      TR_POP;
      return (MPI_SUCCESS);
  }
  
  /* Set father to be the parent of the node to be deleted */
  father = element_stack[size-1];
  father_dir = dir_stack[size-1];
  
  /* Do the actual deletion of the node from the tree. */
  /* Special processing is done if del is at the root of the tree. */
  if( RIGHT(del) == NULL ) {
	/* RIGHT of del is null so we can just delete the node */
	/* set dir to indicate where the NULL child is */
	dir_stack[size] = MPIR_UNBALANCED_RIGHT;
    
	if(father == (MPIR_HBT_node *)tree) {
      tree -> root = LEFT(del);
      tree -> height--;
	}
	else
      if(father_dir < 0)
		LEFT(father) = LEFT(del);
      else
		RIGHT(father) = LEFT(del);
  }
  else if( LEFT(del) == NULL ) {
	/* LEFT of del is null so we can just delete the node                */
	/* set dir to indicate where the NULL child is */
	dir_stack[size] = MPIR_UNBALANCED_LEFT;
	
	if(father == (MPIR_HBT_node *)tree) {
      tree -> root = RIGHT(del);
      tree -> height--;
	}
	else
      if(father_dir < 0)
		LEFT(father) = RIGHT(del);
      else
		RIGHT(father) = RIGHT(del);
  }
  else {
	/* The "trick" employed here is finding the successor to del with a  */
	/* left link that is NULL.  This successor node is then swapped with */
	/* the node that we want to delete.  Thus the number of cases for    */
	/* actual deletion are small.  The tree is out of order (del has     */
	/* had been placed behind successor) but this does not matter        */
	/* since the tree is not accessed during deletion and the del        */
	/* node will be deleted anyway.  The swapping process just moves     */
	/* successor to del position; del is not reinserted since it is to be*/
	/* removed.                                                          */
	temp = RIGHT(del);
	if(LEFT(temp) == NULL) {
      /* This is a special case when the successor is the son of del.  */
      /* Need to fix the stack since del and successor have swapped.   */
      dir_stack[size] = MPIR_UNBALANCED_RIGHT;
      element_stack[size++] = temp; 
      
      /* Here is the swap of del and successor.                        */
      LEFT(temp) = LEFT(del);
      if(father == (MPIR_HBT_node *)tree)
		tree -> root = temp;
      else
		if(father_dir < 0)
          LEFT(father) = temp;
		else
          RIGHT(father) = temp;

      /* successor will be rebalanced but it would have had same       */
      /* balance as del with del in successors place                   */
      B(temp) = B(del);
	}
	else {
      /* Successor is not the son of del so a search must be done.     */
      /* Need to fix the stack since del and successor have swapped.   */
      dir_stack[size] = MPIR_UNBALANCED_RIGHT;
      element_stack[size++] = temp; 
      
      /* Find the first successor to del that has no left subtree.     */
      successor = LEFT(temp);
      while((LEFT(successor) != NULL)) {
		dir_stack[size] = MPIR_UNBALANCED_LEFT;
		element_stack[size++] = successor;
		successor = LEFT(successor);
      };

      /* Here is the swap of del and successor.                        */
      LEFT(successor) = LEFT(del);
      LEFT(element_stack[size -1]) = RIGHT(successor);
      RIGHT(successor) = RIGHT(del);
      if(father == (MPIR_HBT_node *)tree)
		tree -> root = successor;
      else
		if(father_dir < 0)
          LEFT(father) = successor;
		else
          RIGHT(father) = successor;
      
      /* successor will be rebalanced but it would have had same       */
      /* balance as del with del in successors place                   */
      B(successor) = B(del);
	}
  }
     
  /* Rebalance the tree.  Search up the stack that was kept and rebalance  */
  /* at each node if needed.  The search can be terminated if the subtree  */
  /* height has not changed; the balance of higher noded could not have    */
  /* changed.                                                              */
  done = 0;
  
  /* NOTE that the element 0 is the tree HBT so we don't visit it */
  for(top_of_stack = size-1; (top_of_stack > 0) && (!done); 
      top_of_stack--) {
	current = element_stack[top_of_stack];
	dir = dir_stack[top_of_stack];
	
	if ( B(current) == MPIR_BALANCED ) {
      /* The subtree was balanced at this point.                   */
      /* Unbalance it since a node was deleted.  Since the height  */
      /* of this subtree was not changed we are done               */
      if(dir == MPIR_UNBALANCED_LEFT)
		B(current) = MPIR_UNBALANCED_RIGHT;
      else
		B(current) = MPIR_UNBALANCED_LEFT;
      
      done = 1;
	}	    
	else if ( B(current) == dir) {
      /* The subtree was unbalanced toward the side the deletion   */
      /* occurred on so the new subtree is balanced but it has     */
      /* height one less than before so the rebalencing must       */
      /* continue                                                  */
      B(current) = MPIR_BALANCED;
      
      if(top_of_stack == 1)
		tree -> height--;
	}
	else {
      /* The del node was on the unbalanced side so the subtree    */
      /* is unbalanced by two.  Need to do a rotation.             */
	    
      /* The rotation that needs to be done can be determined from */
      /* the son of del.  Again refering to Knuth or Reingold would*/
      /* be more valuable than any written description that I could*/
      /* write.  One day, perhaps, we can include pictures in      */
      /* in comments.                                              */
      if ( dir == MPIR_UNBALANCED_LEFT )
		son = RIGHT(current);
      else
		son = LEFT(current);
      
      if ( B(son) == MPIR_BALANCED ) {
		/* Son was balanced do a single rotation.                */
		/* Since the subtree at father has not changed in        */
		/* height we are done.                                   */
		if(dir == MPIR_UNBALANCED_LEFT) {
          RIGHT(current) = LEFT(son);
          LEFT(son) = current;
		}
		else {
          LEFT(current) = RIGHT(son);
          RIGHT(son) = current;
		}
		
		B(son) = dir;
		done = 1;
      }
      else if (B(son) == -dir ) {
		/* son is balanced the opposite direction we             */
		/* took at current.  Need to reblance and continue       */
		/* since the tree is one shorter than before.            */
		
		if( dir == MPIR_UNBALANCED_LEFT) {
          RIGHT(current) = LEFT(son);
          LEFT(son) = current;
		}
		else {
          LEFT(current) = RIGHT(son);
          RIGHT(son) = current;
		}
		
		/* current and son are balanced now */
		B(current) = ( B(son) = MPIR_BALANCED);
		
		if(top_of_stack == 1)
          tree -> height--;
      }
      else {
		/* son is balanced the same direction we took at current */
		/* Need to do a double rotation and continue.            */
		if(dir == MPIR_UNBALANCED_LEFT) {
          grandson = LEFT(son);
          RIGHT(current) = LEFT(grandson);
          LEFT(son) = RIGHT(grandson);
          LEFT(grandson) = current;
          RIGHT(grandson) = son;
		}
		else {
          grandson = RIGHT(son);
          LEFT(current) = RIGHT(grandson);
          RIGHT(son) = LEFT(grandson);
          RIGHT(grandson) = current;
          LEFT(grandson) = son;
		}
		
		/* adjust the balance factors */
		if( B(grandson) == MPIR_BALANCED)
          B(son) = (B(current) = MPIR_BALANCED);
		else if( B(grandson) == dir) {
          B(son) = -dir;
          B(current) = MPIR_BALANCED;
		}
		else {
          B(son) = MPIR_BALANCED;
          B(current) = dir;
		}
		
		if(top_of_stack == 1)
          tree -> height--;
		
		/* Double rotation puts grandson at root of subtree.     */
		son = grandson;
      }
	    
      /* Since we rotated the subtree has a new root; point father */
      /* of del at this new root.                                  */
      if (( father = element_stack[top_of_stack - 1]) ==
          (MPIR_HBT_node *)tree) {
		tree -> root = son;
      }
      else {
		if ( dir_stack[top_of_stack -1] == MPIR_UNBALANCED_LEFT)
          LEFT(father) = son;
		else
          RIGHT(father) = son;
      }
	} 
  }
    
  /* Delete the node */
  (*node_out) = del;
  TR_POP;
  return (MPI_SUCCESS);
}


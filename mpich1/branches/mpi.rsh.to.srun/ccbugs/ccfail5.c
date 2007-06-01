struct node {
  int type;
  struct node *next;
};

struct main {
  struct node * head;
  int tokens;
} global;


static
void buggy_func(void)
{
  struct node * s1;

  while (global.head != 0 && global.tokens > 0)
    {
      if (global.head->type != 2)
        {
           printf( "BUG: 2 = %d!!!\n", global.head->type);
	   printf( "OK: 2 = %d\n", global.head->type);
	   abort();
         }
      s1 = global.head;

      
      switch(s1->type) 
        { 
	default:
	  printf("oops, shoudl not be there\n");
	  exit(1);
	  
	  break;
	  
	case 2:
	  global.head = s1->next;

	  s1->type = 3;
          noop(s1);

	  if (global.head == 0)
	    noop();
	  
	  break;
        }
    }
}

int main()
{

  struct node s1,s2;
  global.head = &s1;
  global.tokens = 2;
  s1.type = 2;
  s1.next = &s2;
  s2.type = 2;
  s2.next = 0;
  buggy_func();
  printf("everything OK\n");
  return 0;
}


int noop()
{
}

/*
 * First KLEE tutorial: testing a small function
 */

#include <stdio.h>

#define  MAX_SIZE    65535
int int_overflow(unsigned int x) {
  
  unsigned int size;
  unsigned int num = 100;
  char * buf ;

//  if(x>100 ||  x<0) return -1;

  size = x * num + 7;
  
//  if(x>100 ||  x<0) return -1;
 
  if(size < 7 )
      return 0;
  if(size > MAX_SIZE)
      return -1;

  if(x <= (MAX_SIZE-7) /num )
    printf("ok\n");
  else
    return -1; // printf("overflow\n");
     

 // if(x>100 ||  x<0) return -1;
 // buf = (char*) malloc(size);  
  printf("malloc size = 0x%x", size); 
  return 0;

}

int main() {
  int a;
  klee_make_symbolic(&a, sizeof(a), "a");

  return int_overflow(a);
} 

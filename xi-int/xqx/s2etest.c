#include <stdio.h>
#include <string.h>
//#include "s2e.h"

int mtest(unsigned int nbuf)
{
	
//  s2e_rawmon_loadmodule("myprogram", 0x100000, 100000);	
  char str[3] ,  buf[5];
  // printf("Enter two characters: ");
  // if(!fgets(str, sizeof(str), stdin))
  //   return 1;

//  unsigned int nbuf;

//  s2e_enable_forking();
//  s2e_make_symbolic(str, 2, "str");
//  s2e_make_symbolic(&nbuf, 4, "intbuf");
//  s2e_print_expression("str_addr", (int)str);
  
   if(nbuf == 0){
      cprintf("n = 0\n");
    }
   else{
	if(nbuf == 11){
          cprintf("n*2 <= 1\n");
        }
	//else if(nbuf > 0x7fffffff) {
	//	return 0;
	//}
      else if(nbuf*2 <= 2){
          cprintf("n*2 <= 4\n");
        }
      else{
          cprintf("n*2 >= 1\n");
       }
/*      if(nbuf*4 <= 1){
          cprintf("n*4 <= 1\n");
        }
      else{
          cprintf("n*4 >= 1\n");
       }
      if(nbuf + 10 <= 10){
          cprintf("n + 10 <= 10\n");
       }
      else{
          cprintf("n + 10 >= 10\n");
       }
 */        
    }
/*
  if(str[0] == 0 || str[1] == 0) {
    cprintf("Not enough characters\n");

  } else {
    if(str[0] >= 'a' && str[0] <= 'z')
      cprintf("First char is lowercase\n");
    else
      cprintf("First char is not lowercase\n");

    if(str[0] >= '0' && str[0] <= '9')
      cprintf("First char is a digit\n");
    else
      cprintf("First char is not a digit\n");

    if(str[0] == str[1])
      cprintf("First and second chars are the same\n");
    else
      cprintf("First and second chars are not the same\n");
  }
  */
  //s2e_print_expression("str", 0);

 // s2e_disable_forking();
  

 // s2e_get_example(&nbuf, 4);
  memset(buf,0,sizeof(buf));
  strcpy(buf,&nbuf);
  //cprintf(" %04x \n", nbuf);
 // cprintf("\n\n---------- %02x %02x %02x %02x----------\n\n", (unsigned char)(&nbuf)[0], (unsigned char)(&nbuf)[1],
  //       (unsigned char)(&nbuf)[2], (unsigned char)(&nbuf)[3]);
 // s2e_warning(buf);
 // s2e_kill_state(0, "program terminated");
  
  return 0;
}

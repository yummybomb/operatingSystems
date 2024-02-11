#include <stdio.h>
#include <stdlib.h>
#include<sys/types.h>
#include<unistd.h>
int main(int argc, char **argv) {int pid;int i;
  
  
    for (i=0;i<=1;i++)
      {pid=fork();
if (pid==0)
  {printf("hello world \n");
    
    
  }
else {
  //printf("world hello \n");
}
      }
    printf("All descendants  created  \n");
    
}

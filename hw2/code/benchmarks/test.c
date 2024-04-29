#include <stdio.h>
 #include <sys/types.h>
 #include <unistd.h> 
#include <sys/wait.h>
 int main()
 {   
int status;    
int n =10;    
pid_t pid = vfork(); //creating the child process    
if (pid == 0)         
 //if this is a chile process    
 {        
printf("Child process started  %d \n",n);    
}    
else//parent process execution    
{   
wait(&status);        
printf("Now i am coming back to parent process\n");    
}    
printf("value of n: %d \n",n); //sample printing to    
return 0;
 }

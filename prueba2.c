#include<stdio.h>
#include <stdlib.h>
int main(){
    
    puts("X-Header:true\r\n.hola\r\n.\r\n");
    fflush(stdout);
	while(1){
	int c = getc(stdin);
    
	if(c < 0) exit(1);
	}
}

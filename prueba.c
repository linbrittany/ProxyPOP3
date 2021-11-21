#include<stdio.h>
#include <stdlib.h>
int main(){
	while(1){
	int c = getc(stdin);
	if(c < 0) exit(1);
	if(c!='a') putc(c,stdout);
	fflush(stdout);
	}
}

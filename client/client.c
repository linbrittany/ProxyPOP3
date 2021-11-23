// udp client driver program
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>

#define PORT 9090
#define MAXLINE 1000

// Driver code
int main()
{
	char buffer[1000] = {0};
	char message[1000] = {0};
	int sockfd;
	struct sockaddr_in servaddr;
	
	// clear servaddr
	// bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	servaddr.sin_port = htons(PORT);
	servaddr.sin_family = AF_INET;
	
	// create datagram socket
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	
	// connect to server
	if(connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
	{
		printf("\n Error : Connect Failed \n");
		exit(0);
	}

	// request to send datagram
	// no need to specify server address in sendto
	// connect stores the peers IP and port
    int n ;
    if ( (n = read(STDIN_FILENO,message,MAXLINE)) > 0) {
        // const char *exit = "exit";
        // if ( strcmp(message,exit) == 0 ){
        //     break;
        // }
        sendto(sockfd, message, n, 0, (struct sockaddr*)NULL, sizeof(servaddr));
	
	    // waiting for response
	    int received = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr*)NULL, NULL);
	    printf("%s",buffer);
		memset(buffer,0,received);
		memset(message,0,n);
    }
	// close the descriptor
	close(sockfd);
}


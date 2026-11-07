#include<stdio.h>
#include<stdlib.h>
#include<sys/socket.h>


int main(void){

	// socket(domain, type, protocol)  -> renvoie int (-1 si failure)
	int server = socket(AF_INET, SOCK_STREAM, 0);
	if (server < 0){
		perror("socket");
		exit(1);
	}

	// renvoie 3 (0-2 prix par in/out/error)
	printf("socket fd %d\n",server);
	return 0;
}

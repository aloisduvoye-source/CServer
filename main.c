#include<stdio.h>
#include<stdlib.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<string.h>


int main(void){

	// socket(domain, type, protocol)  -> renvoie int (-1 si failure)
	int server = socket(AF_INET, SOCK_STREAM, 0);
	if (server < 0){
		perror("socket");
		exit(1);
	}

	struct sockaddr_in adress;
	memset(&adress,0,sizeof(adress));
	adress.sin_family = AF_INET;
	adress.sin_addr.s_addr = INADDR_ANY;
	adress.sin_port =  htons(8080);

	if (bind(server, (struct sockaddr *)&adress, sizeof(adress)) < 0){
		perror("bind");
		exit(1);
	}

	// renvoie 3 (0-2 prix par in/out/error)
	printf("socket fd %d\n",server);
	return 0;
}

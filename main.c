#include<stdio.h>
#include<stdlib.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<string.h>
#include<unistd.h>


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

	int yes = 1;
	setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

	if (bind(server, (struct sockaddr *)&adress, sizeof(adress)) < 0){
		perror("bind");
		exit(1);
	}

	if (listen(server,10) < 0){
		perror("listen");
		exit(1);
	}

	printf("listening on 8080\n");
	while (1) {
		int client = accept(server, NULL, NULL);
		if (client<0){
			perror("client");
			exit(1);
		}
		printf("client fd %d\n", client);

		char request[4096];
		ssize_t n = read(client, request, sizeof(request)-1);
		if (n<=0){
			close(client);
			continue;
		}

		request[n] = '\0';

		 char method[8], path[256];
		 if (sscanf(request, "%7s %255s", method, path) != 2){
			close (client);
			continue;
		 }

		printf("%s %s\n", method, path);

		char *response=
			"HTTP/1.0 200 OK\r\n"
			"Content-Type: text/html\r\n"
			"\r\n"
			"<h1>Hello World<h1>\n";
		write(client, response, strlen(response));
		close(client);

	}

	return 0;
}

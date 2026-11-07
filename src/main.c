#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<signal.h>
#include<sys/socket.h>
#include<sys/time.h>
#include<netinet/in.h>
#include"http.h"
#include"router.h"

static void handle_client(int client){

	// si le client n'envoie rien pendant 5 s, read() renvoie -1
	struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
	setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	Request req;
	int r = http_parse(client, &req);
	if (r == -1){
		return;
	}
	if (r == -2){
		const char *body = "<h1>400 Bad Request</h1>\n";
		send_response(client, 400, "text/html", body, strlen(body));
		return;
	}

	printf("[pid %d] %s %s%s%s\n", getpid(), req.method, req.path,
		req.query[0] ? "?" : "", req.query);
	route(client, &req);
}


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

	signal(SIGCHLD, SIG_IGN);

	printf("listening on 8080\n");

	while (1) {
		int client = accept(server, NULL, NULL);
		if (client<0){
			perror("accept");
			continue;
		}

		fflush(stdout);
		pid_t pid = fork();
		if (pid < 0){
			perror("fork");
			close(client);
			continue;
		}

		if (pid == 0){
			close(server);
			handle_client(client);
			close(client);
			exit(0);
		}

		close(client);
	}

	return 0;
}

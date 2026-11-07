#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<signal.h>
#include<sys/socket.h>
#include<sys/time.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include"http.h"
#include"router.h"
#include"db.h"

static void handle_client(int client, const char *ip){

	// si le client n'envoie rien pendant 5 s, read() renvoie -1
	struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
	setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	Request req;
	int r = http_parse(client, &req);
	if (r == 0){
		snprintf(req.ip, sizeof(req.ip), "%s", ip);
		printf("[pid %d] %s %s %s%s%s (%zu octets)\n", getpid(), req.ip, req.method, req.path,
			req.query[0] ? "?" : "", req.query, req.body_len);
		route(client, &req);
	}
	else if (r > 0){
		send_error(client, r);
	}

	http_free(&req);
}


int main(void){

	// socket(domain, type, protocol)  -> renvoie int (-1 si failure)
	int server = socket(AF_INET, SOCK_STREAM, 0);
	if (server < 0){
		perror("socket");
		exit(1);
	}

	// CSERVER_PORT permet aux tests de tourner à côté du serveur de dev
	const char *port_env = getenv("CSERVER_PORT");
	int port = port_env != NULL ? atoi(port_env) : 8080;
	if (port <= 0 || port > 65535){
		fprintf(stderr, "port invalide : %s\n", port_env);
		exit(1);
	}

	struct sockaddr_in adress;
	memset(&adress,0,sizeof(adress));
	adress.sin_family = AF_INET;
	adress.sin_addr.s_addr = INADDR_ANY;
	adress.sin_port =  htons(port);

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

	if (db_init() < 0){
		exit(1);
	}

	signal(SIGCHLD, SIG_IGN);

	printf("listening on %d\n", port);

	while (1) {
		// accept() remplit `peer` avec l'adresse du client
		struct sockaddr_in peer;
		socklen_t peer_len = sizeof(peer);
		int client = accept(server, (struct sockaddr *)&peer, &peer_len);
		if (client<0){
			perror("accept");
			continue;
		}

		char ip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));

		fflush(stdout);
		pid_t pid = fork();
		if (pid < 0){
			perror("fork");
			close(client);
			continue;
		}

		if (pid == 0){
			close(server);
			handle_client(client, ip);
			close(client);
			exit(0);
		}

		close(client);
	}

	return 0;
}

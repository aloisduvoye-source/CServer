#include<stdio.h>
#include<stdlib.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<string.h>
#include<unistd.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<signal.h>
#include<sys/time.h>

char* content_type(char* filename){
	char *dot = strrchr(filename, '.');
	if (dot == NULL){return "text/plain";}
	if (strcmp(dot, ".html") == 0){return "text/html";}
	if (strcmp(dot, ".css") == 0){return "text/css";}
	if (strcmp(dot, ".png") == 0){return "image/png";}
	if (strcmp(dot, ".jpg") == 0){return "image/jpeg";}
	if (strcmp(dot, ".webp") == 0){return "image/webp";}
	if (strcmp(dot, ".js") == 0){return "text/javascript";}
	if (strcmp(dot, ".svg") == 0){return "image/svg+xml";}
	if (strcmp(dot, ".ico") == 0){return "image/x-icon";}

	return "text/plain";
}

void send_404(int client){
	char *response=
		"HTTP/1.0 404 not found\r\n"
		"Content-Type: text/html\r\n"
		"\r\n"
		"<h1>404 dumbass</h1>\n";
	write(client, response, strlen(response));
}

void handle_client(int client){

	// si le client n'envoie rien pendant 5 s, read() renvoie -1
	struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
	setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	char request[4096];
	ssize_t n = read(client, request, sizeof(request)-1);
	if (n<=0){
		return;
	}

	request[n] = '\0';

	char method[8], path[256];
	if (sscanf(request, "%7s %255s", method, path) != 2){
		return;
	}

	printf("[pid %d] %s %s\n", getpid(), method, path);

	if (strstr(path, ".." )!= NULL){
		send_404(client);
		return;
	}

	char filename[512];
	snprintf(
			filename,
			sizeof(filename),
			"www%s",
			strcmp(path, "/") == 0 ? "/index.html" : path
	);

	struct stat info;
	int file = -1;

	if (stat(filename,&info) < 0 || !S_ISREG(info.st_mode) || (file  = open(filename, O_RDONLY)) < 0) {
		send_404(client);
		return;
	}

	char header[256];
	int lenght = snprintf(header, sizeof(header),
		"HTTP/1.0 200 OK\r\n"
		"Content-Type: %s\r\n"
		"Content-Length: %ld\r\n"
		"\r\n",
		content_type(filename), (long)info.st_size
	);
	write(client, header, lenght);

	char buffer[4096];
	ssize_t count;
	while ((count = read(file, buffer, sizeof(buffer))) > 0){
		write(client, buffer, count);
	}

	close(file);
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

#include<stdio.h>
#include<string.h>
#include<unistd.h>
#include"http.h"
#include"util.h"

static const char *status_text(int status){
	switch (status){
		case 200: return "OK";
		case 303: return "See Other";
		case 400: return "Bad Request";
		case 404: return "Not Found";
		case 405: return "Method Not Allowed";
		case 500: return "Internal Server Error";
		default:  return "Unknown";
	}
}

int http_parse(int client, Request *req){
	memset(req, 0, sizeof(*req));

	char buffer[4096];
	ssize_t n = read(client, buffer, sizeof(buffer)-1);
	if (n<=0){
		return -1;
	}
	buffer[n] = '\0';

	char target[1024];
	if (sscanf(buffer, "%7s %1023s", req->method, target) != 2){
		return -2;
	}

	// "/chemin?query" -> path = "/chemin", query = "query"
	char *q = strchr(target, '?');
	if (q != NULL){
		*q = '\0';
		if (strlen(q+1) >= sizeof(req->query)){return -2;}
		strcpy(req->query, q+1);
	}

	// dans un chemin, '+' reste un '+' (seule la query l'utilise pour l'espace)
	if (url_decode(target, strlen(target), req->path, sizeof(req->path), 0) < 0){
		return -2;
	}

	return 0;
}

// write() peut écrire moins que demandé : on boucle jusqu'au bout
int write_all(int fd, const char *buf, size_t len){
	while (len > 0){
		ssize_t n = write(fd, buf, len);
		if (n<=0){
			return -1;
		}
		buf += n;
		len -= n;
	}
	return 0;
}

void send_header(int client, int status, const char *content_type, size_t len){
	char header[256];
	int n = snprintf(header, sizeof(header),
		"HTTP/1.0 %d %s\r\n"
		"Content-Type: %s\r\n"
		"Content-Length: %zu\r\n"
		"\r\n",
		status, status_text(status), content_type, len
	);
	write_all(client, header, n);
}

void send_response(int client, int status, const char *content_type, const char *body, size_t len){
	send_header(client, status, content_type, len);
	write_all(client, body, len);
}

void send_404(int client){
	const char *body = "<h1>404 dumbass</h1>\n";
	send_response(client, 404, "text/html", body, strlen(body));
}

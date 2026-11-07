#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<strings.h>
#include<ctype.h>
#include<unistd.h>
#include"http.h"
#include"util.h"

#define MAX_HEADER 8192
#define MAX_BODY   (1024 * 1024)   // 1 Mo

static const char *status_text(int status){
	switch (status){
		case 200: return "OK";
		case 303: return "See Other";
		case 400: return "Bad Request";
		case 404: return "Not Found";
		case 405: return "Method Not Allowed";
		case 413: return "Payload Too Large";
		case 431: return "Request Header Fields Too Large";
		case 500: return "Internal Server Error";
		default:  return "Unknown";
	}
}

// cherche "Content-Length: 123" dans les en-têtes
// renvoie la longueur, 0 si l'en-tête est absent, -1 s'il est invalide
static long parse_content_length(const char *head){
	long length = 0;
	int seen = 0;

	// chaque en-tête commence juste après un "\r\n"
	for (const char *line = strstr(head, "\r\n"); line != NULL; line = strstr(line, "\r\n")){
		line += 2;
		if (strncasecmp(line, "Content-Length:", 15) != 0){continue;}  // nom insensible à la casse

		const char *p = line + 15;
		while (*p == ' ' || *p == '\t'){p++;}
		if (!isdigit((unsigned char)*p)){return -1;}  // refuse "", "-5", "abc"

		char *stop;
		long value = strtol(p, &stop, 10);
		while (*stop == ' ' || *stop == '\t'){stop++;}
		if (*stop != '\r' && *stop != '\0'){return -1;}  // refuse "12abc"

		if (seen && value != length){return -1;}  // deux valeurs différentes
		length = value;
		seen = 1;
	}

	return length;
}

int http_parse(int client, Request *req){
	memset(req, 0, sizeof(*req));

	// 1. lire jusqu'à la ligne vide "\r\n\r\n" qui termine les en-têtes
	//    (les données arrivent par morceaux : un seul read() ne suffit pas toujours)
	char head[MAX_HEADER];
	size_t used = 0;
	char *end = NULL;
	while (end == NULL){
		if (used == sizeof(head) - 1){return 431;}
		ssize_t n = read(client, head + used, sizeof(head) - 1 - used);
		if (n<=0){
			return used == 0 ? -1 : 400;
		}
		used += n;
		head[used] = '\0';
		end = strstr(head, "\r\n\r\n");
	}
	size_t head_len = (end - head) + 4;
	*end = '\0';  // les en-têtes deviennent une chaîne à part

	// 2. la ligne de requête : "GET /chemin?query HTTP/1.1"
	char target[1024];
	if (sscanf(head, "%7s %1023s", req->method, target) != 2){
		return 400;
	}

	// "/chemin?query" -> path = "/chemin", query = "query"
	char *q = strchr(target, '?');
	if (q != NULL){
		*q = '\0';
		if (strlen(q+1) >= sizeof(req->query)){return 400;}
		strcpy(req->query, q+1);
	}

	// dans un chemin, '+' reste un '+' (seule la query l'utilise pour l'espace)
	if (url_decode(target, strlen(target), req->path, sizeof(req->path), 0) < 0){
		return 400;
	}

	// 3. le corps : Content-Length dit exactement combien d'octets lire
	long length = parse_content_length(head);
	if (length < 0){return 400;}
	if (length > MAX_BODY){return 413;}

	req->body = malloc(length + 1);
	if (req->body == NULL){return 500;}

	// le début du corps est peut-être déjà arrivé avec les en-têtes
	size_t got = used - head_len;
	if (got > (size_t)length){got = length;}
	memcpy(req->body, head + head_len, got);

	while (got < (size_t)length){
		ssize_t n = read(client, req->body + got, length - got);
		if (n<=0){
			return 400;
		}
		got += n;
	}
	req->body[length] = '\0';
	req->body_len = length;

	return 0;
}

void http_free(Request *req){
	free(req->body);
	req->body = NULL;
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

void send_error(int client, int status){
	char body[128];
	int len = snprintf(body, sizeof(body), "<h1>%d %s</h1>\n", status, status_text(status));
	send_response(client, status, "text/html", body, len);
}

// `location` doit venir de NOTRE code : un "\r\n" venu d'un utilisateur
// permettrait d'injecter de faux en-têtes dans la réponse
void send_redirect(int client, const char *location){
	char header[512];
	int n = snprintf(header, sizeof(header),
		"HTTP/1.0 303 See Other\r\n"
		"Location: %s\r\n"
		"Content-Length: 0\r\n"
		"\r\n",
		location
	);
	write_all(client, header, n);
}

void send_404(int client){
	const char *body = "<h1>404 dumbass</h1>\n";
	send_response(client, 404, "text/html", body, strlen(body));
}

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<strings.h>
#include<ctype.h>
#include<unistd.h>
#include<errno.h>
#include<time.h>
#include<sys/socket.h>
#include<sys/time.h>
#include"internal.h"

#define MAX_HEADER 8192
#define MAX_BODY   (1024 * 1024)   // 1 Mo

static const char *status_text(int status){
	switch (status){
		case 200: return "OK";
		case 201: return "Created";
		case 303: return "See Other";
		case 400: return "Bad Request";
		case 401: return "Unauthorized";
		case 403: return "Forbidden";
		case 404: return "Not Found";
		case 405: return "Method Not Allowed";
		case 408: return "Request Timeout";
		case 409: return "Conflict";
		case 413: return "Payload Too Large";
		case 429: return "Too Many Requests";
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

// cherche l'en-tête `name` (ex : "Cookie") ; renvoie un pointeur sur sa valeur,
// qui s'arrête au "\r\n" suivant (ou au '\0'), ou NULL s'il est absent
static const char *find_header(const char *head, const char *name){
	size_t n = strlen(name);
	for (const char *line = strstr(head, "\r\n"); line != NULL; line = strstr(line, "\r\n")){
		line += 2;
		if (strncasecmp(line, name, n) == 0 && line[n] == ':'){
			const char *value = line + n + 1;
			while (*value == ' ' || *value == '\t'){value++;}
			return value;
		}
	}
	return NULL;
}

static double now_seconds(void){
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);   // MONOTONIC : insensible aux changements d'heure
	return t.tv_sec + t.tv_nsec / 1e9;
}

#define TIMED_OUT (-2)

// read() qui n'attend pas au-delà de `deadline` : chaque appel n'a droit qu'au temps
// qui reste. Un client qui envoie un octet toutes les 4 s (« slowloris ») ne peut donc
// plus garder la connexion ouverte indéfiniment. Renvoie TIMED_OUT si c'est trop tard.
static ssize_t read_before(int fd, char *buf, size_t size, double deadline){
	double left = deadline - now_seconds();
	if (left <= 0){return TIMED_OUT;}

	struct timeval tv;
	tv.tv_sec = (time_t)left;
	tv.tv_usec = (suseconds_t)((left - (double)tv.tv_sec) * 1e6);
	if (tv.tv_sec == 0 && tv.tv_usec == 0){tv.tv_usec = 1;}   // 0 voudrait dire « sans limite »
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	ssize_t n = read(fd, buf, size);
	if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)){return TIMED_OUT;}
	return n;
}

int http_parse(int client, cs_request *req, int timeout){
	memset(req, 0, sizeof(*req));
	double deadline = now_seconds() + timeout;

	// 1. lire jusqu'à la ligne vide "\r\n\r\n" qui termine les en-têtes
	//    (les données arrivent par morceaux : un seul read() ne suffit pas toujours)
	char head[MAX_HEADER];
	size_t used = 0;
	char *end = NULL;
	while (end == NULL){
		if (used == sizeof(head) - 1){return 431;}
		ssize_t n = read_before(client, head + used, sizeof(head) - 1 - used, deadline);
		if (n == TIMED_OUT){
			return used == 0 ? -1 : 408;   // rien reçu : on ferme simplement
		}
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
	if (cs_url_decode(target, strlen(target), req->path, sizeof(req->path), 0) < 0){
		return 400;
	}

	// 3. le cookie : trop long, on l'ignore plutôt que de refuser la requête
	//    (sur localhost, le navigateur envoie aussi les cookies des autres projets)
	const char *cookie = find_header(head, "Cookie");
	if (cookie != NULL){
		size_t len = strcspn(cookie, "\r");
		if (len < sizeof(req->cookie)){
			memcpy(req->cookie, cookie, len);
			req->cookie[len] = '\0';
		}
	}

	// 4. le corps : Content-Length dit exactement combien d'octets lire
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
		ssize_t n = read_before(client, req->body + got, length - got, deadline);
		if (n == TIMED_OUT){
			return 408;
		}
		if (n<=0){
			return 400;
		}
		got += n;
	}
	req->body[length] = '\0';
	req->body_len = length;

	return 0;
}

void http_free(cs_request *req){
	free(req->body);
	req->body = NULL;
}

// "a=1; cserver_session=f3a9...; b=2" : les paires sont séparées par des ';'
int http_cookie_get(const cs_request *req, const char *name, char *out, size_t out_size){
	size_t n = strlen(name);
	const char *p = req->cookie;

	while (*p != '\0'){
		while (*p == ' ' || *p == ';'){p++;}
		const char *end = strchr(p, ';');
		if (end == NULL){end = p + strlen(p);}

		if ((size_t)(end - p) > n && strncmp(p, name, n) == 0 && p[n] == '='){
			const char *value = p + n + 1;
			size_t len = end - value;
			while (len > 0 && value[len-1] == ' '){len--;}
			if (len >= out_size){return -1;}
			memcpy(out, value, len);
			out[len] = '\0';
			return 0;
		}

		p = end;
	}

	return -1;
}

// write() peut écrire moins que demandé : on boucle jusqu'au bout
int http_write_all(int fd, const char *buf, size_t len){
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

// extra : des lignes d'en-têtes en plus ("Set-Cookie: ...\r\n"), ou NULL.
// Elles doivent venir de NOTRE code (cs_header les vérifie) : un "\r\n" venu
// d'un utilisateur permettrait d'injecter de faux en-têtes dans la réponse
void http_send_header(int client, int status, const char *content_type, size_t len, const char *extra){
	char header[2048];
	int n = snprintf(header, sizeof(header),
		"HTTP/1.0 %d %s\r\n"
		"Content-Type: %s\r\n"
		"Content-Length: %zu\r\n"
		"%s"
		"\r\n",
		status, status_text(status), content_type, len, extra != NULL ? extra : ""
	);
	if (n < 0 || (size_t)n >= sizeof(header)){return;}
	http_write_all(client, header, n);
}

void http_send(int client, int status, const char *content_type, const char *body, size_t len, const char *extra){
	http_send_header(client, status, content_type, len, extra);
	http_write_all(client, body, len);
}

void http_send_error(int client, int status, const char *extra){
	char body[128];
	int len = snprintf(body, sizeof(body), "<h1>%d %s</h1>\n", status, status_text(status));
	http_send(client, status, "text/html", body, len, extra);
}

void http_redirect(int client, const char *location, const char *extra){
	char header[2048];
	int n = snprintf(header, sizeof(header),
		"HTTP/1.0 303 See Other\r\n"
		"Location: %s\r\n"
		"%s"
		"Content-Length: 0\r\n"
		"\r\n",
		location, extra != NULL ? extra : ""
	);
	if (n < 0 || (size_t)n >= sizeof(header)){
		http_send_error(client, 500, NULL);
		return;
	}
	http_write_all(client, header, n);
}

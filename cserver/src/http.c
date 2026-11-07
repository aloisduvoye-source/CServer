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

#define MAX_BODY       (1024 * 1024)   // 1 Mo
#define MAX_LINE       256             // une ligne de taille de morceau, ou d'en-tête final
#define MAX_TRAILERS   32              // en-têtes après le dernier morceau

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
		case 501: return "Not Implemented";
		case 505: return "HTTP Version Not Supported";
		default:  return "Unknown";
	}
}

/* ---------------------------------------------------------------- en-têtes */

// cherche "Content-Length: 123" dans les en-têtes
// renvoie la longueur, 0 si l'en-tête est absent, -1 s'il est invalide
static long parse_content_length(const char *head, int *present){
	long length = 0;
	*present = 0;

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

		if (*present && value != length){return -1;}  // deux valeurs différentes
		length = value;
		*present = 1;
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

// la valeur de l'en-tête `name`, en minuscules, sans les espaces de fin ("" si absent)
static void header_lower(const char *head, const char *name, char *out, size_t size){
	const char *v = find_header(head, name);
	size_t n = 0;
	if (v != NULL){
		while (v[n] != '\0' && v[n] != '\r' && n + 1 < size){
			out[n] = (char)tolower((unsigned char)v[n]);
			n++;
		}
	}
	while (n > 0 && (out[n-1] == ' ' || out[n-1] == '\t')){n--;}
	out[n] = '\0';
}

// ce que les en-têtes disent du corps et de la connexion
typedef struct {
	long length;       // Content-Length (sans morceaux)
	int  chunked;      // Transfer-Encoding: chunked
	int  expect;       // Expect: 100-continue
	int  keep_alive;   // la connexion peut servir à la requête suivante
} head_info;

// analyse les en-têtes (head : terminés par '\0', sans la ligne vide finale)
// renvoie 0, ou le code HTTP d'erreur
static int parse_head(char *head, cs_request *req, head_info *info){
	memset(info, 0, sizeof(*info));

	// la ligne de requête : "GET /chemin?query HTTP/1.1"
	char target[1024];
	char version[16];
	if (sscanf(head, "%7s %1023s %15s", req->method, target, version) != 3){
		return 400;
	}
	if (strncmp(version, "HTTP/", 5) != 0){return 400;}
	int http11 = strcmp(version, "HTTP/1.1") == 0;
	if (!http11 && strcmp(version, "HTTP/1.0") != 0){return 505;}

	// HTTP/1.1 exige l'en-tête Host
	if (http11 && find_header(head, "Host") == NULL){return 400;}

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

	// le cookie : trop long, on l'ignore plutôt que de refuser la requête
	// (sur localhost, le navigateur envoie aussi les cookies des autres projets)
	const char *cookie = find_header(head, "Cookie");
	if (cookie != NULL){
		size_t len = strcspn(cookie, "\r");
		if (len < sizeof(req->cookie)){
			memcpy(req->cookie, cookie, len);
			req->cookie[len] = '\0';
		}
	}

	// le jeton CSRF envoyé par du JavaScript (trop long : ignoré, la vérification échouera)
	const char *csrf = find_header(head, "X-CSRF-Token");
	if (csrf != NULL){
		size_t len = strcspn(csrf, "\r \t");
		if (len < sizeof(req->csrf)){
			memcpy(req->csrf, csrf, len);
			req->csrf[len] = '\0';
		}
	}

	// garder la connexion ? Par défaut oui en 1.1, non en 1.0 ; l'en-tête Connection décide
	char value[128];
	header_lower(head, "Connection", value, sizeof(value));
	info->keep_alive = http11;
	if (strstr(value, "close") != NULL){info->keep_alive = 0;}
	else if (strstr(value, "keep-alive") != NULL){info->keep_alive = 1;}

	header_lower(head, "Expect", value, sizeof(value));
	info->expect = strcmp(value, "100-continue") == 0;

	// le corps : Content-Length, OU des morceaux (Transfer-Encoding: chunked).
	// Les deux à la fois : refusé. Deux serveurs qui ne choisiraient pas le même
	// (un proxy et celui-ci) ne verraient pas la même requête : c'est la base de
	// l'attaque « request smuggling ».
	int has_length;
	info->length = parse_content_length(head, &has_length);
	if (info->length < 0){return 400;}

	header_lower(head, "Transfer-Encoding", value, sizeof(value));
	if (find_header(head, "Transfer-Encoding") != NULL){
		if (has_length){return 400;}
		if (strcmp(value, "chunked") != 0){return 501;}   // gzip, etc. : non pris en charge
		info->chunked = 1;
	}
	if (info->length > MAX_BODY){return 413;}
	return 0;
}

/* ---------------------------------------------------------------- réception */

static double now_seconds(void){
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);   // MONOTONIC : insensible aux changements d'heure
	return t.tv_sec + t.tv_nsec / 1e9;
}

#define TIMED_OUT (-2)

// reçoit au plus `size` octets, sans attendre au-delà de `deadline` : chaque appel
// n'a droit qu'au temps qui reste. Un client qui envoie un octet toutes les 4 s
// (« slowloris ») ne peut donc pas garder la connexion indéfiniment.
// Renvoie le nombre d'octets, 0 si la connexion est fermée, TIMED_OUT, ou -1.
static ssize_t conn_recv(http_conn *conn, char *buf, size_t size, double deadline){
	if (conn->fd < 0){   // fuzzing : les octets viennent de src
		size_t n = size < conn->src_len ? size : conn->src_len;
		memcpy(buf, conn->src, n);
		conn->src += n;
		conn->src_len -= n;
		return (ssize_t)n;
	}

	double left = deadline - now_seconds();
	if (left <= 0){return TIMED_OUT;}

	struct timeval tv;
	tv.tv_sec = (time_t)left;
	tv.tv_usec = (suseconds_t)((left - (double)tv.tv_sec) * 1e6);
	if (tv.tv_sec == 0 && tv.tv_usec == 0){tv.tv_usec = 1;}   // 0 voudrait dire « sans limite »
	setsockopt(conn->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	ssize_t n = read(conn->fd, buf, size);
	if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)){return TIMED_OUT;}
	return n;
}

// ajoute au tampon ce qui arrive
static ssize_t conn_fill(http_conn *conn, double deadline){
	ssize_t n = conn_recv(conn, conn->buf + conn->len, sizeof(conn->buf) - conn->len, deadline);
	if (n > 0){conn->len += n;}
	return n;
}

// retire les n premiers octets du tampon (ils ont été utilisés)
static void conn_take(http_conn *conn, size_t n){
	memmove(conn->buf, conn->buf + n, conn->len - n);
	conn->len -= n;
}

// au plus n octets : d'abord ceux déjà dans le tampon, sinon directement depuis la
// connexion (sans jamais lire plus que demandé : la suite appartient à la requête d'après)
static ssize_t conn_read(http_conn *conn, char *dst, size_t n, double deadline){
	if (conn->len > 0){
		size_t k = n < conn->len ? n : conn->len;
		memcpy(dst, conn->buf, k);
		conn_take(conn, k);
		return (ssize_t)k;
	}
	return conn_recv(conn, dst, n, deadline);
}

// comme strstr, mais dans n octets qui ne finissent pas forcément par '\0'
static char *find_in(char *p, size_t n, const char *needle){
	size_t k = strlen(needle);
	for (size_t i = 0; i + k <= n; i++){
		if (memcmp(p + i, needle, k) == 0){return p + i;}
	}
	return NULL;
}

// une ligne terminée par "\r\n" (sans les "\r\n"), pour le corps en morceaux
// renvoie 0, ou le code HTTP d'erreur
static int read_line(http_conn *conn, char *out, size_t size, double deadline){
	char *end;
	while ((end = find_in(conn->buf, conn->len, "\r\n")) == NULL){
		if (conn->len >= size || conn->len == sizeof(conn->buf)){return 400;}   // ligne trop longue
		ssize_t n = conn_fill(conn, deadline);
		if (n == TIMED_OUT){return 408;}
		if (n <= 0){return 400;}
	}
	size_t len = end - conn->buf;
	if (len >= size){return 400;}
	memcpy(out, conn->buf, len);
	out[len] = '\0';
	conn_take(conn, len + 2);
	return 0;
}

// exactement n octets, dans dst
static int read_exact(http_conn *conn, char *dst, size_t n, double deadline){
	size_t got = 0;
	while (got < n){
		ssize_t k = conn_read(conn, dst + got, n - got, deadline);
		if (k == TIMED_OUT){return 408;}
		if (k <= 0){return 400;}
		got += k;
	}
	return 0;
}

// un corps envoyé en morceaux :  "1a\r\n<26 octets>\r\n" ... "0\r\n" [en-têtes finaux] "\r\n"
static int read_chunked(http_conn *conn, cs_request *req, double deadline){
	size_t total = 0;
	char line[MAX_LINE];

	while (1){
		int status = read_line(conn, line, sizeof(line), deadline);
		if (status != 0){return status;}

		// la taille, en hexadécimal, éventuellement suivie de ";extension"
		if (!isxdigit((unsigned char)line[0])){return 400;}
		size_t digits = strspn(line, "0123456789abcdefABCDEF");
		if (digits > 8){return 413;}   // plus de 4 Go : inutile d'aller plus loin
		if (line[digits] != '\0' && line[digits] != ';' && line[digits] != ' ' && line[digits] != '\t'){
			return 400;
		}
		size_t size = (size_t)strtoul(line, NULL, 16);
		if (size == 0){break;}

		if (total + size > MAX_BODY){return 413;}
		char *body = realloc(req->body, total + size + 1);
		if (body == NULL){return 500;}
		req->body = body;

		status = read_exact(conn, req->body + total, size, deadline);
		if (status != 0){return status;}
		total += size;
		req->body[total] = '\0';

		// chaque morceau est suivi d'un "\r\n"
		status = read_line(conn, line, sizeof(line), deadline);
		if (status != 0){return status;}
		if (line[0] != '\0'){return 400;}
	}

	// d'éventuels en-têtes après le dernier morceau, jusqu'à la ligne vide : ignorés
	for (int i = 0; ; i++){
		if (i == MAX_TRAILERS){return 400;}
		int status = read_line(conn, line, sizeof(line), deadline);
		if (status != 0){return status;}
		if (line[0] == '\0'){break;}
	}

	req->body_len = total;
	return 0;
}

int http_parse(http_conn *conn, cs_request *req, int idle_timeout, int read_timeout, int *keep_alive){
	memset(req, 0, sizeof(*req));
	*keep_alive = 0;

	// 1. jusqu'à la ligne vide "\r\n\r\n" qui termine les en-têtes. Tant que rien n'est
	//    arrivé, on attend idle_timeout ; dès le premier octet, la requête a
	//    read_timeout pour arriver en entier.
	int started = conn->len > 0;
	double deadline = now_seconds() + (started ? read_timeout : idle_timeout);
	char *end;
	while ((end = find_in(conn->buf, conn->len, "\r\n\r\n")) == NULL){
		if (conn->len == sizeof(conn->buf)){return 431;}
		ssize_t n = conn_fill(conn, deadline);
		if (n == TIMED_OUT){
			return started ? 408 : -1;   // rien reçu : on ferme simplement
		}
		if (n <= 0){
			return started ? 400 : -1;
		}
		if (!started){
			started = 1;
			deadline = now_seconds() + read_timeout;
		}
	}

	// les en-têtes, copiés pour former une chaîne ; un '\0' au milieu : refusé
	size_t head_len = end - conn->buf;
	if (memchr(conn->buf, '\0', head_len) != NULL){return 400;}
	char head[HTTP_BUF];
	memcpy(head, conn->buf, head_len);
	head[head_len] = '\0';
	conn_take(conn, head_len + 4);

	// 2. les en-têtes
	head_info info;
	int status = parse_head(head, req, &info);
	if (status != 0){return status;}

	// 3. le corps. Avec "Expect: 100-continue", le client attend notre accord avant
	//    de l'envoyer (curl le fait pour les gros envois)
	int has_body = info.chunked || info.length > 0;
	if (info.expect && has_body && conn->len == 0 && conn->fd >= 0){
		static const char go[] = "HTTP/1.1 100 Continue\r\n\r\n";
		http_write_all(conn->fd, go, sizeof(go) - 1);
	}

	req->body = malloc(info.length + 1);
	if (req->body == NULL){return 500;}
	req->body[0] = '\0';

	if (info.chunked){
		status = read_chunked(conn, req, deadline);
	}
	else {
		status = read_exact(conn, req->body, info.length, deadline);
		req->body[info.length] = '\0';
		req->body_len = info.length;
	}
	if (status != 0){return status;}

	*keep_alive = info.keep_alive;
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

/* ---------------------------------------------------------------- réponses */

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
		"HTTP/1.1 %d %s\r\n"
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

void http_send_error(int client, int status, const char *extra, int with_body){
	char body[128];
	int len = snprintf(body, sizeof(body), "<h1>%d %s</h1>\n", status, status_text(status));
	http_send_header(client, status, "text/html", len, extra);
	if (with_body){
		http_write_all(client, body, len);
	}
}

void http_redirect(int client, const char *location, const char *extra){
	char header[2048];
	int n = snprintf(header, sizeof(header),
		"HTTP/1.1 303 See Other\r\n"
		"Location: %s\r\n"
		"%s"
		"Content-Length: 0\r\n"
		"\r\n",
		location, extra != NULL ? extra : ""
	);
	if (n < 0 || (size_t)n >= sizeof(header)){
		http_send_error(client, 500, NULL, 1);
		return;
	}
	http_write_all(client, header, n);
}

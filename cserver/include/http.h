#ifndef HTTP_H
#define HTTP_H

#include<stddef.h>

typedef struct {
	char   method[8];
	char   path[256];     // sans la query, déjà décodé (%20 -> ' ')
	char   query[512];    // ce qui suit le "?", BRUT : lire avec query_get()
	char   cookie[4096];  // en-tête Cookie brut ("a=1; b=2") : lire avec cookie_get()
	char   ip[46];        // adresse du client (remplie par main.c)
	char  *body;          // corps (POST), terminé par '\0', jamais NULL si le parse a réussi
	size_t body_len;
} Request;

// lit et analyse la requête (en-têtes + corps)
// renvoie 0 si OK, -1 si rien reçu, sinon le code HTTP d'erreur à renvoyer (400, 413...)
int  http_parse(int client, Request *req);
void http_free(Request *req);

// valeur du cookie `name` ; renvoie 0 si trouvé, -1 sinon
int  cookie_get(const Request *req, const char *name, char *out, size_t out_size);

int  write_all(int fd, const char *buf, size_t len);
void send_header(int client, int status, const char *content_type, size_t len);
void send_response(int client, int status, const char *content_type, const char *body, size_t len);
void send_error(int client, int status);

// 303 See Other ; extra_headers : lignes d'en-têtes en plus ("Set-Cookie: ...\r\n") ou NULL
void send_redirect(int client, const char *location, const char *extra_headers);

#endif

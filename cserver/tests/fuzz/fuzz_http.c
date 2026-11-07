// Fuzzing de l'analyse des requêtes HTTP : le premier code qui lit ce qu'envoie un
// inconnu. libFuzzer appelle cette fonction des millions de fois avec des données
// différentes ; AddressSanitizer arrête tout au moindre accès mémoire invalide.
//
// Les données sont « reçues » par une connexion simulée (fd = -1) : plusieurs requêtes
// peuvent s'y suivre, comme en HTTP/1.1, et le corps peut arriver en morceaux.
#include<stdint.h>
#include<string.h>
#include<stdlib.h>
#include"internal.h"

// si l'analyse réussit, ce qu'elle a rempli doit être cohérent
static void check(const cs_request *req){
	if (strlen(req->method) >= sizeof(req->method)){abort();}
	if (strlen(req->path) >= sizeof(req->path)){abort();}
	if (strlen(req->query) >= sizeof(req->query)){abort();}
	if (strlen(req->cookie) >= sizeof(req->cookie)){abort();}
	if (req->body == NULL || req->body[req->body_len] != '\0'){abort();}
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size){
	static http_conn conn;
	memset(&conn, 0, sizeof(conn));
	conn.fd = -1;
	conn.src = (const char *)data;
	conn.src_len = size;

	// jusqu'à 8 requêtes à la suite sur la même connexion
	for (int i = 0; i < 8; i++){
		cs_request req;
		int keep_alive;
		int r = http_parse(&conn, &req, 1, 1, &keep_alive);
		if (r == 0){
			check(&req);
			if (conn.len > sizeof(conn.buf)){abort();}

			// et ce que les handlers en font ensuite : query, formulaire, cookie
			char out[64];
			cs_params_get(req.query, "a", out, sizeof(out));
			cs_params_get(req.body, "name", out, sizeof(out));
			http_cookie_get(&req, "cs_session", out, sizeof(out));
		}
		http_free(&req);
		if (r != 0 || !keep_alive){break;}
	}
	return 0;
}

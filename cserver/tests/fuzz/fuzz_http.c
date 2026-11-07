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
	if (strlen(req->content_type) >= sizeof(req->content_type)){abort();}
	if (req->body == NULL || req->body[req->body_len] != '\0'){abort();}
}

// un fichier trouvé doit être entièrement dans le corps
static void check_file(const cs_request *req, const cs_file *f){
	if (f->data < req->body || f->len > req->body_len
	 || (size_t)(f->data - req->body) > req->body_len - f->len){abort();}
	if (strlen(f->filename) >= sizeof(f->filename) || strlen(f->type) >= sizeof(f->type)){abort();}
	cs_image_type(f->data, f->len);
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

			// et ce que les handlers en font ensuite : query, formulaire (classique ou
			// multipart, avec ses fichiers), cookie
			char out[64];
			cs_params_get(req.query, "a", out, sizeof(out));
			if (form_get(&req, "name", out, sizeof(out)) == 0 && strlen(out) >= sizeof(out)){abort();}
			form_get(&req, "csrf", out, 1);
			cs_file f;
			if (form_file(&req, "file", &f) == 0){check_file(&req, &f);}
			http_cookie_get(&req, "cs_session", out, sizeof(out));
		}
		http_free(&req);
		if (r != 0 || !keep_alive){break;}
	}
	return 0;
}

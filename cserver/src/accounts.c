// les briques des comptes : mots de passe, utilisateurs, connexion, sessions.
// Les pages prêtes à l'emploi (auth.c) sont écrites uniquement avec elles ; un site
// peut s'en servir pour écrire les siennes (connexion par e-mail, par API...).
#include<stdio.h>
#include<string.h>
#include<time.h>
#include<crypt.h>
#include<sys/random.h>
#include"internal.h"

#define SESSION_SECONDS (7 * 24 * 3600)     // 7 jours
#define TOKEN_BYTES     32                  // 256 bits d'aléatoire
#define TOKEN_HEX       (TOKEN_BYTES * 2)

_Static_assert(CS_HASH_MAX >= CRYPT_OUTPUT_SIZE, "CS_HASH_MAX trop petit pour libcrypt");

/* ---------------------------------------------------------------- mots de passe */

// yescrypt ; le résultat contient l'algorithme, le sel et le hachage :
// "$y$j9T$<sel>$<hachage>"
int cs_password_hash(const char *password, char *out, size_t size){
	char salt[CRYPT_GENSALT_OUTPUT_SIZE];
	// rbytes = NULL : libcrypt tire elle-même un sel aléatoire ; count = 0 : coût par défaut
	if (crypt_gensalt_rn("$y$", 0, NULL, 0, salt, sizeof(salt)) == NULL){return -1;}

	struct crypt_data data;
	memset(&data, 0, sizeof(data));
	const char *hash = crypt_rn(password, salt, &data, sizeof(data));
	if (hash == NULL || hash[0] == '*'){return -1;}   // '*' : échec, selon libcrypt
	if (strlen(hash) >= size){return -1;}
	strcpy(out, hash);
	return 0;
}

// compare sans s'arrêter à la première différence : le temps de réponse
// ne révèle pas à quel endroit les chaînes diffèrent
int const_time_eq(const char *a, const char *b){
	size_t la = strlen(a);
	if (la != strlen(b)){return 0;}
	unsigned char diff = 0;
	for (size_t i = 0; i < la; i++){
		diff |= (unsigned char)a[i] ^ (unsigned char)b[i];
	}
	return diff == 0;
}

// Si hash est NULL (utilisateur inconnu), on fait quand même un calcul aussi long :
// sinon, la réponse plus rapide révélerait quels noms existent
int cs_password_verify(const char *password, const char *hash){
	if (hash == NULL){
		char dummy[CS_HASH_MAX];
		cs_password_hash(password, dummy, sizeof(dummy));
		return 0;
	}

	struct crypt_data data;
	memset(&data, 0, sizeof(data));
	const char *computed = crypt_rn(password, hash, &data, sizeof(data));
	return computed != NULL && computed[0] != '*' && const_time_eq(computed, hash);
}

/* ---------------------------------------------------------------- utilisateurs */

long cs_user_create(cs_ctx *c, const char *name, const char *password){
	char hash[CS_HASH_MAX];
	sqlite3 *db = cs_config.accounts ? cs_db(c) : NULL;
	if (db == NULL || cs_password_hash(password, hash, sizeof(hash)) < 0){return -1;}
	return db_user_create(db, name, hash);
}

int cs_user_login(cs_ctx *c, const char *name, const char *password, cs_user *user){
	sqlite3 *db = cs_config.accounts ? cs_db(c) : NULL;
	if (db == NULL){return CS_LOGIN_ERROR;}

	// trop d'échecs récents depuis cette adresse : on refuse sans même vérifier
	long now = (long)time(NULL);
	int failures = db_login_failures(db, c->req.ip, now - cs_config.login_window);
	if (failures < 0){return CS_LOGIN_ERROR;}
	if (failures >= cs_config.login_attempts){return CS_LOGIN_BLOCKED;}

	char hash[CS_HASH_MAX];
	int found = name != NULL && db_user_find(db, name, user, hash, sizeof(hash)) == 1;

	// le hachage est calculé même si le nom n'existe pas (même durée de réponse)
	int valid = password != NULL && cs_password_verify(password, found ? hash : NULL) && found;
	if (!valid){
		db_login_failure_add(db, c->req.ip, now, now - cs_config.login_window);
		return CS_LOGIN_WRONG;
	}
	return CS_LOGIN_OK;
}

/* ---------------------------------------------------------------- sessions */

// jeton imprévisible : getrandom() lit l'aléatoire du noyau (jamais rand() !)
static int new_token(char out[TOKEN_HEX + 1]){
	static const char hex[] = "0123456789abcdef";
	unsigned char bytes[TOKEN_BYTES];
	if (getrandom(bytes, sizeof(bytes), 0) != (ssize_t)sizeof(bytes)){return -1;}

	for (int i = 0; i < TOKEN_BYTES; i++){
		out[2*i]   = hex[bytes[i] >> 4];
		out[2*i+1] = hex[bytes[i] & 0xF];
	}
	out[TOKEN_HEX] = '\0';
	return 0;
}

int auth_lookup_user(cs_ctx *c, cs_user *user){
	char token[TOKEN_HEX + 1];
	if (cs_cookie(c, cs_config.cookie_name, token, sizeof(token)) < 0){return 0;}
	if (strlen(token) != TOKEN_HEX){return 0;}

	sqlite3 *db = cs_db(c);
	return db != NULL && db_session_user(db, token, (long)time(NULL), user) == 1;
}

int cs_session_start(cs_ctx *c, long user_id){
	char token[TOKEN_HEX + 1];
	long expires = (long)time(NULL) + SESSION_SECONDS;
	sqlite3 *db = cs_config.accounts ? cs_db(c) : NULL;
	if (db == NULL || new_token(token) < 0 || db_session_create(db, token, user_id, expires) < 0){
		return -1;
	}

	// HttpOnly : invisible pour le JavaScript (une faille XSS ne peut pas le voler)
	// SameSite=Lax : pas envoyé avec les formulaires d'autres sites (CSRF)
	// pas de Secure : il obligerait le HTTPS, que ce serveur n'a pas
	char cookie[256];
	snprintf(cookie, sizeof(cookie), "Set-Cookie: %s=%s; Path=/; Max-Age=%d; HttpOnly; SameSite=Lax",
		cs_config.cookie_name, token, SESSION_SECONDS);
	cs_header(c, cookie);

	// un nouveau jeton CSRF à chaque connexion : celui d'avant (peut-être vu par
	// quelqu'un d'autre sur cet ordinateur) ne sert plus
	if (csrf_new(c) < 0){return -1;}

	// la suite de la requête voit déjà la personne connectée
	c->user_state = 0;
	return 0;
}

void cs_session_end(cs_ctx *c){
	// la session est supprimée côté serveur : même un cookie copié ne marche plus
	char token[TOKEN_HEX + 1];
	sqlite3 *db = cs_config.accounts ? cs_db(c) : NULL;
	if (db != NULL && cs_cookie(c, cs_config.cookie_name, token, sizeof(token)) == 0){
		db_session_delete(db, token);
	}

	// Max-Age=0 : le navigateur efface le cookie
	char cookie[128];
	snprintf(cookie, sizeof(cookie), "Set-Cookie: %s=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax",
		cs_config.cookie_name);
	cs_header(c, cookie);
	c->user_state = -1;
}

/* ---------------------------------------------------------------- jeton CSRF */

// "Double envoi" : le jeton est dans un cookie ET dans chaque formulaire. Un autre site
// peut faire envoyer un formulaire à ton navigateur, avec tes cookies, mais il ne peut
// pas lire le jeton pour le recopier dans le formulaire : la requête est refusée.

static void csrf_cookie_name(char *out, size_t size){
	snprintf(out, size, "%s_csrf", cs_config.cookie_name);
}

int csrf_new(cs_ctx *c){
	if (new_token(c->csrf) < 0){return -1;}

	char name[96];
	char cookie[256];
	csrf_cookie_name(name, sizeof(name));
	snprintf(cookie, sizeof(cookie), "Set-Cookie: %s=%s; Path=/; Max-Age=%d; HttpOnly; SameSite=Lax",
		name, c->csrf, SESSION_SECONDS);
	cs_header(c, cookie);
	return 0;
}

const char *cs_csrf_token(cs_ctx *c){
	if (!cs_config.accounts){return "";}   // sans comptes, pas de session à protéger
	if (c->csrf[0] != '\0'){return c->csrf;}

	// celui du navigateur, sinon un nouveau
	char name[96];
	csrf_cookie_name(name, sizeof(name));
	char token[TOKEN_HEX + 1];
	if (cs_cookie(c, name, token, sizeof(token)) == 0 && strlen(token) == TOKEN_HEX){
		memcpy(c->csrf, token, sizeof(token));
		return c->csrf;
	}
	return csrf_new(c) == 0 ? c->csrf : "";
}

int csrf_check(cs_ctx *c){
	// sans cookie de session, il n'y a rien à usurper : pas de vérification
	char session[TOKEN_HEX + 1];
	if (!cs_config.accounts || cs_cookie(c, cs_config.cookie_name, session, sizeof(session)) < 0){
		return 1;
	}

	char name[96];
	char expected[TOKEN_HEX + 1];
	csrf_cookie_name(name, sizeof(name));
	if (cs_cookie(c, name, expected, sizeof(expected)) < 0 || strlen(expected) != TOKEN_HEX){
		return 0;
	}

	// le jeton envoyé : dans le formulaire, ou dans l'en-tête X-CSRF-Token (JavaScript)
	char sent[TOKEN_HEX + 1];
	if (cs_form(c, "csrf", sent, sizeof(sent)) < 0){
		if (strlen(c->req.csrf) >= sizeof(sent)){return 0;}
		strcpy(sent, c->req.csrf);
	}
	return const_time_eq(sent, expected);
}

#include<stdio.h>
#include<string.h>
#include<ctype.h>
#include<time.h>
#include<crypt.h>
#include<sys/random.h>
#include"auth.h"
#include"buffer.h"
#include"page.h"
#include"util.h"

// nom spécifique : sur localhost, un cookie "session" d'un autre projet arriverait ici
#define COOKIE_NAME     "cserver_session"
#define SESSION_SECONDS (7 * 24 * 3600)     // 7 jours
#define TOKEN_BYTES     32                  // 256 bits d'aléatoire
#define TOKEN_HEX       (TOKEN_BYTES * 2)

#define USERNAME_MIN 3
#define USERNAME_MAX 20
#define PASSWORD_MIN 8
#define PASSWORD_MAX 128

#define MAX_FAILURES   5                    // échecs de connexion autorisés...
#define FAILURE_WINDOW (15 * 60)            // ... par adresse IP, sur 15 minutes

/* ---------------------------------------------------------------- mots de passe */

// hache avec yescrypt ; le résultat contient l'algorithme, le sel et le hachage :
// "$y$j9T$<sel>$<hachage>". Renvoie 0 si OK, -1 si erreur.
static int password_hash(const char *password, char *out, size_t out_size){
	char salt[CRYPT_GENSALT_OUTPUT_SIZE];
	// rbytes = NULL : libcrypt tire elle-même un sel aléatoire ; count = 0 : coût par défaut
	if (crypt_gensalt_rn("$y$", 0, NULL, 0, salt, sizeof(salt)) == NULL){return -1;}

	struct crypt_data data;
	memset(&data, 0, sizeof(data));
	const char *hash = crypt_rn(password, salt, &data, sizeof(data));
	if (hash == NULL || hash[0] == '*'){return -1;}   // '*' : échec, selon libcrypt
	if (strlen(hash) >= out_size){return -1;}
	strcpy(out, hash);
	return 0;
}

// compare sans s'arrêter à la première différence : le temps de réponse
// ne révèle pas à quel endroit les chaînes diffèrent
static int const_time_eq(const char *a, const char *b){
	size_t la = strlen(a);
	if (la != strlen(b)){return 0;}
	unsigned char diff = 0;
	for (size_t i = 0; i < la; i++){
		diff |= (unsigned char)a[i] ^ (unsigned char)b[i];
	}
	return diff == 0;
}

// 1 si password correspond à hash. Si hash est NULL (utilisateur inconnu), on fait
// quand même un calcul aussi long : sinon, la réponse plus rapide révélerait
// quels noms existent
static int password_verify(const char *password, const char *hash){
	if (hash == NULL){
		char dummy[CRYPT_OUTPUT_SIZE];
		password_hash(password, dummy, sizeof(dummy));
		return 0;
	}

	struct crypt_data data;
	memset(&data, 0, sizeof(data));
	const char *computed = crypt_rn(password, hash, &data, sizeof(data));
	return computed != NULL && computed[0] != '*' && const_time_eq(computed, hash);
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

int auth_current_user(sqlite3 *db, const Request *req, User *user){
	char token[TOKEN_HEX + 1];
	if (cookie_get(req, COOKIE_NAME, token, sizeof(token)) < 0){return 0;}
	if (strlen(token) != TOKEN_HEX){return 0;}
	return db_session_user(db, token, (long)time(NULL), user) == 1;
}

// crée la session, pose le cookie et redirige vers le livre d'or
static void start_session(int client, sqlite3 *db, long user_id){
	char token[TOKEN_HEX + 1];
	long expires = (long)time(NULL) + SESSION_SECONDS;
	if (new_token(token) < 0 || db_session_create(db, token, user_id, expires) < 0){
		send_error(client, 500);
		return;
	}

	// HttpOnly : invisible pour le JavaScript (une faille XSS ne peut pas le voler)
	// SameSite=Lax : pas envoyé avec les formulaires d'autres sites (CSRF)
	// pas de Secure : il obligerait le HTTPS, que ce serveur n'a pas
	char cookie[256];
	snprintf(cookie, sizeof(cookie),
		"Set-Cookie: " COOKIE_NAME "=%s; Path=/; Max-Age=%d; HttpOnly; SameSite=Lax\r\n",
		token, SESSION_SECONDS);
	send_redirect(client, "/guestbook", cookie);
}

/* ---------------------------------------------------------------- pages */

// formulaire d'inscription (signup = 1) ou de connexion (signup = 0)
// name : valeur à réafficher dans le champ ; error : message, ou NULL
static void send_form(int client, int status, int signup, const char *name, const char *error){
	Buffer page;
	buf_init(&page);
	page_begin(&page, signup ? "sign up" : "log in", NULL, NULL);

	buf_append(&page, signup ? "    <h1>sign up</h1>\n" : "    <h1>log in</h1>\n");
	buf_append(&page, signup ? "    <form method=\"post\" action=\"/signup\">\n"
	                         : "    <form method=\"post\" action=\"/login\">\n");
	buf_append(&page, "        <p><input name=\"name\" placeholder=\"name\" autocomplete=\"username\" required value=\"");
	buf_append_html(&page, name);   // dans un attribut "..." : le " doit être échappé
	buf_append(&page, "\"></p>\n");
	buf_append(&page, signup
		? "        <p><input name=\"password\" type=\"password\" placeholder=\"password (8+ characters)\" autocomplete=\"new-password\" minlength=\"8\" required></p>\n"
		: "        <p><input name=\"password\" type=\"password\" placeholder=\"password\" autocomplete=\"current-password\" required></p>\n");
	buf_append(&page, signup ? "        <p><button>sign up</button>" : "        <p><button>log in</button>");
	if (error != NULL){
		buf_append(&page, " <span class=\"error\">");
		buf_append_html(&page, error);
		buf_append(&page, "</span>");
	}
	buf_append(&page, "</p>\n    </form>\n");
	buf_append(&page, signup ? "    <p>already registered? <a href=\"/login\">log in</a></p>\n"
	                         : "    <p>no account? <a href=\"/signup\">sign up</a></p>\n");

	page_end(&page);
	page_send(client, status, &page);
}

void auth_signup_page(int client, Request *req){
	(void)req;
	send_form(client, 200, 1, "", NULL);
}

void auth_login_page(int client, Request *req){
	(void)req;
	send_form(client, 200, 0, "", NULL);
}

/* ---------------------------------------------------------------- actions */

// 3 à 20 caractères parmi a-z A-Z 0-9 _ - : pas d'espaces, pas de sosies Unicode
static int valid_username(const char *s){
	size_t n = strlen(s);
	if (n < USERNAME_MIN || n > USERNAME_MAX){return 0;}
	for (; *s != '\0'; s++){
		if (!isalnum((unsigned char)*s) && *s != '_' && *s != '-'){return 0;}
	}
	return 1;
}

void auth_signup(int client, Request *req){
	char name[64];
	char password[PASSWORD_MAX + 1];

	// attention : si query_get échoue, le buffer n'est pas une chaîne valide
	if (query_get(req->body, "name", name, sizeof(name)) < 0){
		send_form(client, 400, 1, "", "name: 3 to 20 letters, digits, _ or -");
		return;
	}
	if (!valid_username(name)){
		send_form(client, 400, 1, name, "name: 3 to 20 letters, digits, _ or -");
		return;
	}
	if (query_get(req->body, "password", password, sizeof(password)) < 0
	 || strlen(password) < PASSWORD_MIN){
		send_form(client, 400, 1, name, "password: 8 to 128 characters");
		return;
	}

	char hash[CRYPT_OUTPUT_SIZE];
	if (password_hash(password, hash, sizeof(hash)) < 0){
		send_error(client, 500);
		return;
	}

	sqlite3 *db = db_open();
	if (db == NULL){
		send_error(client, 500);
		return;
	}

	long id = db_user_create(db, name, hash);
	if (id == 0){
		send_form(client, 409, 1, name, "this name is already taken");
	}
	else if (id < 0){
		send_error(client, 500);
	}
	else {
		start_session(client, db, id);
	}
	sqlite3_close(db);
}

void auth_login(int client, Request *req){
	sqlite3 *db = db_open();
	if (db == NULL){
		send_error(client, 500);
		return;
	}

	// trop d'échecs récents depuis cette adresse : on refuse sans même vérifier
	long now = (long)time(NULL);
	int failures = db_login_failures(db, req->ip, now - FAILURE_WINDOW);
	if (failures < 0){
		send_error(client, 500);
		sqlite3_close(db);
		return;
	}
	if (failures >= MAX_FAILURES){
		send_form(client, 429, 0, "", "too many failed attempts, try again in 15 minutes");
		sqlite3_close(db);
		return;
	}

	char name[64];
	char password[PASSWORD_MAX + 1];
	int has_name = query_get(req->body, "name", name, sizeof(name)) == 0;
	int has_password = query_get(req->body, "password", password, sizeof(password)) == 0;

	User user;
	char hash[CRYPT_OUTPUT_SIZE];
	int found = has_name && db_user_find(db, name, &user, hash, sizeof(hash)) == 1;

	// le hachage est calculé même si le nom n'existe pas (même durée de réponse)
	int valid = has_password && password_verify(password, found ? hash : NULL) && found;

	if (valid){
		start_session(client, db, user.id);
	}
	else {
		db_login_failure_add(db, req->ip, now, now - FAILURE_WINDOW);
		// même message que le nom ou le mot de passe soit faux : on ne révèle
		// pas quels comptes existent
		send_form(client, 401, 0, has_name ? name : "", "invalid name or password");
	}
	sqlite3_close(db);
}

void auth_logout(int client, Request *req){
	// la session est supprimée côté serveur : même un cookie copié ne marche plus
	char token[TOKEN_HEX + 1];
	if (cookie_get(req, COOKIE_NAME, token, sizeof(token)) == 0){
		sqlite3 *db = db_open();
		if (db != NULL){
			db_session_delete(db, token);
			sqlite3_close(db);
		}
	}

	// Max-Age=0 : le navigateur efface le cookie
	send_redirect(client, "/guestbook",
		"Set-Cookie: " COOKIE_NAME "=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax\r\n");
}

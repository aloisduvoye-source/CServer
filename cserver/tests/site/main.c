// site de test du framework : chaque route exerce une fonctionnalité de cserver.
// Il utilise les valeurs par défaut (www/, templates/, layout.html du framework).
#include<stdio.h>
#include<string.h>
#include<unistd.h>
#include"cserver.h"

extern const cs_migration site_migrations[];

static void send_text(cs_ctx *c, const char *text){
	cs_send(c, 200, "text/plain; charset=utf-8", text, strlen(text));
}

// GET /echo?q=...  ou  POST /echo avec le corps q=... : renvoie q décodé
static void echo(cs_ctx *c){
	char q[256];
	int found = strcmp(c->req.method, "POST") == 0 ? cs_form(c, "q", q, sizeof(q)) : cs_query(c, "q", q, sizeof(q));
	send_text(c, found == 0 ? q : "(absent)");
}

// GET /p/:a/x/:b
static void params(cs_ctx *c){
	char a[64] = "-", b[64] = "-", out[256];
	cs_param(c, "a", a, sizeof(a));
	cs_param(c, "b", b, sizeof(b));
	snprintf(out, sizeof(out), "a=%s b=%s id=%ld", a, b, cs_param_id(c, "a"));
	send_text(c, out);
}

// GET /lent : ne répondrait qu'au bout de 10 s ; request_timeout l'arrête avant
static void slow(cs_ctx *c){
	sleep(10);
	send_text(c, "fini");
}

// des handlers mal écrits : le framework doit rattraper
static void forgets(cs_ctx *c){
	(void)c;
}

static void twice(cs_ctx *c){
	send_text(c, "un");
	send_text(c, "deux");
}

// GET /vars : le moteur de modèles (templates/vars.html dans le layout du framework)
static void vars(cs_ctx *c){
	cs_vars v;
	cs_vars_init(&v);
	cs_set(&v, "title", "vars");
	cs_set(&v, "texte", "<b>gras</b> & \"guillemets\"");
	cs_set_html(&v, "html", "<i>déjà du HTML</i>");
	cs_set(&v, "plein", "1");
	cs_set(&v, "vide", "");
	cs_set_long(&v, "nombre", 42);
	cs_page(c, 200, "vars.html", &v);
}

static void broken(cs_ctx *c){
	cs_page(c, 200, "casse.html", NULL);
}

static void missing(cs_ctx *c){
	cs_page(c, 200, "nexiste-pas.html", NULL);
}

// GET /json?q=... : {"q":"..."}
static void json(cs_ctx *c){
	char q[256];
	if (cs_query(c, "q", q, sizeof(q)) < 0){
		cs_json_error(c, 400, "q manquant");
		return;
	}
	cs_buf b;
	cs_buf_init(&b);
	cs_buf_append(&b, "{\"q\":");
	cs_buf_append_json(&b, q);
	cs_buf_append(&b, "}\n");
	cs_json(c, 200, &b);
}

// GET /prive : réservé aux personnes connectées
static void private_page(cs_ctx *c){
	const cs_user *user = cs_require_user(c);
	if (user == NULL){return;}
	char out[64];
	snprintf(out, sizeof(out), "bonjour %s", user->name);
	send_text(c, out);
}

// POST /notes texte=... et GET /notes (le nombre de notes) : la table du site
static void notes_add(cs_ctx *c){
	char texte[256];
	sqlite3 *db = cs_db(c);
	sqlite3_stmt *stmt = db != NULL ? cs_db_prepare(db, "INSERT INTO notes (texte) VALUES (?);") : NULL;
	if (cs_form(c, "texte", texte, sizeof(texte)) < 0 || stmt == NULL){
		if (stmt != NULL){sqlite3_finalize(stmt);}
		cs_error(c, 400);
		return;
	}
	sqlite3_bind_text(stmt, 1, texte, -1, SQLITE_STATIC);
	if (cs_db_run(db, stmt) < 0){
		cs_error(c, 500);
		return;
	}
	send_text(c, "ok");
}

static void notes_count(cs_ctx *c){
	sqlite3 *db = cs_db(c);
	sqlite3_stmt *stmt = db != NULL ? cs_db_prepare(db, "SELECT COUNT(*) FROM notes;") : NULL;
	if (stmt == NULL || sqlite3_step(stmt) != SQLITE_ROW){
		if (stmt != NULL){sqlite3_finalize(stmt);}
		cs_error(c, 500);
		return;
	}
	char out[32];
	snprintf(out, sizeof(out), "%d", sqlite3_column_int(stmt, 0));
	sqlite3_finalize(stmt);
	send_text(c, out);
}

// POST /api/login : une connexion écrite par le site, avec les briques du framework
static void api_login(cs_ctx *c){
	char name[64], password[129];
	int has_name = cs_form(c, "name", name, sizeof(name)) == 0;
	int has_password = cs_form(c, "password", password, sizeof(password)) == 0;

	cs_user user;
	switch (cs_user_login(c, has_name ? name : NULL, has_password ? password : NULL, &user)){
		case CS_LOGIN_OK: {
			if (cs_session_start(c, user.id) < 0){
				cs_json_error(c, 500, "session");
				return;
			}
			cs_buf b;
			cs_buf_init(&b);
			cs_buf_append(&b, "{\"user\":");
			cs_buf_append_json(&b, user.name);
			cs_buf_append(&b, "}\n");
			cs_json(c, 200, &b);
			return;
		}
		case CS_LOGIN_WRONG:   cs_json_error(c, 401, "wrong name or password"); return;
		case CS_LOGIN_BLOCKED: cs_json_error(c, 429, "too many attempts");      return;
		default:               cs_json_error(c, 500, "error");
	}
}

static void api_logout(cs_ctx *c){
	cs_session_end(c);
	const char *body = "{\"ok\":true}\n";
	cs_send(c, 200, "application/json", body, strlen(body));
}

// GET /entete : cs_header, dont une tentative d'injection qui doit être refusée
static void headers(cs_ctx *c){
	cs_header(c, "X-Test: oui");
	cs_header(c, "X-Mauvais: a\r\nX-Injecte: 1");
	send_text(c, "entetes");
}

// GET /hash?p=... : cs_password_hash puis cs_password_verify
static void hash(cs_ctx *c){
	char p[64], h[CS_HASH_MAX];
	if (cs_query(c, "p", p, sizeof(p)) < 0 || cs_password_hash(p, h, sizeof(h)) < 0){
		cs_error(c, 400);
		return;
	}
	int ok = cs_password_verify(p, h) && !cs_password_verify("autre chose", h)
	      && !cs_password_verify(p, NULL) && strncmp(h, "$y$", 3) == 0;
	send_text(c, ok ? "hash ok" : "hash faux");
}

static const cs_route routes[] = {
	CS_POST("/api/login",  api_login),
	CS_POST("/api/logout", api_logout),
	CS_GET ("/entete",     headers),
	CS_GET ("/hash",       hash),
	CS_PAGE("/", "index.html", "accueil"),
	CS_PAGE("/sous", "sous/dossier/page.html", "sous"),
	CS_GET ("/echo",       echo),
	CS_POST("/echo",       echo),
	CS_GET ("/p/:a/x/:b",  params),
	CS_GET ("/oublie",     forgets),
	CS_GET ("/lent",       slow),
	CS_GET ("/deux",       twice),
	CS_GET ("/vars",       vars),
	CS_GET ("/casse",      broken),
	CS_GET ("/absent",     missing),
	CS_GET ("/json",       json),
	CS_GET ("/prive",      private_page),
	CS_GET ("/notes",      notes_count),
	CS_POST("/notes",      notes_add),
	// chemins à lui (login_path, signup_path) : les routes de comptes sont déclarées
	// à la main, CS_AUTH_ROUTES utilisant les chemins par défaut
	CS_GET ("/inscription", cs_signup_page),
	CS_POST("/inscription", cs_signup),
	CS_GET ("/connexion",   cs_login_page),
	CS_POST("/connexion",   cs_login),
	CS_POST("/logout",      cs_logout),
	CS_END
};

int main(void){
	cs_app app = {
		.name        = "test",
		.migrations  = site_migrations,
		.routes      = routes,
		.accounts    = 1,
		.cookie_name = "test_session",
		.after_login = "/prive",

		// des règles différentes des valeurs par défaut, pour vérifier qu'elles s'appliquent
		.username_max   = 12,
		.password_min   = 10,
		.login_attempts = 3,
		.login_window   = 600,
		.login_path     = "/connexion",
		.signup_path    = "/inscription",

		// des limites courtes, pour que les tests aillent vite
		.max_connections = 4,
		.read_timeout    = 2,
		.request_timeout = 3,
	};
	return cs_run(&app);
}

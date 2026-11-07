// site de test du framework : chaque route exerce une fonctionnalité de cserver.
// Il utilise les valeurs par défaut (www/, templates/, layout.html du framework).
#include<stdio.h>
#include<string.h>
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

static const cs_route routes[] = {
	CS_PAGE("/", "index.html", "accueil"),
	CS_GET ("/echo",       echo),
	CS_POST("/echo",       echo),
	CS_GET ("/p/:a/x/:b",  params),
	CS_GET ("/oublie",     forgets),
	CS_GET ("/deux",       twice),
	CS_GET ("/vars",       vars),
	CS_GET ("/casse",      broken),
	CS_GET ("/absent",     missing),
	CS_GET ("/json",       json),
	CS_GET ("/prive",      private_page),
	CS_GET ("/notes",      notes_count),
	CS_POST("/notes",      notes_add),
	CS_AUTH_ROUTES,
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
	};
	return cs_run(&app);
}

#include"cserver.h"
#include"guestbook.h"

// généré par le Makefile depuis migrations/*.sql
extern const cs_migration site_migrations[];

// page 404 du site : templates/404.html
static void not_found(cs_ctx *c){
	cs_vars v;
	cs_vars_init(&v);
	cs_set(&v, "title", "404");
	cs_page(c, 404, "404.html", &v);
}

// ajouter une page = ajouter une ligne ici
static const cs_route routes[] = {
	// pages sans code C : un modèle de templates/ placé dans le layout
	CS_PAGE("/",      "index.html", NULL),
	CS_PAGE("/about", "about.html", "about"),

	CS_GET ("/guestbook",        guestbook_list),
	CS_POST("/guestbook",        guestbook_post),
	CS_POST("/guestbook/delete", guestbook_delete),
	CS_GET ("/api/messages",     api_messages_list),
	CS_POST("/api/messages",     api_messages_post),

	// inscription, connexion, déconnexion : fournies par le framework
	CS_AUTH_ROUTES,

	CS_END
};

int main(void){
	cs_app app = {
		.name          = "cServer",
		.port          = 8080,
		.static_dir    = "www",
		.templates_dir = "templates",
		.db_path       = "data/site.db",
		.migrations    = site_migrations,
		.routes        = routes,
		.not_found     = not_found,
		.cookie_name   = "cserver_session",
		.after_login   = "/guestbook",
	};
	return cs_run(&app);
}

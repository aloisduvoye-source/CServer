#include<string.h>
#include"cserver.h"
#include"guestbook.h"

// page 404 du site
static void not_found(int client, Request *req){
	(void)req;
	const char *body = "<h1>404 dumbass</h1>\n";
	send_response(client, 404, "text/html", body, strlen(body));
}

// ajouter une page = ajouter une ligne ici
static const cs_route routes[] = {
	{ "GET",  "/guestbook",        guestbook_list },
	{ "POST", "/guestbook",        guestbook_post },
	{ "POST", "/guestbook/delete", guestbook_delete },
	{ "GET",  "/api/messages",     api_messages_list },
	{ "POST", "/api/messages",     api_messages_post },

	// comptes : les pages et actions sont fournies par le framework
	{ "GET",  "/signup",           auth_signup_page },
	{ "POST", "/signup",           auth_signup },
	{ "GET",  "/login",            auth_login_page },
	{ "POST", "/login",            auth_login },
	{ "POST", "/logout",           auth_logout },

	{ NULL, NULL, NULL }
};

int main(void){
	cs_app app = {
		.name        = "cServer",
		.port        = 8080,
		.static_dir  = "www",
		.db_path     = "data/site.db",
		.routes      = routes,
		.not_found   = not_found,
		.cookie_name = "cserver_session",
		.after_login = "/guestbook",
	};
	return cs_run(&app);
}

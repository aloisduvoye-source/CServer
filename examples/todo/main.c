#include"cserver.h"
#include"todos.h"

// généré par le Makefile depuis migrations/*.sql
extern const cs_migration site_migrations[];

static const cs_route routes[] = {
	CS_PAGE("/", "index.html", NULL),

	CS_GET ("/todos",            todos_list),
	CS_POST("/todos",            todos_create),
	CS_POST("/todos/:id/done",   todos_toggle),
	CS_POST("/todos/:id/delete", todos_remove),

	CS_AUTH_ROUTES,
	CS_END
};

// port, www/, templates/ et data/site.db : les valeurs par défaut
int main(void){
	cs_app app = {
		.name        = "todo",
		.migrations  = site_migrations,
		.routes      = routes,
		.cookie_name = "todo_session",
		.after_login = "/todos",
	};
	return cs_run(&app);
}

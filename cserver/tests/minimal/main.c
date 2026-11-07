// le plus petit site : pas de base, pas de comptes, que des valeurs par défaut
#include"cserver.h"

static void hello(cs_ctx *c){
	cs_vars v;
	cs_vars_init(&v);
	cs_set(&v, "title", "minimal");
	cs_set(&v, "db", cs_db(c) != NULL ? "oui" : "non");
	cs_page(c, 200, "hello.html", &v);
}

static const cs_route routes[] = {
	CS_GET("/", hello),
	CS_END
};

int main(void){
	cs_app app = { .routes = routes };
	return cs_run(&app);
}

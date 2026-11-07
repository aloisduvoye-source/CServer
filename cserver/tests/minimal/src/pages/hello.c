// la page d'accueil du site minimal, rangée dans src/pages/
#include"pages/hello.h"
#include"texte/format/greet.h"

const char *util_a(void);
const char *util_b(void);

void hello(cs_ctx *c){
	cs_vars v;
	cs_vars_init(&v);
	cs_set(&v, "title", "minimal");
	cs_set(&v, "db", cs_db(c) != NULL ? "oui" : "non");
	cs_set(&v, "greeting", greeting());
	cs_set(&v, "utils", util_a()[0] == 'a' && util_b()[0] == 'b' ? "a+b" : "?");
	cs_page(c, 200, "hello.html", &v);
}

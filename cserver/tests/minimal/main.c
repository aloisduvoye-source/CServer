// le plus petit site : pas de base, pas de comptes, que des valeurs par défaut.
// Son code est rangé dans src/ (sur plusieurs niveaux), pour tester cserver.mk.
#include"cserver.h"
#include"pages/hello.h"

static const cs_route routes[] = {
	CS_GET("/", hello),
	CS_END
};

int main(void){
	cs_app app = { .routes = routes };
	return cs_run(&app);
}

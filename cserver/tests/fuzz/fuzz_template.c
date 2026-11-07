// Fuzzing du moteur de modèles : un modèle mal écrit (balise jamais fermée, sections
// imbriquées n'importe comment...) doit donner une erreur, jamais un plantage.
#include<stdint.h>
#include<string.h>
#include<stdlib.h>
#include"internal.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size){
	// la configuration que cs_run() aurait remplie
	cs_config.name = "fuzz";
	cs_config.login_path = "/login";
	cs_config.signup_path = "/signup";

	cs_ctx c;
	memset(&c, 0, sizeof(c));

	cs_vars v;
	cs_vars_init(&v);
	cs_set(&v, "title", "<b>titre</b>");
	cs_set(&v, "plein", "1");
	cs_set(&v, "vide", "");
	cs_set_html(&v, "html", "<i>brut</i>");
	cs_set_long(&v, "n", 42);

	cs_buf out;
	cs_buf_init(&out);
	template_render_text(&c, &out, (const char *)data, size, &v);
	cs_buf_free(&out);
	cs_vars_free(&v);
	return 0;
}

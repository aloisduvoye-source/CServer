// les pages de comptes prêtes à l'emploi (CS_AUTH_ROUTES), écrites uniquement
// avec les briques publiques d'accounts.c : un site peut faire les siennes pareil.
#include<string.h>
#include<ctype.h>
#include"internal.h"

#define USERNAME_MIN 3
#define USERNAME_MAX 20
#define PASSWORD_MIN 8
#define PASSWORD_MAX 128

// formulaire d'inscription (signup = 1) ou de connexion (signup = 0) : modèles
// signup.html et login.html (ceux du framework, ou ceux du site s'il en a)
// name : valeur à réafficher dans le champ ; error : message, ou NULL
static void send_form(cs_ctx *c, int status, int signup, const char *name, const char *error){
	cs_vars v;
	cs_vars_init(&v);
	cs_set(&v, "title", signup ? "sign up" : "log in");
	cs_set(&v, "name", name);
	if (error != NULL){cs_set(&v, "error", error);}
	cs_page(c, status, signup ? "signup.html" : "login.html", &v);
}

void cs_signup_page(cs_ctx *c){
	send_form(c, 200, 1, "", NULL);
}

void cs_login_page(cs_ctx *c){
	send_form(c, 200, 0, "", NULL);
}

// 3 à 20 caractères parmi a-z A-Z 0-9 _ - : pas d'espaces, pas de sosies Unicode
static int valid_username(const char *s){
	size_t n = strlen(s);
	if (n < USERNAME_MIN || n > USERNAME_MAX){return 0;}
	for (; *s != '\0'; s++){
		if (!isalnum((unsigned char)*s) && *s != '_' && *s != '-'){return 0;}
	}
	return 1;
}

void cs_signup(cs_ctx *c){
	char name[64];
	char password[PASSWORD_MAX + 1];

	// attention : si cs_form échoue, le buffer n'est pas une chaîne valide
	if (cs_form(c, "name", name, sizeof(name)) < 0){
		send_form(c, 400, 1, "", "name: 3 to 20 letters, digits, _ or -");
		return;
	}
	if (!valid_username(name)){
		send_form(c, 400, 1, name, "name: 3 to 20 letters, digits, _ or -");
		return;
	}
	if (cs_form(c, "password", password, sizeof(password)) < 0 || strlen(password) < PASSWORD_MIN){
		send_form(c, 400, 1, name, "password: 8 to 128 characters");
		return;
	}

	long id = cs_user_create(c, name, password);
	if (id == 0){
		send_form(c, 409, 1, name, "this name is already taken");
	}
	else if (id < 0 || cs_session_start(c, id) < 0){
		cs_error(c, 500);
	}
	else {
		cs_redirect(c, cs_config.after_login);
	}
}

void cs_login(cs_ctx *c){
	char name[64];
	char password[PASSWORD_MAX + 1];
	int has_name = cs_form(c, "name", name, sizeof(name)) == 0;
	int has_password = cs_form(c, "password", password, sizeof(password)) == 0;

	cs_user user;
	switch (cs_user_login(c, has_name ? name : NULL, has_password ? password : NULL, &user)){
		case CS_LOGIN_OK:
			if (cs_session_start(c, user.id) < 0){
				cs_error(c, 500);
				return;
			}
			cs_redirect(c, cs_config.after_login);
			return;

		case CS_LOGIN_BLOCKED:
			send_form(c, 429, 0, "", "too many failed attempts, try again in 15 minutes");
			return;

		case CS_LOGIN_WRONG:
			// même message que le nom ou le mot de passe soit faux : on ne révèle
			// pas quels comptes existent
			send_form(c, 401, 0, has_name ? name : "", "invalid name or password");
			return;

		default:
			cs_error(c, 500);
	}
}

void cs_logout(cs_ctx *c){
	cs_session_end(c);
	cs_redirect(c, cs_config.after_login);
}

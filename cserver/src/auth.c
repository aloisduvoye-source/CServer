// les pages de comptes prêtes à l'emploi (CS_AUTH_ROUTES), écrites uniquement
// avec les briques publiques d'accounts.c : un site peut faire les siennes pareil.
// Les règles (longueurs, tentatives, chemins) viennent de cs_app.
#include<stdio.h>
#include<string.h>
#include<ctype.h>
#include"internal.h"

#define PASSWORD_MAX 128

// formulaire d'inscription (signup = 1) ou de connexion (signup = 0) : modèles
// signup.html et login.html (ceux du framework, ou ceux du site s'il en a)
// name : valeur à réafficher dans le champ ; error : message, ou NULL
static void send_form(cs_ctx *c, int status, int signup, const char *name, const char *error){
	cs_vars v;
	cs_vars_init(&v);
	cs_set(&v, "title", signup ? "sign up" : "log in");
	cs_set(&v, "name", name);
	cs_set_long(&v, "password_min", cs_config.password_min);
	if (error != NULL){cs_set(&v, "error", error);}
	cs_page(c, status, signup ? "signup.html" : "login.html", &v);
}

void cs_signup_page(cs_ctx *c){
	send_form(c, 200, 1, "", NULL);
}

void cs_login_page(cs_ctx *c){
	send_form(c, 200, 0, "", NULL);
}

// lettres, chiffres, _ et - seulement : pas d'espaces, pas de sosies Unicode
static int valid_username(const char *s){
	size_t n = strlen(s);
	if (n < (size_t)cs_config.username_min || n > (size_t)cs_config.username_max){return 0;}
	for (; *s != '\0'; s++){
		if (!isalnum((unsigned char)*s) && *s != '_' && *s != '-'){return 0;}
	}
	return 1;
}

void cs_signup(cs_ctx *c){
	char name[64];
	char password[PASSWORD_MAX + 1];
	char error[96];

	// attention : si cs_form échoue, le buffer n'est pas une chaîne valide
	int has_name = cs_form(c, "name", name, sizeof(name)) == 0;
	if (!has_name || !valid_username(name)){
		snprintf(error, sizeof(error), "name: %d to %d letters, digits, _ or -",
			cs_config.username_min, cs_config.username_max);
		send_form(c, 400, 1, has_name ? name : "", error);
		return;
	}
	if (cs_form(c, "password", password, sizeof(password)) < 0
	 || strlen(password) < (size_t)cs_config.password_min){
		snprintf(error, sizeof(error), "password: %d to %d characters", cs_config.password_min, PASSWORD_MAX);
		send_form(c, 400, 1, name, error);
		return;
	}

	// contre la création de comptes en masse (et le hachage, volontairement lent,
	// qu'on ferait calculer en boucle au serveur)
	if (!cs_rate_limit(c, "signup", NULL, cs_config.signup_limit, cs_config.signup_window)){
		send_form(c, 429, 1, name, "too many sign-ups from this address, try again later");
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
	char error[96];
	switch (cs_user_login(c, has_name ? name : NULL, has_password ? password : NULL, &user)){
		case CS_LOGIN_OK:
			if (cs_session_start(c, user.id) < 0){
				cs_error(c, 500);
				return;
			}
			cs_redirect(c, cs_config.after_login);
			return;

		case CS_LOGIN_BLOCKED: {
			int minutes = (cs_config.login_window + 59) / 60;
			snprintf(error, sizeof(error), "too many failed attempts, try again in %d minute%s",
				minutes, minutes > 1 ? "s" : "");
			send_form(c, 429, 0, "", error);
			return;
		}

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

#include<stdio.h>
#include<string.h>
#include<ctype.h>
#include"account.h"
#include"members.h"

#define NAME_MIN     3
#define NAME_MAX     20
#define PASSWORD_MIN 8
#define PASSWORD_MAX 128

/* ---------------------------------------------------------------- formulaires */

// les valeurs déjà tapées sont réaffichées (sauf le mot de passe) ; error : ou NULL
static void signup_form(cs_ctx *c, int status, const char *name, const char *email, const char *error){
	cs_vars v;
	cs_vars_init(&v);
	cs_set(&v, "title", "sign up");
	cs_set(&v, "name", name);
	cs_set(&v, "email", email);
	cs_set_long(&v, "password_min", PASSWORD_MIN);
	if (error != NULL){cs_set(&v, "error", error);}
	cs_page(c, status, "signup.html", &v);
}

static void login_form(cs_ctx *c, int status, const char *login, const char *error){
	cs_vars v;
	cs_vars_init(&v);
	cs_set(&v, "title", "log in");
	cs_set(&v, "login", login);
	if (error != NULL){cs_set(&v, "error", error);}
	cs_page(c, status, "login.html", &v);
}

void account_signup_page(cs_ctx *c){signup_form(c, 200, "", "", NULL);}
void account_login_page(cs_ctx *c) {login_form(c, 200, "", NULL);}

/* ---------------------------------------------------------------- inscription */

// lettres, chiffres, _ et - : le nom apparaît dans les URL (/u/jean), sans encodage
static int valid_name(const char *s){
	size_t n = strlen(s);
	if (n < NAME_MIN || n > NAME_MAX){return 0;}
	for (; *s != '\0'; s++){
		if (!isalnum((unsigned char)*s) && *s != '_' && *s != '-'){return 0;}
	}
	return 1;
}

void account_signup(cs_ctx *c){
	char name[64], email[EMAIL_MAX + 2], password[PASSWORD_MAX + 1];
	// si cs_form échoue, le buffer n'est pas une chaîne valide : on le vide
	if (cs_form(c, "name", name, sizeof(name)) < 0){name[0] = '\0';}
	if (cs_form(c, "email", email, sizeof(email)) < 0){email[0] = '\0';}

	if (!valid_name(name)){
		signup_form(c, 400, name, email, "name: 3 to 20 letters, digits, _ or -");
		return;
	}
	if (!email_valid(email)){
		signup_form(c, 400, name, email, "invalid email address");
		return;
	}
	if (cs_form(c, "password", password, sizeof(password)) < 0 || strlen(password) < PASSWORD_MIN){
		signup_form(c, 400, name, email, "password: 8 to 128 characters");
		return;
	}
	// le même compteur que les pages d'inscription du framework : 5 par heure et par IP
	if (!cs_rate_limit(c, "signup", NULL, 5, 3600)){
		signup_form(c, 429, name, email, "too many sign-ups from this address, try again later");
		return;
	}

	sqlite3 *db = cs_db(c);
	if (db == NULL){
		cs_error(c, 500);
		return;
	}

	// le compte et son profil sont créés ensemble, ou pas du tout : si l'e-mail est
	// refusé, il ne doit pas rester un compte sans profil. Une transaction : tout ce qui
	// est écrit entre BEGIN et COMMIT est annulé par ROLLBACK.
	if (sqlite3_exec(db, "BEGIN;", NULL, NULL, NULL) != SQLITE_OK){
		cs_error(c, 500);
		return;
	}
	long id = cs_user_create(c, name, password);              // 0 : nom déjà pris
	int profile = id > 0 ? profile_set_email(db, id, email) : -1;  // 0 : e-mail déjà pris
	if (id > 0 && profile == 1 && sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL) == SQLITE_OK){
		if (cs_session_start(c, id) < 0){
			cs_error(c, 500);
			return;
		}
		cs_redirect(c, "/profile");
		return;
	}
	sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);

	// dire que l'e-mail est pris révèle qu'il a un compte ici : c'est le prix d'une
	// inscription simple (sinon, il faudrait passer par un e-mail de confirmation)
	if (id == 0){signup_form(c, 409, name, email, "this name is already taken");}
	else if (profile == 0){signup_form(c, 409, name, email, "this email is already used");}
	else {cs_error(c, 500);}
}

/* ---------------------------------------------------------------- connexion */

void account_login(cs_ctx *c){
	char login[EMAIL_MAX + 2], password[PASSWORD_MAX + 1], name[32];
	int has_login = cs_form(c, "login", login, sizeof(login)) == 0;
	int has_password = cs_form(c, "password", password, sizeof(password)) == 0;
	if (!has_login){login[0] = '\0';}

	// un e-mail : on cherche le nom du compte. Introuvable : on appelle quand même
	// cs_user_login (avec NULL), pour la même durée de réponse et le compte des échecs
	const char *as_name = NULL;
	if (has_login && strchr(login, '@') != NULL){
		sqlite3 *db = cs_db(c);
		if (db != NULL && profile_name_by_email(db, login, name, sizeof(name)) == 1){as_name = name;}
	}
	else if (has_login){
		as_name = login;
	}

	cs_user user;
	switch (cs_user_login(c, as_name, has_password ? password : NULL, &user)){
		case CS_LOGIN_OK:
			if (cs_session_start(c, user.id) < 0){
				cs_error(c, 500);
				return;
			}
			cs_redirect(c, "/profile");
			return;
		case CS_LOGIN_BLOCKED:
			login_form(c, 429, "", "too many failed attempts, try again in 15 minutes");
			return;
		case CS_LOGIN_WRONG:
			// le même message dans tous les cas : on ne révèle pas ce qui est faux
			login_form(c, 401, login, "invalid name, email or password");
			return;
		default:
			cs_error(c, 500);
	}
}

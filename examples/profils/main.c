// Exemple : des membres avec un e-mail et un avatar.
//
// Les comptes du framework (cs_users : nom, mot de passe, sessions) sont complétés
// par les tables du site (migrations/001_profils.sql). Les pages d'inscription et de
// connexion sont celles du site (account.c), écrites avec les briques du framework.
#include"cserver.h"
#include"account.h"
#include"profile.h"

// généré par le Makefile depuis migrations/*.sql
extern const cs_migration site_migrations[];

static const cs_route routes[] = {
	CS_GET ("/",                      members_page),
	CS_GET ("/u/:name",               member_page),
	CS_GET ("/avatars/:id",           avatar_image),

	CS_GET ("/profile",               profile_page),
	CS_POST("/profile/email",         profile_email),
	CS_POST("/profile/avatar",        profile_avatar),
	CS_POST("/profile/avatar/delete", profile_avatar_delete),

	// nos propres pages de comptes, aux chemins par défaut (login_path, signup_path)
	CS_GET ("/signup",                account_signup_page),
	CS_POST("/signup",                account_signup),
	CS_GET ("/login",                 account_login_page),
	CS_POST("/login",                 account_login),
	CS_POST("/logout",                cs_logout),          // celle du framework suffit

	CS_END
};

int main(void){
	cs_app app = {
		.name        = "members",
		.port        = 8080,
		.migrations  = site_migrations,
		.routes      = routes,
		.accounts    = 1,
		.cookie_name = "profils_session",
		.after_login = "/",                // après la déconnexion (cs_logout)
	};
	return cs_run(&app);
}

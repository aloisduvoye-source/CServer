#ifndef ACCOUNT_H
#define ACCOUNT_H

// inscription (nom, e-mail, mot de passe) et connexion (nom ou e-mail), écrites avec
// les briques du framework : cs_user_create, cs_user_login, cs_session_start

#include"cserver.h"

void account_signup_page(cs_ctx *c);
void account_signup(cs_ctx *c);
void account_login_page(cs_ctx *c);
void account_login(cs_ctx *c);

#endif

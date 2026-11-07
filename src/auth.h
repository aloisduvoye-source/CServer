#ifndef AUTH_H
#define AUTH_H

#include"http.h"
#include"db.h"

// lit le cookie de session et cherche l'utilisateur ; 1 si connecté, 0 sinon
int auth_current_user(sqlite3 *db, const Request *req, User *user);

void auth_signup_page(int client, Request *req);   // GET  /signup
void auth_signup(int client, Request *req);        // POST /signup -> 303 + cookie
void auth_login_page(int client, Request *req);    // GET  /login
void auth_login(int client, Request *req);         // POST /login  -> 303 + cookie
void auth_logout(int client, Request *req);        // POST /logout -> 303, cookie effacé

#endif

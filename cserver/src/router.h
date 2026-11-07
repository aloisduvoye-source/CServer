#ifndef ROUTER_H
#define ROUTER_H

#include"http.h"

// cherche la route (méthode, chemin) du site ; sinon sert un fichier statique,
// sinon la page 404
void route(int client, Request *req);

#endif

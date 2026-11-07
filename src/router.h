#ifndef ROUTER_H
#define ROUTER_H

#include"http.h"

// appelle le handler qui correspond à (méthode, chemin), sinon sert un fichier
void route(int client, Request *req);

#endif

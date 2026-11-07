#ifndef PAGE_H
#define PAGE_H

#include"buffer.h"
#include"db.h"

// début commun des pages générées par le serveur : <head>, style, navigation
// user : utilisateur connecté, ou NULL ; script : chemin d'un .js, ou NULL
void page_begin(Buffer *b, const char *title, const User *user, const char *script);
void page_end(Buffer *b);

// envoie la page (ou une 500 si une allocation a échoué), puis la libère
void page_send(int client, int status, Buffer *b);

#endif

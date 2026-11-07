#ifndef GUESTBOOK_H
#define GUESTBOOK_H

#include"http.h"

void guestbook_list(int client, Request *req);   // GET  : formulaire + messages
void guestbook_post(int client, Request *req);   // POST : enregistre puis redirige

#endif

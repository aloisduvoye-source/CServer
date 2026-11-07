#ifndef STATIC_H
#define STATIC_H

#include"http.h"

// sert le fichier www/<path> (ou www/index.html pour "/")
void static_serve(int client, Request *req);

#endif

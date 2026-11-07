#ifndef STATIC_H
#define STATIC_H

#include"http.h"

// sert le fichier <static_dir>/<path> (ou index.html pour "/")
// renvoie 1 si le fichier a été envoyé, 0 s'il n'existe pas
int static_serve(int client, Request *req);

#endif

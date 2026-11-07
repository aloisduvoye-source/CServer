#ifndef CONFIG_H
#define CONFIG_H

// en-tête interne au framework : les sites n'y ont pas accès

#include"cserver.h"

// configuration du site, complétée avec les valeurs par défaut par cs_run()
// (variable globale : chaque processus enfant en reçoit une copie au fork)
extern cs_app cs_config;

#endif

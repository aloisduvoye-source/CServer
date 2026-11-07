# CServer

Serveur HTTP en C, tout à la main (bibliothèques : SQLite, et libcrypt pour les mots de passe).

## Compiler et lancer

```sh
sudo apt install libsqlite3-dev libcrypt-dev   # une seule fois
make
./server
```

Depuis la racine du projet, puis ouvrir http://localhost:8080.

Tests : `./test.sh` (sur le port 8081, en parallèle du serveur de dev)

## Plan

Objectif : un livre d'or (formulaire + liste des messages).

- [x] Fichiers statiques
- [x] Plusieurs clients en parallèle (`fork`)
- [x] Routes dynamiques
- [x] Paramètres d'URL (`?q=...`, décodage `%XX`)
- [x] Requêtes POST (lecture selon `Content-Length`)
- [x] Base de données SQLite
- [x] Échappement HTML (XSS)
- [x] Livre d'or (enregistrer et afficher les messages)
- [x] API JSON
- [x] Sessions et cookies

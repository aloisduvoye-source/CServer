# CServer

Serveur HTTP en C, tout à la main (seule bibliothèque : SQLite).

## Compiler et lancer

```sh
sudo apt install libsqlite3-dev   # une seule fois
make
./server
```

Depuis la racine du projet, puis ouvrir http://localhost:8080.

Tests : `./test.sh`

## Plan

Objectif : un livre d'or (formulaire + liste des messages).

- [x] Fichiers statiques
- [x] Plusieurs clients en parallèle (`fork`)
- [x] Routes dynamiques
- [x] Paramètres d'URL (`?q=...`, décodage `%XX`)
- [x] Requêtes POST (lecture selon `Content-Length`)
- [x] Base de données SQLite
- [x] Échappement HTML (XSS)
- [ ] Livre d'or (enregistrer et afficher les messages)
- [ ] API JSON
- [ ] Sessions et cookies

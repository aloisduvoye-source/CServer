#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<errno.h>
#include<unistd.h>
#include<signal.h>
#include<sys/socket.h>
#include<sys/time.h>
#include<sys/wait.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<pwd.h>
#include<grp.h>
#include"internal.h"

cs_app cs_config;

/* ---------------------------------------------------------------- dans l'enfant */

// la requête a dépassé request_timeout : le noyau envoie SIGALRM, on s'arrête.
// Dans un gestionnaire de signal, seules quelques fonctions sont permises :
// write() et _exit() en font partie, printf() non.
static void on_request_timeout(int sig){
	(void)sig;
	static const char msg[] = "requête trop longue (request_timeout) : processus arrêté\n";
	ssize_t ignored = write(STDERR_FILENO, msg, sizeof(msg) - 1);
	(void)ignored;
	_exit(1);
}

// une connexion ne sert pas indéfiniment : au-delà, le client en ouvre une autre
#define MAX_REQUESTS_PER_CONNECTION 100

// trusted_proxy : des adresses ("127.0.0.1") ou des plages IPv4 ("172.16.0.0/12"),
// séparées par des virgules ou des espaces. Renvoie 1 si `ip` en fait partie, 0 sinon,
// -1 si la liste est mal écrite (vérifié au démarrage).
static int proxy_match(const char *list, const char *ip){
	if (list == NULL){return 0;}
	struct in_addr addr;
	int have_addr = ip != NULL && inet_pton(AF_INET, ip, &addr) == 1;

	char item[64];
	const char *p = list;
	while (*p != '\0'){
		p += strspn(p, ", \t");
		size_t len = strcspn(p, ", \t");
		if (len == 0){break;}
		if (len >= sizeof(item)){return -1;}
		memcpy(item, p, len);
		item[len] = '\0';
		p += len;

		int bits = 32;
		char *slash = strchr(item, '/');
		if (slash != NULL){
			char *end;
			long b = strtol(slash + 1, &end, 10);
			if (slash[1] == '\0' || *end != '\0' || b < 0 || b > 32){return -1;}
			bits = (int)b;
			*slash = '\0';
		}
		struct in_addr net;
		if (inet_pton(AF_INET, item, &net) != 1){return -1;}

		uint32_t mask = bits == 0 ? 0 : htonl(0xFFFFFFFFu << (32 - bits));
		if (have_addr && (addr.s_addr & mask) == (net.s_addr & mask)){return 1;}
	}
	return 0;
}

static void handle_client(int client, const char *ip){
	signal(SIGALRM, on_request_timeout);

	// un client qui ne lit pas ce qu'on lui envoie : write() abandonne au bout de
	// read_timeout secondes sans pouvoir avancer
	struct timeval tv = { .tv_sec = cs_config.read_timeout, .tv_usec = 0 };
	setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	// HTTP/1.1 : plusieurs requêtes à la suite sur la même connexion (la page, puis
	// son CSS, son JS...), au lieu d'une connexion (et d'un fork) par fichier
	static http_conn conn;   // static : 8 Ko qu'il est inutile de mettre sur la pile
	memset(&conn, 0, sizeof(conn));
	conn.fd = client;

	for (int served = 0; served < MAX_REQUESTS_PER_CONNECTION; served++){
		cs_ctx c;
		memset(&c, 0, sizeof(c));
		c.client = client;

		// la première requête a read_timeout pour arriver ; les suivantes peuvent se
		// faire attendre keepalive_timeout, puis ont read_timeout pour arriver en entier
		int keep_alive;
		int idle = served == 0 ? cs_config.read_timeout : cs_config.keepalive_timeout;
		int r = http_parse(&conn, &c.req, idle, cs_config.read_timeout, &keep_alive);
		if (r != 0){
			// une requête illisible : on ne sait plus où commence la suivante, on ferme
			if (r > 0){http_send_error(client, r, "Connection: close\r\n", 1);}
			http_free(&c.req);
			return;
		}

		keep_alive = keep_alive && served + 1 < MAX_REQUESTS_PER_CONNECTION;
		snprintf(c.req.ip, sizeof(c.req.ip), "%s", ip);
		// derrière un proxy, toutes les connexions viennent de lui : l'adresse du
		// visiteur est dans X-Forwarded-For. On ne la croit que si la connexion vient
		// vraiment du proxy ; sinon n'importe qui changerait d'adresse à volonté
		// (et contournerait les limites par IP).
		if (proxy_match(cs_config.trusted_proxy, ip) == 1){
			if (conn.fwd_ip[0] != '\0'){
				snprintf(c.req.ip, sizeof(c.req.ip), "%s", conn.fwd_ip);
			}
			c.req.https = conn.fwd_https;
		}
		c.head = strcmp(c.req.method, "HEAD") == 0;
		cs_header(&c, keep_alive ? "Connection: keep-alive" : "Connection: close");
		printf("[pid %d] %s %s %s%s%s (%zu octets)\n", getpid(), c.req.ip, c.req.method, c.req.path,
			c.req.query[0] ? "?" : "", c.req.query, c.req.body_len);

		// une échéance pour le traitement, handler compris : même une boucle infinie
		// ou une requête SQL qui ne finit pas s'arrêtent. Avec fork(), il suffit d'un
		// appel : seul ce processus est concerné.
		alarm(cs_config.request_timeout);
		route(&c);
		ctx_finish(&c);   // 500 si le handler n'a rien envoyé, ferme la base, libère le corps
		alarm(0);

		if (!keep_alive){return;}
	}
}

// copie la configuration du site et complète ce qu'il n'a pas précisé
static int load_config(const cs_app *app){
	static const cs_route no_routes[] = { CS_END };

	cs_config = *app;
	if (cs_config.name == NULL)        {cs_config.name = "cserver";}
	if (cs_config.port == 0)           {cs_config.port = 8080;}
	if (cs_config.static_dir == NULL)  {cs_config.static_dir = "www";}
	if (cs_config.templates_dir == NULL){cs_config.templates_dir = "templates";}
	if (cs_config.routes == NULL)      {cs_config.routes = no_routes;}
	if (cs_config.cookie_name == NULL) {cs_config.cookie_name = "cs_session";}
	if (cs_config.after_login == NULL) {cs_config.after_login = "/";}
	if (cs_config.username_min == 0)   {cs_config.username_min = 3;}
	if (cs_config.username_max == 0)   {cs_config.username_max = 20;}
	if (cs_config.password_min == 0)   {cs_config.password_min = 8;}
	if (cs_config.login_attempts == 0) {cs_config.login_attempts = 5;}
	if (cs_config.login_window == 0)   {cs_config.login_window = 15 * 60;}
	if (cs_config.signup_limit == 0)   {cs_config.signup_limit = 5;}
	if (cs_config.signup_window == 0)  {cs_config.signup_window = 3600;}
	if (cs_config.login_path == NULL)  {cs_config.login_path = "/login";}
	if (cs_config.signup_path == NULL) {cs_config.signup_path = "/signup";}
	if (cs_config.max_connections == 0){cs_config.max_connections = 64;}
	if (cs_config.read_timeout == 0)   {cs_config.read_timeout = 10;}
	if (cs_config.request_timeout == 0){cs_config.request_timeout = 30;}
	if (cs_config.keepalive_timeout == 0){cs_config.keepalive_timeout = 5;}

	if (cs_config.max_connections < 1 || cs_config.read_timeout < 1 || cs_config.request_timeout < 1
	 || cs_config.keepalive_timeout < 1){
		fprintf(stderr, "max_connections, read_timeout, request_timeout et keepalive_timeout doivent être positifs\n");
		return -1;
	}

	// un nom doit tenir dans cs_user.name
	int name_room = (int)sizeof(((cs_user *)0)->name) - 1;
	if (cs_config.username_min < 1 || cs_config.username_max > name_room
	 || cs_config.username_min > cs_config.username_max || cs_config.password_min < 1
	 || cs_config.login_attempts < 1 || cs_config.login_window < 1
	 || cs_config.signup_limit < 1 || cs_config.signup_window < 1){
		fprintf(stderr, "règles des comptes invalides (noms : 1 à %d caractères)\n", name_room);
		return -1;
	}

	// les variables d'environnement l'emportent : les tests s'en servent pour
	// tourner sur un autre port, avec une base jetable
	const char *port = getenv("CSERVER_PORT");
	if (port != NULL){cs_config.port = atoi(port);}
	if (cs_config.port <= 0 || cs_config.port > 65535){
		fprintf(stderr, "port invalide : %d\n", cs_config.port);
		return -1;
	}

	const char *proxy = getenv("CSERVER_PROXY");
	if (proxy != NULL){cs_config.trusted_proxy = proxy[0] != '\0' ? proxy : NULL;}
	if (proxy_match(cs_config.trusted_proxy, NULL) < 0){
		fprintf(stderr, "trusted_proxy invalide : \"%s\" (attendu : des adresses IPv4 ou "
		                "des plages comme 10.0.0.0/8, séparées par des virgules)\n", cs_config.trusted_proxy);
		return -1;
	}

	const char *host = getenv("CSERVER_HOST");
	if (host != NULL){cs_config.host = host[0] != '\0' ? host : NULL;}
	struct in_addr ignored;
	if (cs_config.host != NULL && inet_pton(AF_INET, cs_config.host, &ignored) != 1){
		fprintf(stderr, "host invalide : \"%s\" (attendu : une adresse IPv4, ex : 127.0.0.1)\n", cs_config.host);
		return -1;
	}

	// une base seulement si le site en a besoin
	int wants_db = cs_config.db_path != NULL || cs_config.migrations != NULL || cs_config.accounts;
	if (wants_db){
		const char *db = getenv("CSERVER_DB");
		if (db != NULL)                {cs_config.db_path = db;}
		if (cs_config.db_path == NULL) {cs_config.db_path = "data/site.db";}
	}

	// les pages de comptes du framework ne marchent pas sans comptes : autant le dire
	// tout de suite plutôt qu'à la première visite
	for (const cs_route *r = cs_config.routes; r->method != NULL; r++){
		int auth_page = r->handler == cs_signup_page || r->handler == cs_signup
		             || r->handler == cs_login_page  || r->handler == cs_login
		             || r->handler == cs_logout;
		if (auth_page && !cs_config.accounts){
			fprintf(stderr, "route %s %s : les pages de comptes demandent .accounts = 1\n", r->method, r->path);
			return -1;
		}

		// les formulaires du framework envoient vers login_path et signup_path
		int login_page = r->handler == cs_login_page || r->handler == cs_login;
		int signup_page = r->handler == cs_signup_page || r->handler == cs_signup;
		if ((login_page && strcmp(r->path, cs_config.login_path) != 0)
		 || (signup_page && strcmp(r->path, cs_config.signup_path) != 0)){
			fprintf(stderr, "route %s %s : ne correspond pas à login_path (%s) ou signup_path (%s)\n",
				r->method, r->path, cs_config.login_path, cs_config.signup_path);
			return -1;
		}
	}
	return 0;
}

/* ---------------------------------------------------------------- dans le parent */

// nombre d'enfants en vie ; modifié par le gestionnaire de SIGCHLD, d'où volatile
// sig_atomic_t (un type que le signal ne peut pas couper en pleine écriture)
static volatile sig_atomic_t children = 0;

// un enfant s'est terminé : on le récupère (sinon il resterait « zombie ») et on
// libère sa place. Plusieurs fins peuvent n'avoir donné qu'un seul signal : on boucle.
static void on_child_exit(int sig){
	(void)sig;
	int saved = errno;   // waitpid peut modifier errno, que le code interrompu utilisait peut-être
	while (waitpid(-1, NULL, WNOHANG) > 0){
		children--;
	}
	errno = saved;
}

// arrêt demandé (docker stop, systemctl stop, Ctrl+C) : on quitte tout de suite.
// Dans un conteneur, le serveur est le processus n°1 : le noyau ignore pour lui les
// signaux qu'il ne gère pas lui-même, et docker stop attendrait 10 s avant de le tuer.
static void on_stop(int sig){
	(void)sig;
	_exit(0);
}

// lancé en root (pour ouvrir le port 80, par exemple) : on passe à un utilisateur sans
// droits, une fois le port ouvert. Une faille dans le serveur ne donnerait alors que
// les droits de cet utilisateur, pas ceux de root sur toute la machine.
static int drop_privileges(void){
	const char *name = getenv("CSERVER_USER");
	if (name == NULL){name = cs_config.user;}

	if (geteuid() != 0){
		if (name != NULL){
			fprintf(stderr, "user = %s ignoré : le serveur ne tourne pas en root\n", name);
		}
		return 0;
	}
	if (name == NULL){
		fprintf(stderr, "attention : le serveur tourne en root ; une faille donnerait tous les droits "
		                "sur la machine (régler .user, ou CSERVER_USER)\n");
		return 0;
	}

	struct passwd *pw = getpwnam(name);
	if (pw == NULL){
		fprintf(stderr, "utilisateur %s inconnu\n", name);
		return -1;
	}
	// dans cet ordre : une fois l'utilisateur changé, on n'a plus le droit de changer de groupe
	if (initgroups(pw->pw_name, pw->pw_gid) < 0 || setgid(pw->pw_gid) < 0 || setuid(pw->pw_uid) < 0){
		perror("changement d'utilisateur");
		return -1;
	}
	if (setuid(0) == 0){   // vérification : revenir en root doit être impossible
		fprintf(stderr, "impossible de quitter root\n");
		return -1;
	}
	printf("le serveur tourne maintenant en %s\n", name);
	return 0;
}

int cs_run(const cs_app *app){
	// redirigé vers un fichier ou un pipe, stdout ne s'écrit que par gros blocs :
	// les logs arriveraient en retard, dans le désordre, ou seraient perdus si le
	// serveur est tué. Ligne par ligne, chaque printf est écrit tout de suite.
	setvbuf(stdout, NULL, _IOLBF, 0);

	if (load_config(app) < 0){
		return 1;
	}

	// socket(domain, type, protocol)  -> renvoie int (-1 si failure)
	int server = socket(AF_INET, SOCK_STREAM, 0);
	if (server < 0){
		perror("socket");
		return 1;
	}

	struct sockaddr_in adress;
	memset(&adress,0,sizeof(adress));
	adress.sin_family = AF_INET;
	adress.sin_addr.s_addr = INADDR_ANY;   // toutes les adresses de la machine
	if (cs_config.host != NULL){inet_pton(AF_INET, cs_config.host, &adress.sin_addr);}
	adress.sin_port =  htons(cs_config.port);

	int yes = 1;
	setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

	if (bind(server, (struct sockaddr *)&adress, sizeof(adress)) < 0){
		perror("bind");
		return 1;
	}

	// la file d'attente du noyau : les connexions qui arrivent pendant que toutes les
	// places sont prises y attendent (au-delà, elles sont refusées)
	if (listen(server, SOMAXCONN) < 0){
		perror("listen");
		return 1;
	}

	// le port est ouvert : plus besoin de root. Avant db_init, pour que la base soit
	// créée par l'utilisateur qui s'en servira.
	if (drop_privileges() < 0){
		return 1;
	}

	if (cs_config.db_path != NULL && db_init() < 0){
		return 1;
	}

	signal(SIGTERM, on_stop);
	signal(SIGINT, on_stop);

	// SA_RESTART : un accept() interrompu par SIGCHLD reprend tout seul
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_child_exit;
	sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGCHLD, &sa, NULL);

	// SIGCHLD est bloqué pendant qu'on lit ou modifie `children` (sinon un enfant qui
	// se termine au même moment fausserait le compte), et débloqué pendant accept()
	sigset_t sigchld, normal;
	sigemptyset(&sigchld);
	sigaddset(&sigchld, SIGCHLD);
	sigprocmask(SIG_BLOCK, &sigchld, &normal);

	printf("%s : listening on %s:%d (au plus %d connexions à la fois)\n",
		cs_config.name, cs_config.host != NULL ? cs_config.host : "*", cs_config.port, cs_config.max_connections);
	if (cs_config.trusted_proxy != NULL){
		printf("proxy de confiance : %s\n", cs_config.trusted_proxy);
	}

	int was_full = 0;
	while (1) {
		// toutes les places sont prises : on attend qu'un enfant se termine.
		// sigsuspend débloque SIGCHLD et attend, d'un seul coup : impossible de rater
		// le signal entre la vérification et l'attente
		if (children >= cs_config.max_connections && !was_full){
			printf("limite de %d connexions atteinte : les suivantes attendent\n", cs_config.max_connections);
			was_full = 1;
		}
		while (children >= cs_config.max_connections){
			sigsuspend(&normal);
		}
		was_full = 0;

		// accept() remplit `peer` avec l'adresse du client
		sigprocmask(SIG_SETMASK, &normal, NULL);
		struct sockaddr_in peer;
		socklen_t peer_len = sizeof(peer);
		int client = accept(server, (struct sockaddr *)&peer, &peer_len);
		sigprocmask(SIG_BLOCK, &sigchld, NULL);
		if (client<0){
			if (errno != EINTR){perror("accept");}
			continue;
		}

		char ip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));

		fflush(stdout);
		pid_t pid = fork();
		if (pid < 0){
			perror("fork");
			close(client);
			continue;
		}

		if (pid == 0){
			// l'enfant n'a pas d'enfants : il reprend le comportement normal des signaux
			signal(SIGCHLD, SIG_DFL);
			signal(SIGTERM, SIG_DFL);
			signal(SIGINT, SIG_DFL);
			sigprocmask(SIG_SETMASK, &normal, NULL);
			close(server);
			handle_client(client, ip);
			close(client);
			exit(0);
		}

		children++;
		close(client);
	}

	return 0;
}

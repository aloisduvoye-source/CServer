#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<signal.h>
#include<sys/socket.h>
#include<sys/time.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include"internal.h"

cs_app cs_config;

static void handle_client(int client, const char *ip){

	// si le client n'envoie rien pendant 5 s, read() renvoie -1
	struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
	setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	cs_ctx c;
	memset(&c, 0, sizeof(c));
	c.client = client;

	int r = http_parse(client, &c.req);
	if (r == 0){
		snprintf(c.req.ip, sizeof(c.req.ip), "%s", ip);
		printf("[pid %d] %s %s %s%s%s (%zu octets)\n", getpid(), c.req.ip, c.req.method, c.req.path,
			c.req.query[0] ? "?" : "", c.req.query, c.req.body_len);
		route(&c);
		ctx_finish(&c);   // 500 si le handler n'a rien envoyé, ferme la base, libère le corps
		return;
	}

	if (r > 0){
		http_send_error(client, r, NULL);
	}
	http_free(&c.req);
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
	if (cs_config.login_path == NULL)  {cs_config.login_path = "/login";}
	if (cs_config.signup_path == NULL) {cs_config.signup_path = "/signup";}

	// un nom doit tenir dans cs_user.name
	int name_room = (int)sizeof(((cs_user *)0)->name) - 1;
	if (cs_config.username_min < 1 || cs_config.username_max > name_room
	 || cs_config.username_min > cs_config.username_max || cs_config.password_min < 1
	 || cs_config.login_attempts < 1 || cs_config.login_window < 1){
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
	adress.sin_addr.s_addr = INADDR_ANY;
	adress.sin_port =  htons(cs_config.port);

	int yes = 1;
	setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

	if (bind(server, (struct sockaddr *)&adress, sizeof(adress)) < 0){
		perror("bind");
		return 1;
	}

	if (listen(server,10) < 0){
		perror("listen");
		return 1;
	}

	if (cs_config.db_path != NULL && db_init() < 0){
		return 1;
	}

	signal(SIGCHLD, SIG_IGN);

	printf("%s : listening on %d\n", cs_config.name, cs_config.port);

	while (1) {
		// accept() remplit `peer` avec l'adresse du client
		struct sockaddr_in peer;
		socklen_t peer_len = sizeof(peer);
		int client = accept(server, (struct sockaddr *)&peer, &peer_len);
		if (client<0){
			perror("accept");
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
			close(server);
			handle_client(client, ip);
			close(client);
			exit(0);
		}

		close(client);
	}

	return 0;
}

#include<stdio.h>
#include<string.h>
#include<unistd.h>
#include<sys/stat.h>
#include<fcntl.h>
#include"internal.h"

static const char* content_type(const char* filename){
	const char *dot = strrchr(filename, '.');
	if (dot == NULL){return "text/plain";}
	if (strcmp(dot, ".html") == 0){return "text/html";}
	if (strcmp(dot, ".css") == 0){return "text/css";}
	if (strcmp(dot, ".png") == 0){return "image/png";}
	if (strcmp(dot, ".jpg") == 0){return "image/jpeg";}
	if (strcmp(dot, ".webp") == 0){return "image/webp";}
	if (strcmp(dot, ".js") == 0){return "text/javascript";}
	if (strcmp(dot, ".svg") == 0){return "image/svg+xml";}
	if (strcmp(dot, ".ico") == 0){return "image/x-icon";}

	return "text/plain";
}

int static_serve(cs_ctx *c){
	// "/../x" sortirait du dossier : on fait comme si le fichier n'existait pas
	if (strstr(c->req.path, "..") != NULL){
		return 0;
	}

	char filename[512];
	int n = snprintf(
			filename,
			sizeof(filename),
			"%s%s",
			cs_config.static_dir,
			strcmp(c->req.path, "/") == 0 ? "/index.html" : c->req.path
	);
	if (n < 0 || (size_t)n >= sizeof(filename)){
		return 0;
	}

	struct stat info;
	int file = -1;

	if (stat(filename,&info) < 0 || !S_ISREG(info.st_mode) || (file  = open(filename, O_RDONLY)) < 0) {
		return 0;
	}

	http_send_header(c->client, 200, content_type(filename), info.st_size);

	char buffer[4096];
	ssize_t count;
	while ((count = read(file, buffer, sizeof(buffer))) > 0){
		if (http_write_all(c->client, buffer, count) < 0){
			break;
		}
	}

	close(file);
	return 1;
}

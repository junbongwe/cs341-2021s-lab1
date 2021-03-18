#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>

#define BUF_SIZE (1 << 12) // 4KB

int open_listenfd(int port);
void *connected_thread(void *aux);

int main(int argc, char *argv[]) {
	int port, listenfd, *connfdp;
	struct sockaddr cliaddr;
	socklen_t addrlen;
	pthread_t tid;

	if (argc != 3 || strcmp(argv[1], "-p") != 0) {
		printf("usage: %s -p [port_number]\n", argv[0]);
		return -1;
	}

	port = atoi(argv[2]);
	listenfd = open_listenfd(port);

	if (listenfd < 0) {
		printf("open_listenfd failed\n");
		return -1;
	}

	while (1) {
		if ((connfdp = (int *)malloc(sizeof(int))) == NULL) {
			printf("OOM\n");
			return -1;
		}
		*connfdp = accept(listenfd, &cliaddr, &addrlen);
		pthread_create(&tid, NULL, connected_thread, connfdp);
	}

	return 0;
}

int open_listenfd(int port) {
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr = (struct sockaddr_in) {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_ANY),
		.sin_port = htons(port)
	};

	if (sockfd < 0)
		return -1;

	if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr))) {
		close(sockfd);
		return -1;
	}

	if (listen(sockfd, 1024)) {
		close(sockfd);
		return -1;
	}

	return sockfd;
}

void error_handler(int connfd, int eno) {
	char buf[BUF_SIZE];

	if (eno == 400)
		strcpy(buf, "HTTP/1.0 400 Bad Request\r\n");
	else if (eno == 404)
		strcpy(buf, "HTTP/1.0 404 Not Found\r\n");
	else {
		printf("should not reach here\n");
		return;
	}
	
	sprintf(buf, "%sServer: Apache/2.0.52 (CentOS)\r\n", buf);
	sprintf(buf, "%sConnection: close\r\n\r\n", buf);

	write(connfd, buf, strlen(buf) + 1);
}

void do_get_method(char *buf, int connfd) {
	char pathname[BUF_SIZE] = "./";
	
	sscanf(buf, "GET %s", &pathname[2]);
	printf("path: %s\n", pathname);
}

void do_post_method() {
}

void *connected_thread(void *aux) {
	int connfd = *((int *)aux), fd, read_byte;
	char buf[BUF_SIZE];
	char pathname[BUF_SIZE] = "./";

	free(aux);
	pthread_detach(pthread_self());
	read_byte = read(connfd, buf, BUF_SIZE);

	if (strncmp(buf, "GET", 3) == 0) {
		struct stat statbuf;

		sscanf(buf, "GET %s", &pathname[2]);
		if (read_byte == BUF_SIZE)
			while (read(connfd, buf, BUF_SIZE) != 0);

		if (((fd = open(pathname, O_RDONLY)) < 0) || fstat(fd, &statbuf))
			error_handler(connfd, 404);
		else {
			strcpy(buf, "HTTP/1.0 200 OK\r\n");
			sprintf(buf, "%sServer: Apache/2.0.52 (CentOS)\r\n", buf);
			sprintf(buf, "%sContent-Length: %ld\r\n", buf, statbuf.st_size);
			sprintf(buf, "%sContent-Type: text/html\r\n", buf);
			sprintf(buf, "%sConnection: Keep-Alive\r\n\r\n", buf);
			write(connfd, buf, strlen(buf) + 1);

			while ((read_byte = read(fd, buf, BUF_SIZE)) != 0)
				write(connfd, buf, read_byte);
		}
	}
	else if (strncmp(buf, "POST", 4) == 0) {
	}
	else {
		if (read_byte == BUF_SIZE)
			while (read(connfd, buf, BUF_SIZE) != 0);
		error_handler(connfd, 400);
	}
	close(connfd);

	return NULL;
}

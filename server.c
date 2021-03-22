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

#define DBUF_SIZE (1 << 16) // 64KB
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

	write(connfd, buf, strlen(buf));
}

void *connected_thread(void *aux) {
	int connfd = *((int *)aux), fd, read_byte, eno;
	char buf[DBUF_SIZE];
	char pathname[BUF_SIZE] = "./";

	free(aux);
	pthread_detach(pthread_self());
	read_byte = read(connfd, buf, DBUF_SIZE);

	if (strncmp(buf, "GET", 3) == 0) {
		struct stat statbuf;
		int parsing_pos = 0;

		sscanf(buf, "GET %s", &pathname[2]);

		if (((fd = open(pathname, O_RDONLY)) < 0) || fstat(fd, &statbuf)) {
			eno = 404;
			goto err;
		}
		else {
			strcpy(buf, "HTTP/1.0 200 OK\r\n");
			sprintf(buf, "%sServer: Apache/2.0.52 (CentOS)\r\n", buf);
			sprintf(buf, "%sContent-Length: %ld\r\n", buf, statbuf.st_size);
			sprintf(buf, "%sContent-Type: text/html\r\n", buf);
			sprintf(buf, "%sConnection: Keep-Alive\r\n\r\n", buf);
			write(connfd, buf, strlen(buf));

			while ((read_byte = read(fd, buf, DBUF_SIZE)) != 0)
				write(connfd, buf, read_byte);
		}
	}
	else if (strncmp(buf, "POST", 4) == 0) {
		int write_byte, parsing_pos = 0;
		size_t content_length = 0;
		sscanf(buf, "POST %s", &pathname[2]);
		while (1) {
			while (strncmp(&buf[parsing_pos++], "\r\n", 2) != 0) {
				if (parsing_pos > read_byte - 4) {
					eno = 400;
					goto err;
				}
			}
			parsing_pos++;
			if (strncmp(&buf[parsing_pos], "\r\n", 2) == 0) {
				parsing_pos += 2;
				break;
			}
			if (strncmp(&buf[parsing_pos], "Content-Length", 14) == 0)
				sscanf(&buf[parsing_pos], "Content-Length: %ld", &content_length);
			while (buf[parsing_pos] != ':') {
				if ((parsing_pos >= read_byte - 5) || (strncmp(&buf[parsing_pos], "\r\n", 2) == 0)) {
					eno = 404;
					goto err;
				}
				parsing_pos++;
			}
		}

		unlink(pathname);
		if ((fd = open(pathname, O_RDWR | O_CREAT, 0644)) < 0) {
			eno = 404;
			goto err;
		}
		if (read_byte > parsing_pos) {
			write_byte = (read_byte - parsing_pos < content_length) ? read_byte - parsing_pos : content_length;
			write(fd, &buf[parsing_pos], write_byte);
			content_length -= write_byte;
		}
		while (content_length) {
			read_byte = read(connfd, buf, DBUF_SIZE);
			write_byte = (read_byte < content_length) ? read_byte : content_length;
			write(fd, buf, write_byte);
			content_length -= write_byte;
		}
		close(fd);

		strcpy(buf, "HTTP/1.0 200 OK\r\n");
		sprintf(buf, "%sServer: Apache/2.0.52 (CentOS)\r\n", buf);
		sprintf(buf, "%sConnection: Keep-Alive\r\n\r\n", buf);
		write(connfd, buf, strlen(buf));
	}
	else {
		eno = 400;
		goto err;
	}
	close(connfd);

	return NULL;

err:
	error_handler(connfd, eno);
	close(connfd);
	return NULL;
}

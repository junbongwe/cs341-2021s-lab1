#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>

#define BUF_SIZE (1 << 12) // 4KB

int open_clientfd(char *hostname, char *port);
void do_get_method(int clientfd, char *hostname, char *port, char *pathname);
void do_post_method(int clientfd, char *hostname, char *port, char *pathname);

int main(int argc, char *argv[]) {
	int clientfd;
	char buf[BUF_SIZE], hostname[BUF_SIZE], port[BUF_SIZE], pathname[BUF_SIZE];

	if (argc != 3 || !(strcmp(argv[1], "-G") == 0 || strcmp(argv[1], "-P") == 0)) {
		printf("usage: %s -G URL | %s -P URL\n", argv[0], argv[0]);
		return -1;
	}

	strcpy(port, "80");
	sscanf(argv[2], "http://%[^'/']%s", buf, pathname);
	sscanf(buf, "%[^':']:%s", hostname, port);	

	clientfd = open_clientfd(hostname, port);
	if (clientfd < 0) {
		printf("open_clientfd failed\n");
		return -1;
	}

	if (strcmp(argv[1], "-G") == 0)
		do_get_method(clientfd, hostname, port, pathname);
	else
		do_post_method(clientfd, hostname, port, pathname);

	close(clientfd);

	return 0;
}

int open_clientfd(char *hostname, char *port) {
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr = {0, };
	struct hostent *he;

	if ((he = gethostbyname(hostname)) == NULL)
		return -1;

	addr.sin_family = AF_INET;
	memcpy(&addr.sin_addr.s_addr, he->h_addr, he->h_length);
	addr.sin_port = htons(atoi(port));

	if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr))) {
		close(sockfd);
		return -1;
	}

	return sockfd;
}

void do_get_method(int clientfd, char *hostname, char *port, char *pathname) {
	char buf[BUF_SIZE];
	int read_byte;

	sprintf(buf, "GET %s HTTP/1.0\r\n", pathname);
	sprintf(buf, "%sHost: %s:%s\r\n", buf, hostname, port);
	sprintf(buf, "%sUser-Agent: Firefox/3.6\r\n", buf);
	sprintf(buf, "%sConnection: Keep-Alive\r\n\r\n", buf);

	write(clientfd, buf, strlen(buf) + 1);
	while ((read_byte = read(clientfd, buf, BUF_SIZE)) != 0)
		write(STDOUT_FILENO, buf, read_byte);
}

void do_post_method(int clientfd, char *hostname, char *port, char *pathname) {	
	struct stat statbuf;
	char buf[BUF_SIZE];
	int read_byte;

	if (fstat(STDIN_FILENO, &statbuf))
		return;

	sprintf(buf, "POST %s HTTP/1.0\r\n", pathname);
	sprintf(buf, "%sHost: %s:%s\r\n", buf, hostname, port);
	sprintf(buf, "%sUser-Agent: Firefox/3.6\r\n", buf);
	sprintf(buf, "%sContent-Length: %ld\r\n", buf, statbuf.st_size);
	sprintf(buf, "%sConnection: Keep-Alive\r\n\r\n", buf);

	write(clientfd, buf, strlen(buf) + 1);
	while((read_byte = read(STDIN_FILENO, buf, BUF_SIZE)) != 0)
		write(clientfd, buf, read_byte);
}

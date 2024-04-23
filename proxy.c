#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

static const char *new_version = "HTTP/1.0";

// void *thread(void *vargp);
void doit(int connfd);
void do_request(int p_clientfd, char *method, char *uri_ptos, char *host);
void do_response(int p_connfd, int p_clientfd);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
int parse_uri(char *uri, char *uri_ptos, char *host, char *port);

void sigchld_handler(int sig){
  // [인자값 의미]
  // -1 : 임의의 자식 프로세스를 기다림
  // 0 : ?
  // WNOHANG : 기다리는 PID가 종료되지 않아서 즉시 종료 상태를 회수 할 수 없는 상황에서 호출자는 차단되지 않고 반환값으로 0을 받음
  while(waitpid(-1, 0, WNOHANG) > 0){
    return;
  }
}

int main(int argc, char **argv) {
  int listenfd, *p_connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  // pthread_t tid;
  
  if(argc != 2){
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  Signal(SIGCHLD, sigchld_handler);
  listenfd = Open_listenfd(argv[1]);

  while(1){
    clientlen = sizeof(clientaddr);
    // 클라이언트 연결 요청을 proxy의 연결 식별자가 accept
    p_connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    if(Fork() == 0){
      Close(listenfd);
      doit(p_connfd);
      Close(p_connfd);
      exit(0);
    }
    // 안해도 메모리 누수 없음???
    Close(p_connfd);
  }
}

// void *thread(void *vargp){
//   int connfd = *((int *)vargp);
//   Pthread_detach(pthread_self());
//   Free(vargp);
//   doit(connfd);
//   Close(connfd);
//   return NULL;
// }

// 클라이언트의 요청을 받아서 읽고 파싱해서 서버에게 보낸다
void doit(int p_connfd){
  int clientfd;
  char buf[MAXLINE], host[MAXLINE], port[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char uri_ptos[MAXLINE];
  rio_t rio;

  Rio_readinitb(&rio, p_connfd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers to proxy:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);

  parse_uri(uri, uri_ptos, host, port);

  clientfd = Open_clientfd(host, port);
  do_request(clientfd, method, uri_ptos, host);
  do_response(p_connfd, clientfd);
  Close(clientfd);
}

void do_request(int p_clientfd, char *method, char *uri_ptos, char *host){
  char buf[MAXLINE];
  printf("Request headers to server: \n");
  printf("%s %s %s\n", method, uri_ptos, new_version);

  sprintf(buf, "GET %s %s\r\n", uri_ptos, new_version);
  sprintf(buf, "%sHost: %s\r\n", buf, host);
  sprintf(buf, "%s%s", buf, user_agent_hdr);
  sprintf(buf, "%sConnections: close\r\n", buf);
  sprintf(buf, "%sProxy-Connection: close\r\n\r\n", buf);

  Rio_writen(p_clientfd, buf, (size_t)strlen(buf));
}

void do_response(int p_connfd, int p_clientfd){
  char buf[MAX_CACHE_SIZE];
  ssize_t n;
  rio_t rio;

  Rio_readinitb(&rio, p_clientfd);
  n = Rio_readnb(&rio, buf, MAX_CACHE_SIZE);
  Rio_writen(p_connfd, buf, n);
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  // 에러 Bdoy 생성
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor="
                "ffffff"
                ">\r\n",
          body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  // 에러 Header 생성 & 전송
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));

  // 에러 Body 전송
  Rio_writen(fd, body, strlen(body));
}

int parse_uri(char *uri, char *uri_ptos, char *host, char *port){
  char *ptr;

  if(!(ptr = strstr(uri, "://")))
    return -1;
  ptr += 3;
  strcpy(host, ptr);

  if((ptr = strchr(host, ':'))){
    *ptr = '\0';
    ptr += 1;
    strcpy(port, ptr);
  }
  else{
    if((ptr = strchr(host, '/'))){
      *ptr = '\0';
      ptr += 1;
    }
    strcpy(port, "80");
  }

  if((ptr = strchr(port, '/'))){
    *ptr = '\0';
    ptr += 1;
    strcpy(uri_ptos, "/");
    strcat(uri_ptos, ptr);
  }
  else strcpy(uri_ptos, "/");

  return 0;
}
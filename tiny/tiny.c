/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, char *method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

/* port 번호를 인자로 받아 클라이언트 요청이 올 때마다 새로 연결 소켓을 생성하여 doit 함수 호출 */
int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;                              // 클라이언트 주소 구조체 크기
  struct sockaddr_storage clientaddr;               // 클라이언트 주소 정보 저장하기 위한 구조체

  /* Check command line args */
  // 넘어온 포트번호가 없으면 에러 처리 후 종료
  if (argc != 2) {              
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  // 듣기 소켓 오픈
  listenfd = Open_listenfd(argv[1]);
  // 무한 서버 루프 실행
  while (1) {
    clientlen = sizeof(clientaddr);
    // 반복적으로 연결 요청 접수
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);  // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);   // 트랜잭션 수행
    Close(connfd);  // 자신 쪽의 연결 끝 닫음
  }
}

// 클라이언트의 요청 처리
void doit(int fd){
  int is_static;
  struct stat sbuf;
  // MAXLINE = 8192 (8KB)
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;
  
  Rio_readinitb(&rio, fd);
  // 요청 라인을 읽고 분석
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  // tiny는 get 메소드만 지원 (다른 메소드 요청 시 에러)
  if(strcasecmp(method, "GET") && strcasecmp(method, "HEAD")){
    clienterror(fd, method, "501", "Not implemented",
              "Tiny does not implement this method");
    return;
  }
  // HEAD, GET method면 헤더를 읽어들이고, 다른 요청은 무시
  // HEAD 메소드일때만 read_requesthdrs를 실행해야 하는거 아닌가??
  read_requesthdrs(&rio);
  // get 요청으로부터 uri 파싱
  // 요청이 정적인지 동적인지 나타내는 플래그 설정 (정적이면 1, 동적이면 0 반환)
  is_static = parse_uri(uri, filename, cgiargs);
  // 파일이 디스크 상에 없으면, 클라이언트에게 에러 메시지 보내고 리턴
  if(stat(filename, &sbuf) < 0){
    clienterror(fd, filename, "404", "Not found",
                "Tiny couldn't find this file");
    return;
  }
  // 정적 컨텐츠 일 경우
  if(is_static){
    // 파일이 보통 파일이며 읽기 권한을 가지고 있는지 (아니면 에러 후 리턴)
    if(!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)){
      clienterror(fd, filename, "403", "Forbidden",
                  "Tiny couldn't read the file");
      return;
    }
    // 맞으면 정적 컨텐츠 클라이언트에게 제공
    serve_static(fd, filename, sbuf.st_size, method);
  }
  // 동적 컨텐츠 일 경우 (정적 컨텐츠 흐름과 동일)
  else{
    if(!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)){
      clienterror(fd, filename, "403", "Forbidden",
                  "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs, method);
  }
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg){
  char buf[MAXLINE], body[MAXBUF];
  /* Build the HTTP response body */
  /* 브라우저 사용자에게 에러를 설명하는 응답 본체에 HTML도 함께 보낸다 */
  /* HTML 응답은 본체에서 컨텐츠의 크기와 타입을 나타내야하기에, HTMl 컨텐츠를 한 개의 스트링으로 만든다. */
  /* 이는 sprintf를 통해 body는 인자에 스택되어 하나의 긴 문자열이 저장된다. */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);
  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int) strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

void read_requesthdrs(rio_t *rp){
  char buf[MAXLINE];

  rio_readlineb(rp, buf, MAXLINE);
  while(strcmp(buf, "\r\n")){
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

int parse_uri(char *uri, char *filename, char *cgiargs){
  char *ptr;
  // cgi-bin가 문자열에 없는 경우는 정적 컨텐츠
  if(!strstr(uri, "cgi-bin")){
    // 정적 컨텐츠면 CGI 인자 스트링 지우고
    strcpy(cgiargs, "");
    // URI를 상대 리눅스 경로이름으로 변환
    strcpy(filename, ".");
    strcat(filename, uri);
    // URI가 '/' 문자로 끝나면 기본 파일 이름을 추가
    if(uri[strlen(uri)-1] == '/')
      strcat(filename, "home.html");
    return 1;
  }
  // 동적 컨텐츠면
  else{
    // 모든 CGI 인자 추출하고
    ptr = index(uri, '?');
    if(ptr){
      strcpy(cgiargs, ptr+1);
      *ptr = '\0';
    }
    else
      strcpy(cgiargs, "");
    // 나머지 URI 부분을 상대 리눅스 파일 이름으로 변환
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}

void serve_static(int fd, char *filename, int filesize, char *method){
  int srcfd;                // 파일 디스크립터
  char *srcp,               // 파일 내용을 메모리에 매핑한 포인터
       filetype[MAXLINE],   // 파일의 MIME 타입
       buf[MAXBUF];         // 응답 헤더를 저장할 버퍼

  /* 응답 헤더 생성 및 전송 */
  get_filetype(filename, filetype);                           // 파일 타입 결정
  sprintf(buf, "HTTP/1.0 200 OK\r\n");                        // 응답 라인 작성
  // 응답 헤더
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);         // 서버 정보 추가
  sprintf(buf, "%sConnections: close\r\n", buf);              // 연결 종료 정보 추가
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);    // 컨텐츠 길이 추가
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);  // 컨텐츠 타입 추가

  /* 응답 라인과 헤더를 클라이언트에게 보냄 */
  Rio_writen(fd, buf, strlen(buf)); 
  printf("Response headers: \n");
  printf("%s", buf);

  if(strcasecmp(method, "HEAD") == 0)
    return;
  /* 응답 바디 전송 */
  srcfd = Open(filename, O_RDONLY, 0);                        // 파일 열기
  // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); // 파일을 메모리에 동적할당
  srcp = (char *) Malloc(filesize);                           // mmap 대신 malloc 사용
  Rio_readn(srcfd, srcp, filesize);           
  
  Close(srcfd);                                             // 파일 닫기
  Rio_writen(fd, srcp, filesize);                           // 클라이언트에게 파일 내용 전송
  // Munmap(srcp, filesize);                                // 메모리 할당 해제
  free(srcp);                                               // mummap 대신 free 사용                            
}

void get_filetype(char *filename, char *filetype){
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4");
  else
    strcpy(filetype, "text/plain");
}

void serve_dynamic(int fd, char *filename, char *cgiargs, char *method){
  char buf[MAXLINE], *emptylist[] = {NULL};

  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if(Fork() == 0){
    // QUERY_STRING 환경변수를 요청 URI의 CGI 인자들로 초기화
    setenv("QUERY_STRING", cgiargs, 1);
    setenv("REQUEST_METHOD", method, 1);
    // 자식은 자식의 표준 출력을 연결 파일 식별자로 재지정
    Dup2(fd, STDOUT_FILENO);
    // CGI 프로그램을 로드하고 실행
    Execve(filename, emptylist, environ);
  }
  Wait(NULL);
}

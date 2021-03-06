/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
/* This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */

/************************************************************************************************************************
 * @file httpd.c
 * @brief 
 * @author      renbin.guo added comment  
 * @version 
 * @date 2017-07-07
 *
 * @usage:
 *
 *      server:
 *
 *      1.通过telnet发送get命令
 *
 *      [root@localhost tinyhttpd]# ./server-http 
 *      httpd running on port 47547
 *      client:
 *          [root@localhost tinyhttpd]# telnet  192.168.117.131 47547
 *          Trying 192.168.117.131...
 *          Connected to 192.168.117.131.  GET / HTTP/1.1
 *          Escape character is '^]'.
 *          GET /test.html HTTP/1.1     // 要注意这里敲了2个回车!  还需要注意的是要'/test.html',这个文件需要放到htdocs才能被访问到。
 *
 *          HTTP/1.0 200 OK
 *          Server: jdbhttpd/0.1.0
 *          Content-Type: text/html
 *
 *          1111111111111
 *          Connection closed by foreign host.
 *          [root@localhost ~]# 
 *
 *      2.通过浏览器做实验
 *
 *      [root@localhost tinyhttpd]# ./server-http 
 *      httpd running on port 47547
 *      client:
 *          在浏览器输入:   127.0.0.1:47547
 *          然后在弹出的框中输入'yellow'
 *          你会看过网页的颜色变了
 *      效果可参考: http://blog.csdn.net/jcjc918/article/details/42129311
 *      
 *
 *
 *
 *  @note :
 *           1.   对自己的提醒:  
 *                  如果项目是放在linux ,vmware和windows共享目录下,则共享目录下所有文件都是可读可写可执行的，所以cgi总是1，即使你访问的只是/index
 *                  所以如果在是放在共享目录下,则cgi当访问index的时候，要把它改为cgi=0。
 *
                 if ((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH)    )
                        cgi = 1;          // renbin.guo modified 这里原本是 cgi = 1;  但是由于我在虚拟机共享目录下普通文件test.html都是可执行的，所以我把它改成0
                 您运行的时候，要改为1

            2.  在浏览器中运行的时候，输入'yellow'之后在服务器端的log中我遇到了 Can't locate CGI.pm in @INC'
                这个需要安装CGI.pm  centos6.5 yum install perl-CGI

            3.  需要修改htdos中的check.cgi 和color.cgi开始第一行的perl位置。我的是在 #!/usr/bin/perl -Tw (可通过whereis perl查看您的perl位置)
************************************************************************************************************************/


#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>

#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"

void *accept_request(void *);  
void bad_request(int);
void cat(int, FILE *);
void cannot_execute(int);
void error_die(const char *);
void execute_cgi(int, const char *, const char *, const char *);
int  get_line(int, char *, int);
void headers(int, const char *);
void not_found(int);
void serve_file(int, const char *);
int  startup(u_short *);
void unimplemented(int);

/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client */
/**********************************************************************/
void *accept_request(void * tclient)  
{
    int client = *(int *)tclient;  
    char buf[1024];
    int numchars;
    char method[255];
    char url[255];
    char path[512];
    size_t i, j;
    struct stat st;
    int cgi = 0;      /* becomes true if server decides this is a CGI program */
    char *query_string = NULL;

    /*得到请求的第一行*/
    numchars = get_line(client, buf, sizeof(buf));
    printf("###[accept_request] numchars = %d ,buf = %s \n",numchars,buf);
    i = 0; j = 0;
    /*把客户端的请求方法存到 method 数组*/
    while (!ISspace(buf[j]) && (i < sizeof(method) - 1))
    {
        method[i] = buf[j]; 
        i++; j++;
    }
    method[i] = '\0';
    printf("method = %s\n",method);    // renbin.guo added 2017-07-06
    
    /*如果既不是 GET 又不是 POST 则无法处理 */
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))
    {
        unimplemented(client);
        return NULL;  
    }

    /* POST 的时候开启 cgi */
    if (strcasecmp(method, "POST") == 0)
        cgi = 1;

    /*读取 url 地址*/
    i = 0;
    while (ISspace(buf[j]) && (j < sizeof(buf)))
        j++;
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf)))
    {
        /*存下 url */
        url[i] = buf[j];
        i++; j++;
    }
    url[i] = '\0';

    printf("url = %s\n",url);    // renbin.guo added 2017-07-07

    /*处理 GET 方法*/
    if (strcasecmp(method, "GET") == 0)
    {
        /* 待处理请求为 url */
        query_string = url;
        while ((*query_string != '?') && (*query_string != '\0'))
            query_string++;
        /* GET 方法特点，? 后面为参数*/
        if (*query_string == '?')
        {
            /*开启 cgi */
            cgi = 1;
            *query_string = '\0';
            query_string++;
            printf("### get to here !!\n");  // renbin.guo added 2017-07-07
        }
    }

    /*格式化 url 到 path 数组，html 文件都在 htdocs 中*/
    // 所以再命令行请求的格式必须是  GET /test.html HTTP/1.0    注意/test.html必须有'/'
    sprintf(path, "htdocs%s", url);

    
    /*默认情况为 index.html */
    if (path[strlen(path) - 1] == '/')      /// 如果请求的是目录   或者说请求是这样的 GET / HTTP/1.0  
        strcat(path, "index.html");         /// 拼接字符串 path = htdocs/index.html
    /*根据路径找到对应文件 */
    if (stat(path, &st) == -1) {        ///如果没有找到对应的文件( 文件不存在stat就会返回-1 )
        /*把所有 headers 的信息都丢弃*/ ///grb  就是get方法后面的所有行，直接取出来，丢弃
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
        {
            printf("-------in stat-----------\n");
            numchars = get_line(client, buf, sizeof(buf));

        }
        /*回应客户端找不到*/
        not_found(client);
    }
    else        //s/ 找到文件!
    {
        /*如果是个目录，则默认使用该目录下 index.html 文件*/
        if ((st.st_mode & S_IFMT) == S_IFDIR)
            strcat(path, "/index.html");
      if ((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH)    )
          cgi = 1;          // renbin.guo modified 这里原本是 cgi = 1;  但是由于我在虚拟机共享目录下普通文件都是可执行的，所以我把它改成0
      /*不是 cgi,直接把服务器文件返回，否则执行 cgi */
      printf("### path = %s\n",path);
      if (!cgi){
          printf("### !CGI\n");
          serve_file(client, path);
      }
      else
      {
          printf("### CGI\n");
          execute_cgi(client, path, method, query_string);
      }
    }

    /*断开与客户端的连接（HTTP 特点：无连接）*/
    close(client);          /// 所以我们发送一个请求之后GET /test.html HTTP/1.0，显示完了，就有Connection closed by foreign host

    printf("----thread end !--------------------------------------\n");
    return NULL;                /// 结束线程。
}

/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
void bad_request(int client)
{
    char buf[1024];

    /*回应客户端错误的 HTTP 请求 */
    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}

/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
void cat(int client, FILE *resource)
{
    char buf[1024];

    /*读取文件中的所有数据写到 socket */
    fgets(buf, sizeof(buf), resource);
    while (!feof(resource))
    {
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
void cannot_execute(int client)
{
    char buf[1024];

    /* 回应客户端 cgi 无法执行*/
    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
void error_die(const char *sc)
{
    /*出错信息处理 */
    perror(sc);
    exit(1);
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
void execute_cgi(int client, const char *path, const char *method, const char *query_string)
{
    char buf[1024];
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int content_length = -1;

    buf[0] = 'A'; buf[1] = '\0';
    if (strcasecmp(method, "GET") == 0)
        /*把所有的 HTTP header 读取并丢弃*/
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
        {
            printf("  ----execute_cgi()");
            numchars = get_line(client, buf, sizeof(buf));
        }
    else    /* POST */
    {
        /* 对 POST 的 HTTP 请求中找出 content_length */
        numchars = get_line(client, buf, sizeof(buf));
        while ((numchars > 0) && strcmp("\n", buf))
        {
            /*利用 \0 进行分隔 */
            buf[15] = '\0';
            /* HTTP 请求的特点*/
            if (strcasecmp(buf, "Content-Length:") == 0)
                content_length = atoi(&(buf[16]));
            numchars = get_line(client, buf, sizeof(buf));
        }
        /*没有找到 content_length */
        if (content_length == -1) {
            /*错误请求*/
            bad_request(client);
            return;
        }
    }

    /* 正确，HTTP 状态码 200 */
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);

    /* 建立管道*/
    if (pipe(cgi_output) < 0) {
        /*错误处理*/
        cannot_execute(client);
        return;
    }
    /*建立管道*/
    if (pipe(cgi_input) < 0) {
        /*错误处理*/
        cannot_execute(client);
        return;
    }

    if ((pid = fork()) < 0 ) {
        /*错误处理*/
        cannot_execute(client);
        return;
    }
    if (pid == 0)  /* child: CGI script */
    {
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        /* 把 STDOUT 重定向到 cgi_output 的写入端 */
        dup2(cgi_output[1], 1);

        /* 把 STDIN 重定向到 cgi_input 的读取端 */
        dup2(cgi_input[0], 0);

        /* 关闭 cgi_input 的写入端 和 cgi_output 的读取端 */
        close(cgi_output[0]);
        close(cgi_input[1]);

        /*设置 request_method 的环境变量*/
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);

        if (strcasecmp(method, "GET") == 0) {
            /*设置 query_string 的环境变量*/
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        }
        else {   /* POST */
            /*设置 content_length 的环境变量*/
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
        /*用 execl 运行 cgi 程序*/
        printf("### before execl color.cgi\n");  /* grb 这句写到标准输出了，然后它被下面的父进程通过read(cgi_output[0]) 读了出来*/
        execl(path, path, NULL);
        printf("### after  execl color.cgi\n");         /* grb 因为执行了另外一个镜像，所有永远不会执行到这里来 */

        exit(0);
    } else {    /* parent */

        /* 关闭 cgi_input 的读取端 和 cgi_output 的写入端 */
        close(cgi_output[1]);
        close(cgi_input[0]);
        if (strcasecmp(method, "POST") == 0)
            /*接收 POST 过来的数据*/
            for (i = 0; i < content_length; i++) {
                recv(client, &c, 1, 0);
                printf("%c",c );

                /*把 POST 数据写入 cgi_input，现在重定向到 STDIN */
                write(cgi_input[1], &c, 1);         /* grb  这里写入的是 color=red ,这个是给color.cgi这个程序的参数 ,接下来我们等待color.cgi处理，处理完后读取处理结果，将结果发给浏览器*/
            }
        printf("\n------sent core=red to color.cgi -----------------------------------\n");

        /*读取 cgi_output 的管道输出到客户端，该管道输入是 STDOUT */
        while (read(cgi_output[0], &c, 1) > 0)
        {

                printf("%c",c );
                send(client, &c, 1, 0);         /* grb 将color.cgi产生的输出数据发送给浏览器 */

        }

        /*关闭管道*/
        close(cgi_output[0]);
        close(cgi_input[1]);
        /*等待子进程*/
        waitpid(pid, &status, 0);
    }
}

/**********************************************************************/
/* Get a line from a socket, whether the line ends in a newline,
 * carriage return, or a CRLF combination.  Terminates the string read
 * with a null character.  If no newline indicator is found before the
 * end of the buffer, the string is terminated with a null.  If any of
 * the above three line terminators is read, the last character of the
 * string will be a linefeed and the string will be terminated with a
 * null character.
 * Parameters: the socket descriptor
 *             the buffer to save the data in
 *             the size of the buffer
 * Returns: the number of bytes stored (excluding null) */
/**********************************************************************/
int get_line(int sock, char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;

    printf("[get_line ] begin\n");
    /*把终止条件统一为 \n 换行符，标准化 buf 数组*/
    while ((i < size - 1) && (c != '\n'))
    {
        /*一次仅接收一个字节*/
        n = recv(sock, &c, 1, 0);
        /* DEBUG printf("%02X\n", c); */
        printf("%c", c);    //renbin.guo added 2017-07-07   
        fflush(stdout);     //renbin.guo added 2017-07-07  printf按行缓冲，所以这里需要刷新(如果末尾不加换行符的话)
        if (n > 0)
        {
            /*收到 \r 则继续接收下个字节，因为换行符可能是 \r\n */
            if (c == '\r')
            {
                /*使用 MSG_PEEK 标志使下一次读取依然可以得到这次读取的内容，可认为接收窗口不滑动*/
                n = recv(sock, &c, 1, MSG_PEEK);
                /* DEBUG printf("%02X\n", c); */
                printf("%c", c);        //renbin.guo added 2017-07-07
                fflush(stdout);         //renbin.guo added 2017-07-07
                /*但如果是换行符则把它吸收掉*/
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            /*存到缓冲区*/
            buf[i] = c;
            i++;
        }
        else
            c = '\n';
    }
    buf[i] = '\0';
    printf("[get_line ] end\n");
    /*返回 buf 数组大小*/
    return(i);
}

/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *             the name of the file */
/**********************************************************************/
void headers(int client, const char *filename)
{
    char buf[1024];
    (void)filename;  /* could use filename to determine file type */

    /*正常的 HTTP header */
    printf("### before headers () send 1\n");
    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);      /* grb 这里直接用send 就可以了!,send系统调用 */
    printf("### headers () send 1\n");
    /*服务器信息*/
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    printf("### headers () send 2\n");
     
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    printf("### headers () send 3\n");
    
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);

    printf("### headers () send 4\n");
}

/**********************************************************************/
/* Give a client a 404 not found status message. */
/**********************************************************************/
void not_found(int client)
{
    char buf[1024];

    /* 404 页面 */
    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    /*服务器信息*/
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Send a regular file to the client.  Use headers, and report
 * errors to client if they occur.
 * Parameters: a pointer to a file structure produced from the socket
 *              file descriptor
 *             the name of the file to serve */
/**********************************************************************/
void serve_file(int client, const char *filename)
{
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];

    /*读取并丢弃 header */      //grb 客户端发过来的get可能有很多行,我们实际上只处理它的第一行。tinny http功能有限啊!
    buf[0] = 'A'; 
    buf[1] = '\0';      /* grb这里初始化为这个有必要吗?不初始化可以吗? */
    while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */  ///grb 这里之所以要判断'\n'，是因为文件开头有提到，必须按下两个回车键，所以最后一个空行代表get请求结束
    {
        printf("  ---server_file()\n");
        numchars = get_line(client, buf, sizeof(buf));
    }

    /*打开 sever 的文件*/
    resource = fopen(filename, "r");
    if (resource == NULL)
        not_found(client);
    else
    {
        /*写 HTTP header */
        headers(client, filename);

        printf(" ### after hearders\n");
        /*复制文件*/
        cat(client, resource);
        printf(" ### after cat\n");
    }
    fclose(resource);
}

/**********************************************************************/
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket */
/**********************************************************************/
int startup(u_short *port)
{
    int httpd = 0;
    struct sockaddr_in name;

    /*建立 socket */
    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if (httpd == -1)
        error_die("socket");
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
        error_die("bind");
    /*如果当前指定端口是 0，则动态随机分配一个端口*/
    if (*port == 0)  /* if dynamically allocating a port */
    {
        socklen_t namelen = sizeof(name);  
        if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
            error_die("getsockname");
        *port = ntohs(name.sin_port);
    }
    /*开始监听*/
    if (listen(httpd, 5) < 0)
        error_die("listen");
    /*返回 socket id */
    return(httpd);
}

/**********************************************************************/
/* Inform the client that the requested web method has not been
 * implemented.
 * Parameter: the client socket */
/**********************************************************************/
void unimplemented(int client)
{
    char buf[1024];

    /* HTTP method 不被支持*/
    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    /*服务器信息*/
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/

int main(void)
{
    int server_sock = -1;
    u_short port = 0;
    int client_sock = -1;
    struct sockaddr_in client_name;
    socklen_t client_name_len = sizeof(client_name);  
    pthread_t newthread;

    /*在对应端口建立 httpd 服务*/
    server_sock = startup(&port);
    printf("httpd running on port %d\n", port);

    while (1)
    {
        /*套接字收到客户端连接请求*/
        client_sock = accept(server_sock,(struct sockaddr *)&client_name,&client_name_len);
        if (client_sock == -1)
            error_die("accept");
        /*派生新线程用 accept_request 函数处理新请求*/
        /* accept_request(client_sock); */
        printf(" ------------------------------------new thread !-=-------\n");
        if (pthread_create(&newthread , NULL, accept_request, (void *)&client_sock) != 0)  
            perror("pthread_create");
    }

    close(server_sock);

    return(0);
}

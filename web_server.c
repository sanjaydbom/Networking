#define _GNU_SOURCE
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <signal.h>

#include "libhttp.h"

static bool respond_bad_request(int connection_fd) {
    return http_start_response(connection_fd, 400) &&
           http_send_header(connection_fd, "Content-Type", "text/html") &&
           http_end_headers(connection_fd) &&
           http_send_string(connection_fd,
                            "<center><h1>400 Bad Request</h1><hr></center>");
}

static bool respond_forbidden(int connection_fd) {
    return http_start_response(connection_fd, 403) &&
           http_send_header(connection_fd, "Content-Type", "text/html") &&
           http_end_headers(connection_fd) &&
           http_send_string(connection_fd,
                            "<center><h1>403 Forbidden</h1><hr></center>");
}

static bool respond_not_found(int connection_fd) {
    return http_start_response(connection_fd, 404) &&
           http_send_header(connection_fd, "Content-Type", "text/html") &&
           http_end_headers(connection_fd) &&
           http_send_string(connection_fd,
                            "<center><h1>404 Not Found</h1><hr></center>");
}

static void handle_request(int connection_fd, const char *website_directory) {
    struct http_request *request = http_request_parse(connection_fd);

    /* Request path must start with "/". */
    if (request == NULL || request->path[0] != '/') {
        respond_bad_request(connection_fd);
        free(request);
        return;
    }

    /* Basic security mechanism: no requests may contain ".." in the path. */
    if (strstr(request->path, "..") != NULL) {
        respond_forbidden(connection_fd);
        free(request);
        return;
    }

    char dest[1000] = "";
    strcat(dest, website_directory);
    strcat(dest, request->path);
    struct stat file_stat;
    stat(dest, &file_stat);
    printf("Request: %s\n", request->path);
    //printf("File type: %s\n", http_get_mime_type(request->path));
    if(S_ISREG(file_stat.st_mode)) {
        FILE* f = fopen(dest, "rb");
        char* buffer = malloc(file_stat.st_size);
        fread(buffer, sizeof(char), file_stat.st_size, f);
        http_start_response(connection_fd, 200);
        printf("File type: %s\n", http_get_mime_type(request->path));
        http_send_header(connection_fd, "Content-Type", http_get_mime_type(request->path));
        char length[32] = "";
        sprintf(length, "%d",file_stat.st_size);
        http_send_header(connection_fd, "Content-Length", length);
        http_end_headers(connection_fd);
        send(connection_fd, buffer, file_stat.st_size, NULL);
        free(buffer);
        fclose(f);
        return;
    } else if(S_ISDIR(file_stat.st_mode)) {
        DIR* d;
        struct dirent* dir;
        d = opendir(dest);
        while((dir = readdir(d)) != NULL) {
            if(strcmp(dir->d_name, "index.html") == 0) {
                strcat(dest, "index.html");
                stat(dest, &file_stat);
                FILE* f = fopen(dest, "rb");
                char* buffer = malloc(file_stat.st_size);
                fread(buffer, sizeof(char), file_stat.st_size, f);
                http_start_response(connection_fd, 200);
                http_send_header(connection_fd, "Content-Type", "text/html");
                char length[32] = "";
                sprintf(length, "%d",file_stat.st_size);
                http_send_header(connection_fd, "Content-Length", length);
                http_end_headers(connection_fd);
                send(connection_fd, buffer, file_stat.st_size, 0);
                free(buffer);
                fclose(f);
                closedir(d);
                return;
            }
        }
        closedir(d);
        char html_buffer[1000]="";
        strcat(html_buffer, "<!DOCTYPE html>\n<html>\n<head>\n</head>\n<body>\n<a href=\"");
        //strcat(html_buffer, dest);
        strcat(html_buffer, "../\">Parent Directory</a><br>\n");
        d = opendir(dest);
        while((dir = readdir(d)) != NULL) {
            if(strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0) {
                strcat(html_buffer, "<a href=\"");
                //strcat(html_buffer, dest);
                //strcat(html_buffer, "/");
                strcat(html_buffer, dir->d_name);
                strcat(html_buffer, "\">");
                strcat(html_buffer, dir->d_name);
                strcat(html_buffer, "</a><br>\n");
            }
        }
        closedir(d);
        strcat(html_buffer, "</body>\n</html>\n");
        http_start_response(connection_fd, 200);
        http_send_header(connection_fd, "Content-Type", "text/html");
        http_end_headers(connection_fd);
        http_send_string(connection_fd, html_buffer);
        return;
    }
    /* In the starter code, just serve response for "file not found." */
    respond_not_found(connection_fd);
}

#ifdef WEB_SERVER_PLAIN

static void handle_connection(int server_socket, int connection_socket, const char *website_directory) {
    handle_request(connection_socket, website_directory);
    close(connection_socket);
}

#endif

#ifdef WEB_SERVER_PROCESSES

static void handle_connection(int server_socket, int connection_socket, const char *website_directory) {
    pid_t pid = fork();
    if(pid == 0) {
        handle_request(connection_socket, website_directory);
        close(connection_socket);
        exit(0);
    }
    else {
        close(connection_socket);
    }
}

#endif

#ifdef WEB_SERVER_THREADS
struct handle_request_args {
    char* dir;
    int conn_sock;
};

static void* handle_requests_helper(void* args){
    struct handle_request_args* arg = (struct handle_request_args*) args;
    handle_request(arg->conn_sock, arg->dir);
    close(arg->conn_sock);
    free(args);
    pthread_detach(pthread_self());
}

static void handle_connection(int server_socket, int connection_socket, const char *website_directory) {
    pthread_t thread_handle;
    struct handle_request_args* args = (struct handle_request_args*)malloc(sizeof(struct handle_request_args));
    args->dir = website_directory;
    args->conn_sock = connection_socket;
    pthread_create(&thread_handle, NULL, &handle_requests_helper, (void*)args);
}


#endif

int main(int argc, char **argv) {
    signal(SIGCHLD, SIG_IGN);
    if (argc < 3) {
        printf("Usage: %s <port> <website_directory>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int port = atoi(argv[1]);
    if (port == 0 || port > 65535) {
        fprintf(stderr, "invalid port\n");
        return EXIT_FAILURE;
    }

    int server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    int socket_option = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &socket_option,
                   sizeof(socket_option)) != 0) {
        perror("setsockopt(SO_REUSEADDR)");
        return EXIT_FAILURE;
    }

    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons((uint16_t) port);
    if (bind(server_socket, (struct sockaddr *) &server_address,
             sizeof(server_address)) != 0) {
        perror("bind");
        return EXIT_FAILURE;
    }
    listen(server_socket, SOMAXCONN);

    for (;;) {
        struct sockaddr_in client_address;
        socklen_t client_address_length = sizeof(client_address);
        int connection_socket =
            accept(server_socket, (struct sockaddr *) &client_address,
                   &client_address_length);
        if (connection_socket < 0) {
            perror("accept");
            return EXIT_FAILURE;
        }

        char address_string_buffer[16];

        if (inet_ntop(AF_INET, &client_address.sin_addr,
                      &address_string_buffer[0],
                      sizeof(address_string_buffer)) == NULL) {
            perror("inet_ntop");
            strcpy(&address_string_buffer[0], "<unknown>");
        }

        printf("Accepted connection from %s:%d\n", &address_string_buffer[0],
               (int) ntohs(client_address.sin_port));

        handle_connection(server_socket, connection_socket, argv[2]);
    }

    return EXIT_SUCCESS;
}

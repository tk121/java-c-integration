#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>

#define PORT 5000
#define MAX_EVENTS 100
#define BUFFER_SIZE 1024

void set_nonblocking(int fd) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}

void handle_request(char* buffer, char* response) {
    if (strstr(buffer, "processA") != NULL) {
        sprintf(response, "{\"status\":\"OK\",\"result\":{\"message\":\"Processed A\"}}");
    }
    else if (strstr(buffer, "processB") != NULL) {
        sprintf(response, "{\"status\":\"OK\",\"result\":{\"message\":\"Processed B\"}}");
    }
    else {
        sprintf(response, "{\"status\":\"ERROR\"}");
    }
}

int main() {

    int server_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, SOMAXCONN);

    set_nonblocking(server_fd);

    int epfd = epoll_create1(0);

    struct epoll_event ev, events[MAX_EVENTS];

    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &ev);

    printf("epoll TCP Server started on port %d\n", PORT);

    while (1) {

        int nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);

        for (int i = 0; i < nfds; i++) {

            if (events[i].data.fd == server_fd) {
                // 新規接続
                int client_fd;
                while ((client_fd = accept(server_fd, 
                        (struct sockaddr *)&address, 
                        (socklen_t*)&addrlen)) > 0) {

                    set_nonblocking(client_fd);

                    ev.events = EPOLLIN;
                    ev.data.fd = client_fd;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev);
                }
            }
            else {
                int client_fd = events[i].data.fd;

                char buffer[BUFFER_SIZE] = {0};
                char response[BUFFER_SIZE] = {0};

                int n = read(client_fd, buffer, BUFFER_SIZE);

                if (n <= 0) {
                    close(client_fd);
                    continue;
                }

                printf("Received: %s\n", buffer);

                handle_request(buffer, response);

                send(client_fd, response, strlen(response), 0);

                close(client_fd);
            }
        }
    }

    return 0;
}
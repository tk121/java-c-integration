#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 5000

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

    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    char buffer[1024] = {0};
    char response[1024] = {0};

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 3);

    printf("TCP Server started on port %d\n", PORT);

    while (1) {
        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);

        read(new_socket, buffer, 1024);
        printf("Received: %s\n", buffer);

        handle_request(buffer, response);

        send(new_socket, response, strlen(response), 0);
        close(new_socket);

        memset(buffer, 0, sizeof(buffer));
        memset(response, 0, sizeof(response));
    }

    return 0;
}

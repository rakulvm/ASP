#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>

#define BUFFER_SIZE 1024
#define DEFAULT_PORT 2024

void error(const char *msg) {
    perror(msg);
    exit(1);
}

int isValidCommand(const char *cmd) {
    const char *validCommands[] = {"dirlist -a", "quitc", "dirlist -t", NULL};

    for (int i = 0; validCommands[i] != NULL; i++) {
        if (strcmp(cmd, validCommands[i]) == 0) {
            return 1;
        }
    }

    if (strncmp(cmd, "w24fn ", 6) == 0 || strncmp(cmd, "w24ft ", 6) == 0 || 
        strncmp(cmd, "w24fz ", 6) == 0 || strncmp(cmd, "w24fdb ", 6) == 0 || 
        strncmp(cmd, "w24fda ", 6) == 0) {
        return 1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    int sockfd, portno = DEFAULT_PORT, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    char buffer[BUFFER_SIZE];
    char *hostname = "localhost"; // Default to localhost if not specified

    if (argc > 1) {
        hostname = argv[1];
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("ERROR opening socket");

    server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(1);
    }

    bzero((char *)&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(portno);

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) 
        error("ERROR connecting");

    while (1) {
        printf("$clientw24: ");
        bzero(buffer, BUFFER_SIZE);
        fgets(buffer, BUFFER_SIZE - 1, stdin);
        buffer[strcspn(buffer, "\n")] = 0;

        if (!isValidCommand(buffer)) {
            printf("Invalid command.\n");
            continue;
        }

        n = write(sockfd, buffer, strlen(buffer));
        if (n < 0) error("ERROR writing to socket");

        if (strncmp(buffer, "quitc", 5) == 0) break;

        bzero(buffer, BUFFER_SIZE);
        n = read(sockfd, buffer, BUFFER_SIZE - 1);
        if (n < 0 && errno != EAGAIN) error("ERROR reading from socket");
        printf("%s", buffer); // Print server's response

        if (strncmp(buffer, "redirect ", 9) == 0) { // Server sends "redirect <port>"
            sscanf(buffer, "redirect %d", &portno);
            close(sockfd); // Close the old connection
            sockfd = socket(AF_INET, SOCK_STREAM, 0);
            serv_addr.sin_port = htons(portno); // Update port number
            if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) 
                error("ERROR reconnecting");
        }
    }

    close(sockfd);
    return 0;
}

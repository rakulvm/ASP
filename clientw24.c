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

void error(const char *msg) {
    perror(msg);
    exit(1); // Exit with error status
}

// Function to check if the command is valid
int isValidCommand(const char *cmd) {
    const char *validCommands[] = {"dirlist -a", "quitc", "dirlist -t", NULL}; 
    for (int i = 0; validCommands[i] != NULL; i++) {
        if (strcmp(cmd, validCommands[i]) == 0) {
            return 1; // Command is valid
        }
    }
    return 0; // Command is not valid
}

int main(int argc, char *argv[]) {
    int sockfd, portno, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    char buffer[BUFFER_SIZE];

    if (argc < 3) {
        fprintf(stderr,"usage %s hostname port\n", argv[0]);
        exit(1);
    }

    portno = atoi(argv[2]);
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("ERROR opening socket");

    server = gethostbyname(argv[1]);
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
        buffer[strcspn(buffer, "\n")] = 0; // Remove newline character from the
        
                if (!isValidCommand(buffer)) {
            printf("Invalid command.\n");
            continue;
        }

        n = write(sockfd, buffer, strlen(buffer));
        if (n < 0) error("ERROR writing to socket");

        if (strncmp(buffer, "quitc", 5) == 0) break; // Quit command issued

        // Assume server sends all response data for a command at once:
        do {
            bzero(buffer, BUFFER_SIZE);
            n = read(sockfd, buffer, BUFFER_SIZE - 1);
            if (n > 0) {
                printf("%s", buffer);  // Print server's response
            }
        } while (n > 0);  // Continue reading until no more data

        // After reading from the socket, check if the message is "END"
        if (strcmp(buffer, "END") == 0) {
            break;
        }

        if (n < 0 && errno != EAGAIN) error("ERROR reading from socket");
    }

    close(sockfd);
    return 0;
}

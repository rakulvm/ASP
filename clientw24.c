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
    // Updated list of commands without w24fn since we'll check it separately
    const char *validCommands[] = {"dirlist -a", "quitc", "dirlist -t", NULL};

    // Check fixed commands
    for (int i = 0; validCommands[i] != NULL; i++) {
        if (strcmp(cmd, validCommands[i]) == 0) {
            return 1; // Command is valid
        }
    }
    
    // Check if the command starts with "w24fn " (note the space after w24fn)
    if (strncmp(cmd, "w24fn ", 6) == 0) {
        return 1; // Command is valid if it starts with "w24fn "
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

    // Inside your main while loop, you already send any input to the server:
    while (1) {
        printf("$clientw24: ");
        bzero(buffer, BUFFER_SIZE);
        fgets(buffer, BUFFER_SIZE - 1, stdin);
        buffer[strcspn(buffer, "\n")] = 0; // Remove newline character from the end
        
        if (!isValidCommand(buffer)) {
            printf("Invalid command.\n");
            continue; // Skip sending invalid command
        }

        // Send valid command to the server
        n = write(sockfd, buffer, strlen(buffer));
        if (n < 0) error("ERROR writing to socket");

        // Quit command issued by client
        if (strncmp(buffer, "quitc", 5) == 0) break;

        // Print server's response
        do {
            bzero(buffer, BUFFER_SIZE);
            n = read(sockfd, buffer, BUFFER_SIZE - 1);
            if (n > 0) {
                printf("%s", buffer);  // Print server's response
            }
        } while (n > 0);  // Continue reading until no more data

        if (strcmp(buffer, "END") == 0) {
            continue; // Prepare for next command
        }

        if (n < 0 && errno != EAGAIN) error("ERROR reading from socket");
        }
    close(sockfd);
    return 0;
}

/*
 * This code is licensed under the Attribution-NonCommercial-NoDerivatives 4.0 International license.
 *
 * Author: D'Arcy Smith (ds@programming101.dev)
 *
 * You are free to:
 *   - Share: Copy and redistribute the material in any medium or format.
 *   - Under the following terms:
 *       - Attribution: You must give appropriate credit, provide a link to the license, and indicate if changes were made.
 *       - NonCommercial: You may not use the material for commercial purposes.
 *       - NoDerivatives: If you remix, transform, or build upon the material, you may not distribute the modified material.
 *
 * For more details, please refer to the full license text at:
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void           parse_arguments(int argc, char *argv[], char **address, char **port, char **command);
static void           handle_arguments(const char *binary_name, const char *address, const char *port_str, in_port_t *port);
static in_port_t      parse_in_port_t(const char *binary_name, const char *port_str);
_Noreturn static void usage(const char *program_name, int exit_code, const char *message);
static void           convert_address(const char *address, struct sockaddr_storage *addr);
static int            socket_create(int domain, int type, int protocol);
static void           socket_connect(int sockfd, struct sockaddr_storage *addr, in_port_t port);
static void           socket_close(int sockfd);
static int            send_to_server(const char *arg, int sockfd);

#define UNKNOWN_OPTION_MESSAGE_LEN 24
#define BASE_TEN 10
#define LENGTH 1024

int main(int argc, char *argv[])
{
    char                   *address;
    char                   *port_str;
    in_port_t               port;
    int                     sockfd;
    struct sockaddr_storage addr;

    // A4 additions
    char *command;

    address  = NULL;
    port_str = NULL;
    parse_arguments(argc, argv, &address, &port_str, &command);
    handle_arguments(argv[0], address, port_str, &port);
    convert_address(address, &addr);
    sockfd = socket_create(addr.ss_family, SOCK_STREAM, 0);
    socket_connect(sockfd, &addr, port);

    send_to_server(command, sockfd);

    socket_close(sockfd);

    return EXIT_SUCCESS;
}

static void parse_arguments(int argc, char *argv[], char **address, char **port, char **command)
{
    int opt;

    opterr = 0;

    while((opt = getopt(argc, argv, "h")) != -1)
    {
        switch(opt)
        {
            case 'h':
            {
                usage(argv[0], EXIT_SUCCESS, NULL);
            }
            case '?':
            {
                char message[UNKNOWN_OPTION_MESSAGE_LEN];

                snprintf(message, sizeof(message), "Unknown option '-%c'.", optopt);
                usage(argv[0], EXIT_FAILURE, message);
            }
            default:
            {
                usage(argv[0], EXIT_FAILURE, NULL);
            }
        }
    }

    //    if(optind + 1 >= argc)
    //    {
    //        usage(argv[0], EXIT_FAILURE, "Too few arguments.");
    //    }
    //
    //    if(optind < argc - 3)
    //    {
    //        usage(argv[0], EXIT_FAILURE, "Too many arguments.");
    //    }
    if(optind >= argc)
    {
        usage(argv[0], EXIT_FAILURE, "The ip address, port and command are required");
    }

    if(optind + 1 >= argc)
    {
        usage(argv[0], EXIT_FAILURE, "The port and command are required");
    }

    if(optind + 2 >= argc)
    {
        usage(argv[0], EXIT_FAILURE, "The execv command is required");
    }

    if(optind < argc - 3)
    {
        usage(argv[0], EXIT_FAILURE, "Error: Too many arguments.");
    }
    *address = argv[optind];
    *port    = argv[optind + 1];
    *command = argv[optind + 2];

    //    printf("Command: %s\n", *command);
}

static void handle_arguments(const char *binary_name, const char *address, const char *port_str, in_port_t *port)
{
    if(address == NULL)
    {
        usage(binary_name, EXIT_FAILURE, "The port is required.");
    }

    if(port_str == NULL)
    {
        usage(binary_name, EXIT_FAILURE, "The address is required.");
    }

    *port = parse_in_port_t(binary_name, port_str);
}

in_port_t parse_in_port_t(const char *binary_name, const char *str)
{
    char     *endptr;
    uintmax_t parsed_value;

    errno        = 0;
    parsed_value = strtoumax(str, &endptr, BASE_TEN);

    if(errno != 0)
    {
        perror("Error parsing in_port_t");
        exit(EXIT_FAILURE);
    }

    // Check if there are any non-numeric characters in the input string
    if(*endptr != '\0')
    {
        usage(binary_name, EXIT_FAILURE, "Invalid characters in input.");
    }

    // Check if the parsed value is within the valid range for in_port_t
    if(parsed_value > UINT16_MAX)
    {
        usage(binary_name, EXIT_FAILURE, "in_port_t value out of range.");
    }

    return (in_port_t)parsed_value;
}

_Noreturn static void usage(const char *program_name, int exit_code, const char *message)
{
    if(message)
    {
        fprintf(stderr, "%s\n", message);
    }

    fprintf(stderr, "Usage: %s [-h] <address> <port>\n", program_name);
    fputs("Options:\n", stderr);
    fputs("  -h  Display this help message\n", stderr);
    exit(exit_code);
}

static void convert_address(const char *address, struct sockaddr_storage *addr)
{
    memset(addr, 0, sizeof(*addr));

    if(inet_pton(AF_INET, address, &(((struct sockaddr_in *)addr)->sin_addr)) == 1)
    {
        addr->ss_family = AF_INET;
    }
    else if(inet_pton(AF_INET6, address, &(((struct sockaddr_in6 *)addr)->sin6_addr)) == 1)
    {
        addr->ss_family = AF_INET6;
    }
    else
    {
        fprintf(stderr, "%s is not an IPv4 or an IPv6 address\n", address);
        exit(EXIT_FAILURE);
    }
}

static int socket_create(int domain, int type, int protocol)
{
    int sockfd;
    sockfd = socket(domain, type, protocol);

    if(sockfd == -1)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

static void socket_connect(int sockfd, struct sockaddr_storage *addr, in_port_t port)
{
    char      addr_str[INET6_ADDRSTRLEN];
    in_port_t net_port;
    socklen_t addr_len;

    if(inet_ntop(addr->ss_family, addr->ss_family == AF_INET ? (void *)&(((struct sockaddr_in *)addr)->sin_addr) : (void *)&(((struct sockaddr_in6 *)addr)->sin6_addr), addr_str, sizeof(addr_str)) == NULL)
    {
        perror("inet_ntop");
        exit(EXIT_FAILURE);
    }

    printf("Connecting to: %s:%u\n", addr_str, port);
    net_port = htons(port);

    if(addr->ss_family == AF_INET)
    {
        struct sockaddr_in *ipv4_addr;

        ipv4_addr           = (struct sockaddr_in *)addr;
        ipv4_addr->sin_port = net_port;
        addr_len            = sizeof(struct sockaddr_in);
    }
    else if(addr->ss_family == AF_INET6)
    {
        struct sockaddr_in6 *ipv6_addr;

        ipv6_addr            = (struct sockaddr_in6 *)addr;
        ipv6_addr->sin6_port = net_port;
        addr_len             = sizeof(struct sockaddr_in6);
    }
    else
    {
        fprintf(stderr, "Invalid address family: %d\n", addr->ss_family);
        exit(EXIT_FAILURE);
    }

    if(connect(sockfd, (struct sockaddr *)addr, addr_len) == -1)
    {
        char *msg;

        msg = strerror(errno);
        fprintf(stderr, "Error: connect (%d): %s\n", errno, msg);
        exit(EXIT_FAILURE);
    }

    printf("Connected to: %s:%u\n", addr_str, port);
}

static void socket_close(int sockfd)
{
    if(close(sockfd) == -1)
    {
        perror("Error closing socket - client");
        exit(EXIT_FAILURE);
    }
}

static int send_to_server(const char *arg, int sockfd)
{
    //    write(STDOUT_FILENO, arg, sizeof(arg));

    uint16_t size = (uint16_t)strlen(arg);
    char     buffer[LENGTH];
    //    printf("Arg length: %d\n", size);
    //    unsigned long size = strlen(arg);
    send(sockfd, &size, sizeof(uint16_t), 0);
    write(sockfd, arg, size);

    while(1)
    {
        ssize_t bytes_read;

        bytes_read = read(sockfd, buffer, LENGTH);

        if(bytes_read <= 0)
        {
            break;    // Input closed or error
        }

        write(STDOUT_FILENO, buffer, (size_t)bytes_read);
    }

    // socket_close(sockfd);

    return EXIT_SUCCESS;
}

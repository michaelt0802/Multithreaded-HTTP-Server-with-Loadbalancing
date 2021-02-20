#include <err.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/time.h>
#include <limits.h>

#include "queue.h"

#define BUFFER_SIZE 4096
#define INTERNAL_SERVER_ERROR "HTTP/1.1 500 Interal Server Error\r\nContent-Length: 0\r\n\r\n"

struct worker_t {
    int worker_id;
    int server_port;
    ssize_t server_socket;
    ssize_t client_socket;
    pthread_t thread_fd;
    pthread_cond_t condition_variable;
    bool busy;

};
typedef struct worker_t worker;

struct server_info_t {
    int server_id;
    int server_port;
    bool server_status; // initialed false
    int recieved_requests; // initialed 0
    int total_errors; // initialed 0
    pthread_mutex_t server_mut;
};
typedef struct server_info_t server_info;

struct servers_t {
    int server_count;
    server_info *servers;
    int load_balancer_port;
};
typedef struct servers_t server;

struct healthcheck_t {
    int requests;
    int flag;
    pthread_t thread_fd;
    pthread_cond_t condition_variable;
    pthread_mutex_t healthcheck_mut;
};
typedef struct healthcheck_t healthcheck;

/*
 * client_connect takes a port number and establishes a connection as a client.
 * connectport: port number of server to connect to
 * returns: valid socket if successful, -1 otherwise
 */
int client_connect(uint16_t connectport);

/*
 * server_listen takes a port number and creates a socket to listen on
 * that port.
 * port: the port number to receive connections
 * returns: valid socket if successful, -1 otherwise
 */
int server_listen(int port);

/*
 * bridge_connections send up to BUFFER_SIZE bytes from fromfd to tofd
 * fromfd, tofd: valid sockets
 * returns: number of bytes sent, 0 if connection closed, -1 on error
 */
int bridge_connections(int fromfd, int tofd);

/*
 * bridge_loop forwards all messages between both sockets until the connection
 * is interrupted. It also prints a message if both channels are idle.
 * sockfd1, sockfd2: valid sockets
 */
void bridge_loop(int sockfd1, int sockfd2);

int healthcheck_connect(int i);

// healthcheck periodic loop
void healthcheck_probe();

// helper function that predictively increments a servers total requests by 1
void increment_requests(int i);

// Pick server that is Least Frequently Used on requests
// In case of tie give it to server with most total errors
int determine_best_server();

void update_servers_on_event();

/*
    healthcheck thread that probes all servers for healthcheck data
    periodically every R requests or seconds of request_timer
    whichever comes first
*/
void* healthcheck_checker_thread(void* health_struct);

void* worker_thread(void *worker_thread);

void* dispatcher(void * tc);

int init_healthcheck(healthcheck* health);

int init_workers(int thread_count);

int init_dispatcher(int thread_count);

int init_servers(int argc, char **argv, int start_of_ports);

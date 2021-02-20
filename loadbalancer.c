#include "loadbalancer.h"

// With help from Clark and Michael's section videos

pthread_mutex_t mutex_queue = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  queue_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t worker_lock = PTHREAD_MUTEX_INITIALIZER;

const int request_timer = 5;

server servers;

worker *workers;

int client_connect(uint16_t connectport) {
    int connfd;
    struct sockaddr_in servaddr;

    connfd=socket(AF_INET,SOCK_STREAM,0);
    if (connfd < 0)
        return -1;
    memset(&servaddr, 0, sizeof servaddr);

    servaddr.sin_family=AF_INET;
    servaddr.sin_port=htons(connectport);

    /* For this assignment the IP address can be fixed */
    inet_pton(AF_INET,"127.0.0.1",&(servaddr.sin_addr));

    // connect will return -1 if server is down
    if(connect(connfd,(struct sockaddr *)&servaddr,sizeof(servaddr)) < 0)
        return -1;
    return connfd;
}

int server_listen(int port) {
    int listenfd;
    int enable = 1;
    struct sockaddr_in servaddr;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0)
        return -1;
    memset(&servaddr, 0, sizeof servaddr);
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);

    if(setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0)
        return -1;
    if (bind(listenfd, (struct sockaddr*) &servaddr, sizeof servaddr) < 0)
        return -1;
    if (listen(listenfd, 500) < 0)
        return -1;
    return listenfd;
}

int bridge_connections(int fromfd, int tofd) {
    char recvline[100];
    int n = recv(fromfd, recvline, 100, 0);
    if (n < 0) {
        printf("connection error receiving\n");
        return -1;
    } else if (n == 0) {
        printf("receiving connection ended\n");
        return 0;
    }
    recvline[n] = '\0';
    //usleep(500000);
    n = send(tofd, recvline, n, 0);
    if (n < 0) {
        printf("connection error sending\n");
        return -1;
    } else if (n == 0) {
        printf("sending connection ended\n");
        return 0;
    }
    return n;
}

void bridge_loop(int sockfd1, int sockfd2) {
    fd_set set;
    struct timeval timeout;

    int fromfd, tofd;
    while(1) {
        // set for select usage must be initialized before each select call
        // set manages which file descriptors are being watched
        FD_ZERO (&set);
        FD_SET (sockfd1, &set);
        FD_SET (sockfd2, &set);

        // same for timeout
        // max time waiting, 5 seconds, 0 microseconds
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        char buffer[BUFFER_SIZE + 1];
        int res;

        // select return the number of file descriptors ready for reading in set
        switch (select(FD_SETSIZE, &set, NULL, NULL, &timeout)) {
            case -1:
                printf("error during select, exiting\n");
                memset(buffer, '\0', BUFFER_SIZE + 1);
                snprintf(buffer, BUFFER_SIZE, INTERNAL_SERVER_ERROR);

                res = send(sockfd1, buffer, BUFFER_SIZE, 0);

                if (res < 0) {
                    printf("Error writing Internal Server Error on server: %d\n", sockfd2);
                }
                return;
            case 0:
                printf("Server timed out\n");
                memset(buffer, '\0', BUFFER_SIZE + 1);
                snprintf(buffer, BUFFER_SIZE, INTERNAL_SERVER_ERROR);

                res = send(sockfd1, buffer, BUFFER_SIZE, 0);

                if (res < 0) {
                    printf("Error writing Internal Server Error on server: %d\n", sockfd2);
                }

                return;
            default:
                if (FD_ISSET(sockfd1, &set)) {
                    fromfd = sockfd1;
                    tofd = sockfd2;
                } else if (FD_ISSET(sockfd2, &set)) {
                    fromfd = sockfd2;
                    tofd = sockfd1;
                } else {
                    printf("this should be unreachable\n");
                    return;
                }
        }
        if (bridge_connections(fromfd, tofd) <= 0)
            return;
    }
}

int healthcheck_connect(int iris) {
    int id = iris;
    int connfd;
    uint16_t connectport = (uint16_t) servers.servers[id].server_port;
    if ((connfd = client_connect(connectport)) < 0) {
        printf("Failed connecting on server %d\n", servers.servers[id].server_port);
        return -1;
    }

    char buffer[BUFFER_SIZE + 1];
    memset(buffer, '\0', BUFFER_SIZE + 1);
    snprintf(buffer, BUFFER_SIZE, "GET /healthcheck HTTP/1.1\r\n\r\n");

    int res = send(connfd, buffer, BUFFER_SIZE, 0);

    if (res < 0) {
        printf("connection error receiving on server %d\n", servers.servers[id].server_port);
        close(connfd);
        return -1;
    }
    memset(buffer, '\0', BUFFER_SIZE + 1);

    ssize_t writ_length = 0;
    do {
        writ_length = recv(connfd, buffer + writ_length, BUFFER_SIZE - writ_length, 0);
    } while (writ_length > 0);

    // parse response into httpRequest object
    char con_len[10];
    char fail[10];
    char total[10];

    sscanf((const char *)buffer, "%*[HTTP/1.1 ]%*s %*s%*[\r\nContent-Length: ]%s%*[\r\n\r\n]%s%*[\n]%s", con_len, fail, total);

    int con_int = atoi(con_len);
    pthread_mutex_lock(&servers.servers[id].server_mut);
    servers.servers[id].total_errors = atoi(fail);
    servers.servers[id].recieved_requests = atoi(total);
    pthread_mutex_unlock(&servers.servers[id].server_mut);
    // printf("con_int: %d\n", con_int);
    // printf("Server %d errors: %d\n", i,  servers.servers[i].total_errors);
    // printf("Server %d total: %d\n", i, servers.servers[i].recieved_requests);

    close(connfd);

    if(con_int <= 0) {
        return -1;
    }

    return 0;
}

int healthcheck_count = 0; //TODO DELETE THIS
void healthcheck_probe() {
    // interate through servers and do a health check_method
    for(int i = 0; i < servers.server_count; i++) {
        //pthread_mutex_trylock(&servers.servers[i].server_mut); //use trylock?? to avoid deadlock?
        // healthcheck on servers.servers[i].port

        int ret = healthcheck_connect(i);
        pthread_mutex_lock(&servers.servers[i].server_mut);
        if(ret < 0) {
            servers.servers[i].server_status = false;
        }
        else {
            servers.servers[i].server_status = true;
        }
        printf("Server %d status: %d\n", i, servers.servers[i].server_status);
        pthread_mutex_unlock(&servers.servers[i].server_mut);
    }

    printf("Healthcheck probe %d complete\n\n", healthcheck_count++);
}

void increment_requests(int i) {
    pthread_mutex_lock(&servers.servers[i].server_mut);
    servers.servers[i].recieved_requests++;
    pthread_mutex_unlock(&servers.servers[i].server_mut);
}

int determine_best_server() {
    int min = INT_MAX;
    int best_server = 0;
    for(int i = 0; i < servers.server_count; i++) {
        if(servers.servers[i].server_status == true) {
            pthread_mutex_lock(&servers.servers[i].server_mut);
            if(servers.servers[i].recieved_requests < min) {
                best_server = i;
                min = servers.servers[i].recieved_requests;
            }
            else if(servers.servers[i].recieved_requests == min) {
                pthread_mutex_lock(&servers.servers[best_server].server_mut);
                if(servers.servers[i].total_errors > servers.servers[best_server].total_errors)
                    best_server = i;
                pthread_mutex_unlock(&servers.servers[best_server].server_mut);
            }

            pthread_mutex_unlock(&servers.servers[i].server_mut);
        }
    }

    return best_server;
}

// TODO have int flag here for specific event?
void update_servers_on_event() {
    // if server goes down during healthcheck_probe
    // increment total requests after closing connection
    //pthread_mutex_lock(&servers.servers.server_mut);
    // TBD
    //pthread_mutex_unlock(&servers.servers.server_mut);
}

void* healthcheck_checker_thread(void* health_struct) {
    healthcheck* health = (healthcheck*) health_struct;
    struct timespec ts;
    struct timeval now;
    while(1) {
        pthread_mutex_lock(&health->healthcheck_mut);

        memset(&ts, 0, sizeof(ts));

        gettimeofday(&now, NULL);

        ts.tv_sec = now.tv_sec + request_timer;

        pthread_cond_timedwait(&health->condition_variable, &health->healthcheck_mut, &ts);

        // if(health->flag) {
        //     pthread_mutex_unlock(&health->healthcheck_mut); }
        health->requests = 0;
        // health->flag = 0;
        pthread_mutex_unlock(&health->healthcheck_mut);
        healthcheck_probe();
    }
}

void* worker_thread(void *worker_thread) {
    worker* worker_t = (worker *) worker_thread;
    while (true) {
        //pthread_mutex_lock(&worker_lock);
        while (worker_t->client_socket < 0) {

            if (pthread_cond_wait(&worker_t->condition_variable, &worker_lock)) {
                perror("worker_thread");
            }
        }
        int best_server = determine_best_server();

        int connfd;
        uint16_t connectport = servers.servers[best_server].server_port; //from client
        if ((connfd = client_connect(connectport)) < 0) {
            printf("worker failed connecting\n");
            char buffer[BUFFER_SIZE + 1];
            memset(buffer, '\0', BUFFER_SIZE + 1);
            snprintf(buffer, BUFFER_SIZE, INTERNAL_SERVER_ERROR);

            int res = send(worker_t->client_socket, buffer, BUFFER_SIZE, 0);
            if(res < 0) {
                printf("UHHH what?\n");
            }
        }
        else {
            bridge_loop(worker_t->client_socket, connfd);
        }

        increment_requests(best_server);
        close(worker_t->client_socket);
        worker_t->client_socket = -1;
        worker_t->busy = false;
    }
}

void* dispatcher(void * tc) {
    int thread_count = *(int *) tc;
    while (true) {
        int i = 0;
        int *psock_fd;
        while (true) {
            if (workers[i].busy == false) {
                pthread_mutex_lock(&mutex_queue);
                if ((psock_fd = dequeue()) == NULL) {
                    pthread_cond_wait(&queue_cond, &mutex_queue);
                    psock_fd = dequeue();
                }

                pthread_mutex_unlock(&mutex_queue);


                workers[i].client_socket = *psock_fd;
                workers[i].busy = true;
                free(psock_fd);
                int error = pthread_cond_signal(&workers[i].condition_variable);
                if (error != 0) {
                    write(STDERR_FILENO, "Error signaling thread\n", 50);
                    exit(EXIT_FAILURE);
                }

                break;
            }
            ++i;
            if (i >= thread_count){
                i = 0;
            }
        }
    }
}

int init_healthcheck(healthcheck* health) {
    health->requests = 0;
    health->condition_variable = (pthread_cond_t) PTHREAD_COND_INITIALIZER;
    health->healthcheck_mut = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
    int ret = pthread_create(&health->thread_fd, NULL, healthcheck_checker_thread,
            (void *) health);
    if (ret != 0) {
        write(STDERR_FILENO, "Error creating healthcheck thread\n", 100);
        return -1;
    }

    return 0;
}

int init_workers(int thread_count) {
    // allocate memory for worker array
    workers = malloc(thread_count * sizeof(worker));
    for(int i = 0; i < thread_count; i++) {
        workers[i].worker_id = i;
        workers[i].server_port = -1;
        workers[i].client_socket = -1;
        workers[i].condition_variable = (pthread_cond_t) PTHREAD_COND_INITIALIZER;
        workers[i].busy = false;
        int ret = pthread_create(&workers[i].thread_fd, NULL, worker_thread,
                (void *) &workers[i]);
        if (ret != 0) {
            printf("Error creating worker thread thread\n");
            return -1;
        }

    }

    return 0;
}

int init_dispatcher(int thread_count) {
    // initialze worker threads
    int ret = init_workers(thread_count);
    if(ret < 0) {
        printf("Error on server init\n");
        exit(EXIT_FAILURE);
    }

    pthread_t dispatch_fd;
    //sleep(1);
    ret = pthread_create(&dispatch_fd, NULL, dispatcher, (void*) &thread_count);
    if (ret < 0) {
        printf("Error creating dispatcher");
        return -1;
    }

    return 0;
}

int init_servers(int argc, char **argv, int start_of_ports) {
    servers.server_count = argc - start_of_ports;
    if(servers.server_count <= 0) {
        printf("No httpserver ports provided\n");
        return -1;
    }

    // allocate memory for server struct array
    // TODO to be freed later?
    servers.servers = malloc(servers.server_count * sizeof(server_info));
    // initialze server info structs
    int count = 0;
    for(int i = start_of_ports; i < argc; i++) {
        int port = atoi(argv[i]);
        if (port < 0) {
            printf("Invalid port number provided\n");
            return -1;
        }
        servers.servers[count].server_id = count;
        servers.servers[count].server_port = port;
        servers.servers[count].server_status = false;
        servers.servers[count].recieved_requests = 0;
        servers.servers[count].total_errors = 0;
        pthread_mutex_init(&servers.servers[count].server_mut, NULL);
        count++;
    }

    return 0;
}

int main(int argc,char **argv) {
    if (argc < 1) {
        write(STDERR_FILENO, "missing arugments\n", 18);
        exit(EXIT_FAILURE);
    }
    // parse arguments for flags and port number
    extern char *optarg;
    extern int optind;
    int c;
    int thread_count = 4;
    int request_count = 5;

    while ((c = getopt(argc, argv, "N:R:")) != -1) {
        switch (c) {
            case 'N':
                thread_count = atoi(optarg);
                if (thread_count < 1) {
                    write(STDERR_FILENO, "N must be 1 or greater\n", 25);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'R':
                request_count = atoi(optarg);
                if (request_count < 1) {
                    write(STDERR_FILENO, "R must be 1 or greater\n", 25);
                    exit(EXIT_FAILURE);
                }
                break;
            case '?':
                exit(EXIT_FAILURE);
                break;
        }
    }
    servers.load_balancer_port = atoi(argv[optind++]);

    // initialze server structs
    int ret = init_servers(argc, argv, optind);
    if(ret < 0) {
        printf("Error on server init\n");
        exit(EXIT_FAILURE);
    }
    // start healthcheck probe thread
    healthcheck health;
    ret = init_healthcheck(&health);
    if(ret < 0) {
        printf("Error on healthcheck init\n");
        exit(EXIT_FAILURE);
    }


    healthcheck_probe();

    // initialze worker threads
    // ret = init_workers(thread_count);
    // if(ret < 0) {
    //     printf("Error on server init\n");
    //     exit(EXIT_FAILURE);
    // }

    // initialze dispatcher thread
    // also initialzes workers
    ret = init_dispatcher(thread_count);
    if(ret < 0) {
        printf("Error on server init\n");
        exit(EXIT_FAILURE);
    }

    // take in requests from clients and put them on a queue
    // every R requests, signal the healthcheck_checker_thread condition_variable
        // have the thread reset requests if reach timer
    //int connfd;
    int listenfd, acceptfd;
    bool servers_available;

    char internal_server_error_message[BUFFER_SIZE + 1];
    memset(internal_server_error_message, '\0', BUFFER_SIZE + 1);
    snprintf(internal_server_error_message, BUFFER_SIZE, INTERNAL_SERVER_ERROR);

    if ((listenfd = server_listen(servers.load_balancer_port)) < 0)
        err(1, "failed listening");
    while(true) {
        if ((acceptfd = accept(listenfd, NULL, NULL)) < 0) {
            printf("failed\n");
            err(1, "failed accepting");
        }

        if(++health.requests == request_count) {
            printf("Max Requests Reached Probing Server...\n");
            // health.flag = 1;
            pthread_cond_signal(&health.condition_variable);
        }
        // check for any server availble to work
        servers_available = false;
        for(int i = 0; i < servers.server_count; i++) {
            if(servers.servers[i].server_status == true) {
                servers_available = true;
                break;
            }
        }
        if(!servers_available) {
            printf("No servers availble for work\n");
            int res = send(acceptfd, internal_server_error_message, BUFFER_SIZE, 0);
            if (res < 0) {
                printf("Error writing Internal Server Error to client: %d\n", acceptfd);
            }
            close(acceptfd);
            continue;
        }
        int *pclient = malloc(sizeof(int));
        *pclient = acceptfd;

        pthread_mutex_lock(&mutex_queue);
        pthread_cond_signal(&queue_cond);
        enqueue(pclient);
        pthread_mutex_unlock(&mutex_queue);
    }
}

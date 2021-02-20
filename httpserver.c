#include "http.h"
#include "threads.h"

int log_fd = 0;
size_t thread_count = 0;
int lflag = 0;
ssize_t offset = 0;
ssize_t log_total = 0;
ssize_t log_error = 0;

pthread_mutex_t mutex_queue = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t wait_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  queue_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutex_log = PTHREAD_MUTEX_INITIALIZER;

int countdigits(int num) {
    int count = 0;

    do
    {
        count++;
        num /= 10;
    } while(num != 0);

    return count;
}

ssize_t countdigitslog() {
    pthread_mutex_lock(&mutex_log);
    ssize_t total = countdigits(log_total) + countdigits(log_error);
    pthread_mutex_unlock(&mutex_log);
    return total;
}

ssize_t return_log_error() {
    pthread_mutex_lock(&mutex_log);
    ssize_t temp = log_error;
    pthread_mutex_unlock(&mutex_log);
    return temp;
}

ssize_t return_log_total() {
    pthread_mutex_lock(&mutex_log);
    ssize_t temp = log_total;
    pthread_mutex_unlock(&mutex_log);
    return temp;
}

void errorlogger(struct httpObject tvars) {
    char buff[70];
    ssize_t incr;
    incr = snprintf(buff, 70, "FAIL: %s /%s HTTP/1.1 --- response %d\n========\n", tvars.method, tvars.filename,  tvars.status_code);
    pthread_mutex_lock(&mutex_log);
    log_error += 1;
    log_total += 1;
    ssize_t tempoffset = offset;
    offset += incr;
    pthread_mutex_unlock(&mutex_log);
    pwrite(log_fd, buff, incr, tempoffset);

    return;
}

void logger(struct httpObject* tvars, uint8_t* buffer) {
    uint8_t chars[BUFFER_SIZE];
    ssize_t incr;
    memcpy(chars, buffer, BUFFER_SIZE);
    char buff[70];
    if(tvars->chars_logged == 0){
        incr = snprintf(buff, 70, "%s /%s length %ld\n", tvars->method,  tvars->filename,  tvars->content_length);
        pwrite(log_fd, buff, incr, tvars->poffset);
        tvars->poffset += incr;
    }

    uint8_t * c = chars;
    for (ssize_t i = tvars->chars_logged; i < (tvars->content_length - tvars->chars_logged); i++) {
        if ((i % 20) == 0) {
            if (i != 0) {
                incr = snprintf(buff, 70, "\n");
                pwrite(log_fd, buff, incr, tvars->poffset);
                tvars->poffset += incr;
            }
            incr = snprintf(buff, 70, "%07ld", (i/10));
            pwrite(log_fd, buff, incr, tvars->poffset);
            tvars->poffset += incr;
            incr = snprintf(buff, 70, "0");
            pwrite(log_fd, buff, incr, tvars->poffset);
            tvars->poffset += incr;
        }
        incr = snprintf(buff, 70, " %02x", c[i]);
        pwrite(log_fd, buff, incr, tvars->poffset);
        tvars->poffset += incr;
    }
        incr = snprintf(buff, 70, "\n========\n");
        pwrite(log_fd, buff, incr, tvars->poffset);
        tvars->poffset += incr;
}

// calculates amount of space to write to log file as well as incrementing the
// global offset variable by that amount
ssize_t calculate_offset(struct httpObject tvars) {
    pthread_mutex_lock(&mutex_log);
    log_total += 1;
    ssize_t length = tvars.content_length;
    ssize_t total = strlen(tvars.method) + strlen(tvars.filename) + countdigits(length) + 11;
    total += (69*(length/20))+((length%20)*3+9) + 9;
    ssize_t tempoffset = offset;
    offset += total;
    pthread_mutex_unlock(&mutex_log);
    return tempoffset;
}

void* worker_thread(void* worker)
{

    worker_struct_t* wStruct = (worker_struct_t*) worker;
    while (true) {
        //pthread_mutex_lock(&wait_lock);
        while (wStruct->client_sockd < 0) {

            if (pthread_cond_wait(&wStruct->cond, &wait_lock)) {
                perror("worker_thread");
            }
        }
        printf("Worker %d now working on socket = %d\n", wStruct->id,
                wStruct->client_sockd);
        send_response(process_request(read_http_request(wStruct->client_sockd, lflag)));
        printf("Wrorker %d finished working on socket = %d\n", wStruct->id,
                wStruct->client_sockd);

        close(wStruct->client_sockd);
        wStruct->client_sockd = -1;
    }
}

void* dispatcher(void* runtime_info)
{
    thread_runtime_t* runtime = (thread_runtime_t*) runtime_info;
    worker_struct_t workers[runtime->thread_count];

    // create the worker pool threads
    for (int i = 0; i < runtime->thread_count; i++) {
        // printf("creating thread %d\n", i);
        workers[i].client_sockd = -1;
        workers[i].cond = (pthread_cond_t) PTHREAD_COND_INITIALIZER;
        workers[i].id = i;
        int error = pthread_create(&workers[i].thread_fd, NULL, worker_thread,
                (void *) &workers[i]);
        if (error != 0) {
            write(STDERR_FILENO, "Error creating worker thread thread\n", 50);
            exit(EXIT_FAILURE);
        }
    }

    // take client requests from queue, parse request, and send off for processing to next available thread
    while (true) {
        // Wait for main to signal that socket has been queue'

        // check if pcleint has a fd to process
        // iterate through pool of thread to find waiting thread
        // unlock mutex and signal thread to work on processed client request
        /*
         * while we haven't passed of the client_socketd
         *   if worker with thread_fd of -1
         *      give client_sockd to worker
         *      exit while
         */

        int i = 0;
        int *psock_fd;
        while (true) {
            if (workers[i].client_sockd == -1) {
                pthread_mutex_lock(&mutex_queue);
                if ((psock_fd = dequeue()) == NULL) {// take next available client
                    pthread_cond_wait(&queue_cond, &mutex_queue);
                    psock_fd = dequeue();
                }
                // printf("sending worker %d cleintsocd = %d\n", i, *psock_fd);

                pthread_mutex_unlock(&mutex_queue);


                workers[i].client_sockd = *psock_fd;
                free(psock_fd);//wStruct correct place to free this?
                int error = pthread_cond_signal(&workers[i].cond);
                if (error != 0) {
                    write(STDERR_FILENO, "Error signaling thread\n", 50);
                    exit(EXIT_FAILURE);
                }

                break;
            }
            ++i; // Increment loop
            // reset i to 0 to iterate from beginning or worker array
            if (i == runtime->thread_count){
                i = 0;
                //for(ssize_t j = 0; j < thread_count; j++) {
                    //printf("worker %ld socket = %d\n", j, workers[j].client_sockd);
                //}
                //printf("reseting loop\n");
                //sleep(3);
            }
        }
    }

    return NULL;
}

int main(int argc, char** argv) {
    // check for correct number of program arguments
    if (argc == 1) {
        write(STDERR_FILENO, "No port number provided\n", 25);
        exit(EXIT_FAILURE);
    }
    // parse arguments for flags and port number
    extern char *optarg;
    extern int optind;
    int c;
    int Nflag = 0;
    lflag = 0;
    char *logname, *tcount;

    while ((c = getopt(argc, argv, "N:l:")) != -1) {
        switch (c) {
            case 'N':
                Nflag = 1;
                tcount = optarg;
                break;
            case 'l':
                lflag = 1;
                logname = optarg;
                break;
            case '?':
                exit(EXIT_FAILURE);
                break;
        }
    }

    if (Nflag == 1) {
        thread_count = atoi(tcount);
        if (thread_count < 1) {
            write(STDERR_FILENO, "N must be 1 or greater\n", 25);
            exit(EXIT_FAILURE);
        }
    } else {
        thread_count = 4;
    }

    if (lflag == 0) {
        logname = "";
    }
    else {
        //create log file if flagged
        log_fd = open(logname, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    }

    /*
      Create sockaddr_in with server information
    */
    char *port = argv[optind];
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(port));
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    socklen_t addrlen = sizeof(server_addr);

    /*
        Create server socket
    */
    int server_sockd = socket(AF_INET, SOCK_STREAM, 0);

    // Need to check if server_sockd < 0, meaning an error
    if (server_sockd < 0) {
        perror("socket");
    }

    /*
        Configure server socket
    */
    int enable = 1;

    /*
        This allows you to avoid: 'Bind: Address Already in Use' error
    */
    int ret = setsockopt(server_sockd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    /*
        Bind server address to socket that is open
    */
    ret = bind(server_sockd, (struct sockaddr *) &server_addr, addrlen);

    /*
        Listen for incoming connections
    */
    ret = listen(server_sockd, 5); // 5 should be enough, if not use SOMAXCONN

    if (ret < 0) {
        return EXIT_FAILURE;
    }

    /*
     * Create dispatch thread
     */

    thread_runtime_t runtime = {log_fd, thread_count};
    pthread_t dispatch_fd;
    int error = pthread_create(&dispatch_fd, NULL, dispatcher, (void*)&runtime);
    if (error < 0) {
        perror("Dispatcher thread");
    }

    /*
        Connecting with a client
    */
    struct sockaddr client_addr;
    socklen_t client_addrlen = sizeof(client_addr);

    while (true) {
        printf("[+] server is waiting...\n");

        /*
         * 1. Accept Connection
         */
        int client_sockd = accept(server_sockd, &client_addr, &client_addrlen);
        if(client_sockd < 0) {
            perror("accept failed");
        }

        int *pclient = malloc(sizeof(int));
        *pclient = client_sockd;

        pthread_mutex_lock(&mutex_queue);
        pthread_cond_signal(&queue_cond);
        enqueue(pclient);
        pthread_mutex_unlock(&mutex_queue);
    }

    return EXIT_SUCCESS;
}

#include "http.h"
#include "threads.h"
#include <pthread.h>

pthread_mutex_t mutex_file = PTHREAD_MUTEX_INITIALIZER;

//assign logging function for get, head, and all of error outcomes
//create new calculate_offset for errors

void printRequest(struct httpObject tvars) {
    printf("HTTP REQUEST: \n");
    printf("method == %s\n", tvars.method);
    printf("filename == %s\n", tvars.filename);
    printf("httpversion == %s\n", tvars.httpversion);
    printf("content_length == %ld\n", tvars.content_length);
}

void printResponse(struct httpObject tvars) {
    printf("HTTP RESPONSE: \n");
    printf("method == %s\n", tvars.method);
    printf("status_code == %d\n", tvars.status_code);
    printf("status_code_message == %s\n", tvars.status_code_message);
    printf("content_length == %ld\n", tvars.content_length);
}

void put_request(struct httpObject* tvars) {
    pthread_mutex_lock(&mutex_file);
    /* printf("Processing PUT request...\n\n"); */
    uint8_t buffer[BUFFER_SIZE];
    memset(buffer, '\0', BUFFER_SIZE);
    strcpy(tvars->method, "PUT");
    if(tvars->lflag) {
        tvars->poffset = calculate_offset(*tvars);
        tvars->chars_logged = 0;
    }
    printf("put filename = %s\n",tvars->filename);
    if(strcmp(tvars->filename, "healthcheck") == 0) {
        tvars->status_code = 403;
        pthread_mutex_unlock(&mutex_file);
        return;
    }

    int total_recv_length = 0;
    int fd = open(tvars->filename,  O_RDWR | O_CREAT | O_TRUNC, 0777);
    if(fd < 0) {
        //perror("put error");
        tvars->status_code = 500;
        pthread_mutex_unlock(&mutex_file);
        return;
    }
    do {
        int recv_length = recv(tvars->origin_socket, buffer, BUFFER_SIZE, 0);
        total_recv_length += recv_length;
        if(tvars->lflag) {
            logger(tvars, buffer);
        }
        write(fd, buffer, recv_length);
    } while (total_recv_length < tvars->content_length);

    close(fd);

    pthread_mutex_unlock(&mutex_file);

    tvars->status_code =  201;
}

void get_request(struct httpObject* tvars) {
    strcpy(tvars->method, "GET");
    pthread_mutex_lock(&mutex_file);
    if(strcmp(tvars->filename, "healthcheck") == 0 && tvars->lflag == 1) {
        tvars->status_code = 200;
        ssize_t digits = countdigitslog();
        tvars->content_length = digits + 1;
        pthread_mutex_unlock(&mutex_file);
        return;
    }
    if(strcmp(tvars->filename, "healthcheck") == 0 && tvars->lflag == 0) {
        printf("thats a bad health\n");
        tvars->status_code = 404;
        return;
    }
    if(access(tvars->filename, F_OK)) {
        tvars->status_code = 404;
        pthread_mutex_unlock(&mutex_file);
        return;
    }
    //pthread_mutex_lock(&mutex_file);
    int fd = open(tvars->filename, O_RDONLY);
    if (fd < 0) {
        tvars->status_code = 403;
        pthread_mutex_unlock(&mutex_file);
        return;
    }

    struct stat statbuf;
    fstat(fd, &statbuf);
    tvars->content_length = statbuf.st_size;

    close(fd);
    pthread_mutex_unlock(&mutex_file);

    tvars->status_code = 200;
}

void head_request(struct httpObject* tvars) {
    strcpy(tvars->method, "HEAD");
    if(strcmp(tvars->filename, "healthcheck") == 0) {
        tvars->status_code = 403;
        return;
    }
    pthread_mutex_lock(&mutex_file);

    struct stat statbuf;
    int stat_info = stat(tvars->filename, &statbuf);
    if(stat_info < 0) {
        tvars->status_code = 404;
        pthread_mutex_unlock(&mutex_file);
        return;
    }
    tvars->content_length = statbuf.st_size;
    pthread_mutex_unlock(&mutex_file);

    tvars->status_code = 200;
}

int check_filename(const char* filename) {
    // iterate over each char in filename
    // convert to ascii value
    // compare valid ascii values
    // return 0 if char not in whitelist
    // return 0 if filename longer than 27 char

    if(strlen(filename) > 27) {
        return 0;
    }

    for(int i = 0; filename[i] != '\0'; i++) {
        if((64 < filename[i] && filename[i]  <= 90) || (96 < filename[i] && filename[i] <= 122)
            || (47 < filename[i] && filename[i] <= 58) || filename[i] == 45 || filename[i] == 95) {

        }
        else {
            return 0;
        }
    }

    return 1;
}

int check_method(const char* method){
    return strcmp(method, "PUT") == 0
        || strcmp(method, "GET") == 0
        || strcmp(method, "HEAD") == 0;
}

int check_http_version(const char* version) {
    if(strcmp(version, "1.1") == 0) {
        return 1;
    }
    return 0;
}

int validate(struct httpObject tvars) {
    return check_filename(tvars.filename)
        && check_method(tvars.method)
        && check_http_version(tvars.httpversion);
}


struct httpObject read_http_request(int client_sockd, int lflag) {
    struct httpObject tvars;
    tvars.lflag = lflag;
    uint8_t buffer[BUFFER_SIZE + 1];
    memset(buffer, '\0', BUFFER_SIZE + 1);
    // Receive client info
    int res = recv(client_sockd, buffer, BUFFER_SIZE, MSG_PEEK);
    ssize_t hsize;
    char * dcrlf = strstr((const char *) buffer, "\r\n\r\n");
    if(dcrlf) {
        hsize = (ssize_t) dcrlf - (ssize_t) buffer + 4;
    }

    memset(buffer, '\0', BUFFER_SIZE + 1);
    res = recv(client_sockd, buffer, hsize, 0);
    if(res < 0) {
        strcpy(tvars.filename, ".}");
        return tvars;
    }
    // parse response into httpRequest object
    sscanf((const char *)buffer, "%s%*[ /]%s%*[ HTTP/]%s", tvars.method, tvars.filename,
            tvars.httpversion);
    char con_len[10];
    sscanf((const char *) buffer, "%*s%*s%*s%*s%*s%*s%*s%*s%*s%*s%s",  con_len);
    int con_int = atoi(con_len);
    tvars.content_length = (ssize_t) con_int;
    tvars.origin_socket = client_sockd;

    return tvars;
}

struct httpObject process_request(struct httpObject tvars) {
    if (!validate(tvars)) {
        tvars.status_code = 400;
        return tvars;
    }

    // Switch for methods
    if((strcmp(tvars.method, "PUT")) == 0) {
        printf("doing put req\n");
        put_request(&tvars);
    }
    else if((strcmp(tvars.method, "GET") == 0)) {
        printf("doing get req\n");
        get_request(&tvars);
    }
    else if((strcmp(tvars.method, "HEAD") == 0)) {
        printf("doing head req\n");
        head_request(&tvars);
    }

    return tvars;
}

void calculate_status_code_message(struct httpObject* tvars) {
    switch(tvars->status_code) {
        case 200:
            strcpy(tvars->status_code_message, "OK");
            break;
        case 201:
            strcpy(tvars->status_code_message, "Created");
            tvars->content_length = 0;
            break;
        case 400:
            strcpy(tvars->status_code_message, "Bad Request");
            tvars->content_length = 0;
            if(tvars->lflag == 1){
                errorlogger(*tvars);
            }
            break;
        case 403:
            strcpy(tvars->status_code_message, "Forbidden");
            tvars->content_length = 0;
            if(tvars->lflag == 1){
                errorlogger(*tvars);
            }
            break;
        case 404:
            strcpy(tvars->status_code_message, "Not Found");
            tvars->content_length = 0;
            if(tvars->lflag == 1){
                errorlogger(*tvars);
            }
            break;
        default:
            strcpy(tvars->status_code_message, "Internal Server Error");
            tvars->content_length = 0;
            if(tvars->lflag == 1){
                errorlogger(*tvars);
            }
            break;
    }
}

void send_response(struct httpObject tvars) {
    printf("status = %d\n", tvars.status_code);
    // if get request send contents of file to client
    uint8_t buffer[BUFFER_SIZE];
    memset(buffer, '\0', BUFFER_SIZE);
    calculate_status_code_message(&tvars);
    snprintf((char *)buffer, BUFFER_SIZE, "HTTP/1.1 %d %s\r\nContent-Length: %ld\r\n\r\n",
            tvars.status_code, tvars.status_code_message,
            tvars.content_length);

    write(tvars.origin_socket, (const char *) buffer, strlen((const char *)buffer));
    // If its a valid get request
    if(strcmp(tvars.filename, "healthcheck") == 0 && tvars.status_code == 200 && tvars.lflag == 1) {
        memset(buffer, '\0', BUFFER_SIZE);
        int writ_length = tvars.content_length;
        ssize_t log_error = return_log_error();
        ssize_t log_total = return_log_total();
        snprintf(( char *)buffer, BUFFER_SIZE, "%ld\n%ld", log_error, log_total);
        write(tvars.origin_socket, buffer, writ_length);

    }
    else if((strcmp(tvars.method, "GET") == 0) && tvars.status_code == 200) {
        if(tvars.lflag) {
            tvars.poffset = calculate_offset(tvars);
            tvars.chars_logged = 0;
        }

        memset(buffer, '\0', BUFFER_SIZE);
        int total_writ_length = tvars.content_length;
        //pthread_mutex_lock(&mutex_file);
        pthread_mutex_lock(&mutex_file);

        int fd = open(tvars.filename, O_RDONLY);
        do {
            int writ_length = read(fd, buffer, BUFFER_SIZE);
            total_writ_length -= writ_length;
            if(tvars.lflag) {
                logger(&tvars, buffer);
            }
            write(tvars.origin_socket, buffer, writ_length);
        } while (total_writ_length > 0);

        close(fd);
        pthread_mutex_unlock(&mutex_file);
        //pthread_mutex_unlock(&mutex_file);
        }
}

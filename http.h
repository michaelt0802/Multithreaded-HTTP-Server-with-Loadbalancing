#ifndef OOGA_HTTP_H_
#define OOGA_HTTP_H_

#include <sys/socket.h>
#include <sys/stat.h>
#include <stdio.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <fcntl.h>
#include <unistd.h> // write
#include <string.h> // memset
#include <stdlib.h> // atoi
#include <stdbool.h> // true, false

#define BUFFER_SIZE 4096

struct httpObject {
    /* Create some object 'struct' to keep track of all
        the components related to a HTTP message
        NOTE: There may be more member variables you would want to add
    */
    char method[5];         // PUT, HEAD, GET
    char filename[28];      // what is the file we are worried about
    char httpversion[9];    // HTTP/1.1
    ssize_t content_length; // example: 13
    int origin_socket;      // originating socket file descriptor
    int status_code;               // 200, 404, etc.
    char status_code_message[100]; // OK, File not found, etc
    ssize_t poffset;        // offset for pwrite
    int chars_logged;       // running total of chars written in log
    int lflag;              // flag for if logging happening
};
/*
struct httpResponse {
    char method[5];                // PUT, HEAD, GET
    int status_code;               // 200, 404, etc.
    char status_code_message[100]; // OK, File not found, etc
    ssize_t content_length;        // example: 13
    int origin_socket;             // originating socket file descriptor
    char filename[28];             // what is the file we are worried about
    ssize_t poffset;               // offset for pwrite
    int lflag;                     // flag for if logging happening
    int chars_logged;       // running total of chars written in log
};
*/
/*
struct workerInfo {
    int log_fd;
    int lflag;
    ssize_t offset;
}
*/

/*
   Takes socket and returns parsed request in a data structure
   \param client_sockd - socket file descriptor
   \param lflag - flag for if logging happening
   \return struct httpRequest parsed data
*/
struct httpObject read_http_request(int client_sockd, int lflag);

/*
   Validates HTTP request & creates a response
   \param request struct httpRequest holding parsed information from client
                                     socket
   \return struct httpResponse holding response data
*/
struct httpObject process_request(struct httpObject tvars);

/*
   Sends proper HTTP response to client_sockd
   \param response holds response info
   \param client_sockd holds socket file descriptor of client with request
*/
void send_response(struct httpObject tvars);

/*
   Helper function: Prints out values of httpRequest struct
   \param req HTTP request object
*/
void printRequest(struct httpObject tvars);

/*
   Helper function: Prints out values of httpResponse struct
   \param req HTTP response object
*/
void printResponse(struct httpObject tvars);

/*
   Validates filename, HTTP version, and method
   \param req HTTP request object
   \return bool true if valid request
*/
int validate(struct httpObject tvars);

/*
   returns true if HTTP requested filename is a valid request
   \param filename requested file
*/
int check_filename(const char* filename);

/*
   returns true if version == 1.1
   \param version HTTP version
*/
int check_http_version(const char* version);

/*
   returns true if method is PUT,HEAD, OR GET
   \param method string representing HTTP method
*/
int check_method(const char* method);

void put_request(struct httpObject* tvars);

/*
 * \param req the request made by the client
 * \param res the response object to store values
 */
void get_request(struct httpObject* tvars);

/*
 * \param req the request made by the client
 * \param res the response object to store values
 */
void head_request(struct httpObject* tvars);

/*
 * Inserts proper message based off status code into given httpReponse obj
 \param res the HTTP resopnse
 */
void calculate_status_code_message(struct httpObject* tvars);

#endif

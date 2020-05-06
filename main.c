#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <getopt.h>

#include <sys/time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>

struct send_data {
    char *data;
    unsigned int length;
};

#define ONE_MEGABYTE (1024*1024LL)
#define DEFAULT_SEND_SIZE (100 * ONE_MEGABYTE)

#define MAX_CNGST_ALGO 64
// Congestion algorithm storage.
struct tcp_congest_algos {
    char *algos[MAX_CNGST_ALGO];
    int n;
};

/* Structure passed to the dispatch thread.
 * - id: local, application specific ID.
 * - tid: thread ID.
 * - fd: file descriptor of the client socket.
 * - *d: pointer to the data to be send to the client.
 * - *cngst_algorithm - the name of the congestion control algorithm to be used.
 */
struct thread_args {
    int id;
    pthread_t tid;
    int fd;
    struct send_data *d;
    char *cngst_algorithm;
};


int listen_socket();
void connection_dispatch(int, struct send_data *);
void *sending_thread(void *);

struct send_data *create_data(const unsigned long long int);

struct tcp_congest_algos *congestion_algorithms(void);

int timeval_subtract(struct timeval *, struct timeval *, struct timeval *);

// Global dispatch loop variable and signal handler.
int dispatch_loop = 1;

void sigint_handler(int sig) {
    dispatch_loop = 0;
}


int main(int argc, char **argv) {
    int listen_fd;
    int opt;
    unsigned long int send_data_size = DEFAULT_SEND_SIZE;
    struct send_data *send_data;

    srand(time(NULL));

    sigaction(SIGINT, &(struct sigaction){{sigint_handler}}, NULL);

    //Parse the command line arguments
    while (1) {

        struct option long_options[] = {
            { "size", required_argument, NULL, 's'},
            { 0, 0, 0, 0 }
        };

        if ((opt = getopt_long(argc, argv, "s:", long_options, NULL)) == -1) {
            break;
        }

        switch (opt) {
        case 's':
            send_data_size = (atoi(optarg) * ONE_MEGABYTE);
            if (send_data_size == 0) {
                send_data_size = DEFAULT_SEND_SIZE;
            }
        };
    }

    // Set up the listening socket
    printf("- Send size set to %llu MB\n", (send_data_size) / ONE_MEGABYTE);
    listen_fd = listen_socket();
    printf("- Listening on port %d\n", 9000);

    //Create the data to be sent to the client.
    printf("- Allocating random data... ");
    fflush(stdout);
    send_data = create_data(send_data_size);
    printf("done\n");

    //Accept connetions and dispatch to threads.
    connection_dispatch(listen_fd, send_data);

    free(send_data->data);
    free(send_data);

    return 0;
}


/*
 * listen_socket() - sets up the TCP listening socket on TCP port 9000
 *
 * TODO: make the port an argument.
 *
 * return - the TCP listener file descriptor
 */
#define TCP_LISTEN_PORT "9000"
int listen_socket() {
    int listening_fd, ret;
    struct addrinfo hints, *res;

    memset(&hints, 0, sizeof(hints));

    hints = (struct addrinfo){
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_flags = AI_PASSIVE
    };

    getaddrinfo(NULL, TCP_LISTEN_PORT, &hints, &res);

    // Create the listening socket
    listening_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if(listening_fd == 0) {
        perror("Could not create listening socket");
        freeaddrinfo(res); 
        exit(1);
    }

    ret = bind(listening_fd, res->ai_addr, res->ai_addrlen);
    if (ret < 0) {
        perror("Could not bind() socket");
        freeaddrinfo(res);
        exit(1);
    }

    listen(listening_fd, 10);
    freeaddrinfo(res);
    return listening_fd;
}


/* 
 * connection_dispatch() - accepts new incoming client connections to
 * the listener and dispatches them to a new thread.
 *
 * listening_fd -   the file descriptor created by listen_socket()
 * *sd -    a structure containing data and the length of the data that
 *          will be sent by the thread
 */
void connection_dispatch(int listening_fd, struct send_data *sd) {
    int connection_fd;
    struct sockaddr_storage their_addr;
    socklen_t addr_size = sizeof(their_addr);
    struct thread_args *args;
    int i = 0;
    struct tcp_congest_algos *c_algos;

    //Get our congestion algorithms, malloc performed in congestion_algorithms()
    c_algos = congestion_algorithms();

    //Loop defined by global var which is set by signal handler.
    while (dispatch_loop) {
        connection_fd = accept(listening_fd, (struct sockaddr *) &their_addr, &addr_size);

        // Did we get interrupted?
        if (errno == EINTR) {
            goto cleanup;
        }

        //Allocate and set the thread arguments
        args = malloc(sizeof(*args));
        args->id = i;
        args->fd = connection_fd;
        args->d = sd;
        args->cngst_algorithm = c_algos->algos[rand() % c_algos->n];

        // The thread detaches and then cleans up its own resources at the end.
        pthread_create(&args->tid, NULL, sending_thread, args);
        i++;
    }

cleanup:
    printf("SIGINT received - cleaning up\n");
    /* As we use strtok_r() to tokenise the algorithms from the getline() function, the first entry 
     * one is a pointer  to the original piece of memory allocated by getline() */
    free(c_algos->algos[0]);
    free(c_algos);
    close(listening_fd);

    return;
}


/* 
 * create_data() - allocates a block of memory and fills it with random
 * data in the 'a-z' ASCII range
 *
 * length - the length of the block of data to create in bytes
 *
 * returns - pointer to a struct send_data which contains a pointer to
 *  the data and the length of the data
 */    
struct send_data *create_data(const unsigned long long int length) {
    char *data;
    struct send_data *sd;

    data = malloc(length * sizeof(*data));
    sd = malloc(sizeof(*sd));

    sd->length = length;

    for (int i = 0; i < length; i++) { data[i] = (rand() % 26) + 97; }

    sd->data = data;

    return sd;
}


/*
 * sending_thread() - function called by thread dispatch for every incoming client connection.
 *
 * a - a pointer to a struct thread_args which contins
 *      - the application ID of the thread
 *      - the pthread_t ID of the thread
 *      - the file descriptor of the socket
 *      - the struct send_data that will be sent to the client
 *      - the name (char *) of the congestion algorithm that will be used.
 *
 * returns: nothing.
 */
void *sending_thread(void *a) {
    struct thread_args *t_arg = a;
    socklen_t cngst_algo_len;
    int ret;
    struct timeval start_time, end_time, elapsed_time;

    // We don't need to join back up with the thread as we don't need
    // anything returned from it. So we detach and let it clean itself
    // up
    pthread_detach(pthread_self());

    if (gettimeofday(&start_time, NULL) != 0) {
        perror("Could not gettimeofday()");
    }


    //Set the congestion algorithm
    cngst_algo_len = strlen(t_arg->cngst_algorithm) + 1;
    ret = setsockopt(t_arg->fd, IPPROTO_TCP, TCP_CONGESTION, t_arg->cngst_algorithm, cngst_algo_len);
    if (ret < 0) {
        perror("setsockopt(): could not set congestion algorithm");
    }

    
    // Send the congestion algorithm, including the null byte
    // then send the data
    send(t_arg->fd, t_arg->cngst_algorithm, cngst_algo_len, 0);
    send(t_arg->fd, t_arg->d->data, t_arg->d->length, 0);

    if (gettimeofday(&end_time, NULL) != 0) {
        perror("Could not gettimeofday()");
    }

    timeval_subtract(&elapsed_time, &end_time, &start_time);        

    printf("- [ID: %d,Algorithm: %s,Send time: %ld.%06ld]\n", t_arg->id, t_arg->cngst_algorithm, elapsed_time.tv_sec, elapsed_time.tv_usec);

    //The thread cleans up its own resources
    // We don't free the actual data as its shared by all
    // of the threads. We only free the send_data structure
    close(t_arg->fd);
    free(t_arg);

    return NULL;
}



/*
 * tcp_congest_algos() - returns a list of available TCP congestion
 * algorithms
 *
 * returns: struct tcp_congest_algos, which has an array of char * for each of
 * the algorithms, and the number of algorithms 'n'.
 */
struct tcp_congest_algos *congestion_algorithms(void) {
    FILE *available_cngst; 
    char *algorithms = NULL, *algorithm;
    char *strtok_save = NULL;
    size_t line_length = 0;
    struct tcp_congest_algos *return_algos;
    int i = 0, x;

    available_cngst = fopen("/proc/sys/net/ipv4/tcp_available_congestion_control", "r");

    getline(&algorithms, &line_length, available_cngst);
    //Strip out newline
    for (x = 0; x < line_length; x++) {
        if (algorithms[x] == '\n') {
            algorithms[x] = '\0';
        }
    }

    strtok_save = algorithms;

    return_algos = malloc(sizeof(*return_algos));

    while( (algorithm = strtok_r(strtok_save, " ", &strtok_save)) ) {
        if (i == MAX_CNGST_ALGO) {
            printf("Maximum congestion algorithms reached: %d\n", MAX_CNGST_ALGO);
            break;
        }

        printf("- Congestion Algorithm: %s\n", algorithm);
        return_algos->algos[i] = algorithm;
        i++;
    } 

    return_algos->n = i;

    fclose(available_cngst);

    return return_algos;
}



/* Subtract the ‘struct timeval’ values X and Y,
   storing the result in RESULT.
   Return 1 if the difference is negative, otherwise 0. */

/*
 * timeval_subtract() - takes to struct timeval pointers and returns the differnce in
 * the result parameter.
 *
 * returns: 1 if the result is negative
 */
int timeval_subtract(struct timeval *result, struct timeval *x, struct timeval *y)
{
    /* Perform the carry for the later subtraction by updating y. */
    if (x->tv_usec < y->tv_usec) {
      int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
      y->tv_usec -= 1000000 * nsec;
      y->tv_sec += nsec;
    }
    if (x->tv_usec - y->tv_usec > 1000000) {
      int nsec = (x->tv_usec - y->tv_usec) / 1000000;
      y->tv_usec += 1000000 * nsec;
      y->tv_sec -= nsec;
    }

    /* Compute the time remaining to wait.
       tv_usec is certainly positive. */
    result->tv_sec = x->tv_sec - y->tv_sec;
    result->tv_usec = x->tv_usec - y->tv_usec;

    /* Return 1 if result is negative. */
    return x->tv_sec < y->tv_sec;
}

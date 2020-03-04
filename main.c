//#define  _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>

#define DATA_LENGTH 1024 * 1024 * 1000

struct send_data {
    char *data;
    unsigned int length;
};

#define MAX_CNGST_ALGO 64

struct tcp_congest_algos {
    char *algos[MAX_CNGST_ALGO];
    int n;
};

struct thread_args {
    int id;
    pthread_t tid;
    int fd;
    struct send_data *d;
    char *cngst_algorithm;
};


int listen_socket(int);
void connection_dispatch(int, struct send_data *);
void *sending_thread(void *);

struct send_data *create_data(int);

struct tcp_congest_algos *congestion_algorithms(void);



int dispatch_loop = 1;

void sigint_handler(int sig) {
    dispatch_loop = 0;
}


int main(int argc, char **argv) {
    int listen_fd;
    struct send_data *send_data;

    srand(time(NULL));

    sigaction(SIGINT, &(struct sigaction){sigint_handler}, NULL);

    listen_fd = listen_socket(9000);

    send_data = create_data(DATA_LENGTH);

    connection_dispatch(listen_fd, send_data);

    free(send_data->data);
    free(send_data);

    return 0;
}


int listen_socket(int port) {
    int listening_fd, ret;
    struct addrinfo hints, *res;

    memset(&hints, 0, sizeof(hints));

    hints = (struct addrinfo){
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_flags = AI_PASSIVE
    };

    getaddrinfo(NULL, "9000", &hints, &res);

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


void connection_dispatch(int listening_fd, struct send_data *sd) {
    int connection_fd;
    struct sockaddr_storage their_addr;
    socklen_t addr_size = sizeof(their_addr);
    struct thread_args *args;
    int i = 0;
    struct tcp_congest_algos *c_algos;

    //Get our congestion algorithms, malloc performed in congestion_algorithms()
    c_algos = congestion_algorithms();

    while (dispatch_loop) {
        connection_fd = accept(listening_fd, (struct sockaddr *) &their_addr, &addr_size);

        if (errno == EINTR) {
            goto cleanup;
        }

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


struct send_data *create_data(int length) {
    char *data;
    struct send_data *sd;

    data = malloc(length * sizeof(*data));
    sd = malloc(sizeof(*sd));

    sd->length = length;

    for (int i = 0; i < length; i++) { data[i] = (rand() % 26) + 97; }

    sd->data = data;

    return sd;
}


void *sending_thread(void *a) {
    struct thread_args *t_arg = a;
    socklen_t cngst_algo_len;
    int ret;
    
    pthread_detach(pthread_self());

    printf("Sending on ID %d, algorithm: %s\n", t_arg->id, t_arg->cngst_algorithm);

    //Set the congestion algorithm
    cngst_algo_len = strlen(t_arg->cngst_algorithm) + 1;
    ret = setsockopt(t_arg->fd, IPPROTO_TCP, TCP_CONGESTION, t_arg->cngst_algorithm, cngst_algo_len);
    if (ret < 0) {
        perror("setsockopt(): could not set congestion algorithm");
    }


    //Send the congestion algorithm, including the null byte
    send(t_arg->fd, t_arg->cngst_algorithm, cngst_algo_len, 0);

    //Send the data
    send(t_arg->fd, t_arg->d->data, t_arg->d->length, 0);

    //The thread cleans up its own resources
    // We don't free the actual data as its shared by all
    // of the threads. We only free the send_data structure
    close(t_arg->fd);
    free(t_arg);
}




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

    while( algorithm = strtok_r(strtok_save, " ", &strtok_save) ) {
        if (i == MAX_CNGST_ALGO) {
            printf("Maximum congestion algorithms reached: %d\n", MAX_CNGST_ALGO);
            break;
        }

        printf("Algorithm: %s (%p)\n", algorithm, algorithm);
        return_algos->algos[i] = algorithm;
        i++;
    } 

    return_algos->n = i;

    fclose(available_cngst);

    return return_algos;
}


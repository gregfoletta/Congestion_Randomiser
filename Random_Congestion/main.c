#define _POSIX_C_SOURCE 200112L


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
#include <netdb.h>

struct send_data {
    char *data;
    unsigned int length;
};

struct thread_args {
    int id;
    pthread_t tid;
    int fd;
    struct send_data *d;
};


void *sending_thread(void *);

int dispatch_loop = 1;

void sigint_handler(int sig) {
    dispatch_loop = 0;
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
        exit(1);
    }

    ret = bind(listening_fd, res->ai_addr, res->ai_addrlen);
    if (ret < 0) {
        perror("Could not bind() socket");
        exit(1);
    }

    listen(listening_fd, 10);

    freeaddrinfo(res);

    return listening_fd;
}

#define MAX_THREADS 4096

void connection_dispatch(int listening_fd, struct send_data *sd) {
    int connection_fd;
    struct sockaddr_storage their_addr;
    socklen_t addr_size = sizeof(their_addr);
    struct thread_args *args;
    int i = 0;


    while (dispatch_loop) {
        connection_fd = accept(listening_fd, (struct sockaddr *) &their_addr, &addr_size);

        if (errno == EINTR) {
            goto cleanup;
        }

        args = malloc(sizeof(*args));
        args->id = i;
        args->fd = connection_fd;
        args->d = sd;

        // The thread detaches and then cleans up its own resources at the end.
        // Have to check the interaction with SIGINT
        pthread_create(&args->tid, NULL, sending_thread, args);
        i++;
    }

cleanup:
    printf("SIGINT received - cleaning up\n");

    close(listening_fd);

    return;
}


struct send_data *create_data(int length) {
    char *data;
    struct send_data *sd;

    data = malloc(length * sizeof(*data));
    sd = malloc(sizeof(*sd));

    sd->length = length;

    for (int i = 0; i < length; i++) { data[i] = 'a'; }

    sd->data = data;

    return sd;
}


void *sending_thread(void *a) {
    struct thread_args *t_arg = a;
    
    pthread_detach(pthread_self());

    printf("Sending on ID %d\n", t_arg->id);

    send(t_arg->fd, t_arg->d->data, t_arg->d->length, 0);

    //The thread cleans up its own resources
    // We don't free the actual data as its shared by all
    // of the threads. We only free the send_data structure
    close(t_arg->fd);
    free(t_arg);
}



#define DATA_LENGTH 1024 * 1024 * 30

int main(int argc, char **argv) {
    int listen_fd;
    struct send_data *send_data;

    signal(SIGINT, sigint_handler);

    listen_fd = listen_socket(9000);

    send_data = create_data(DATA_LENGTH);

    connection_dispatch(listen_fd, send_data);

    free(send_data->data);
    free(send_data);

    return 0;
}

# TCP Congestion Randomiser

## What is it?

The multi-threaded server TCP server that uses randomly assigned congestion algorithms.

## How do I build it?

```
# git clone https://github.com/gregfoletta/Congestion_Randomiser.git
# gcc -Wall -pthread main.c -o cngst_rand
```

## How does it work?

The process does initiall performs the following: 

- Allocates a chunk of data in Mb based on the `--size` command line argument.
    - Defaults to 100Mb.
- Fills the data with bytes in the range of 0x61 - 0x7a, which maps to ASCII 'a-z'
- Determines the TCP congestion algorithms available.
- Sets up a listening socket on TCP port 9000.

Then, for each incoming client connection:

- Spawns a new thread.
- Randomly sets the client connection's TCP congestion algorithm to one of the available methods.
- Sends a null terminated string representing the congestion algorithm used.
- Sends the chunk of data that was allocated at the start.
- Prints details about the data sent, algorithm used and time taken to STDOUT.
- Resources are cleaned up and the thread ends.

The process can be killed with a SIGINT. This terminates the dispatch loop and cleans up all the resources.

## Any gotchas?

This build has been targeted to run on Linux. Won't work on any other operating system.

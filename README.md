# TCP Congestion Randomiser

## What is it?

The multi-threaded server TCP server sends data using randomly assigned congestion algorithms.

## How do I build it?

```
# git clone https://github.com/gregfoletta/Congestion_Randomiser.git
# gcc -Wall -pthread main.c -o cngst_rand
```

## How do I call it?

Defaults to 100Mb of data to send:
```
# ./cngst_ran
```
or set your own size (in Mb)
```
# ./cngst_rand --size 500
```

I generally use `parallel` and `netcat` to initiate connections. For example the following command initiates n parallel connections (where n is the number of CPU cores you have) up to a total of 60.
```
# parallel -n0 nc -d localhost 9000 ::: {1..60} > /dev/null
```

## Can you show me some output?

Sure:

```
# ./cngst_rand 
- Send size set to 100 MB
- Listening on port 9000
- Allocating random data... done
- Congestion Algorithm: reno
- Congestion Algorithm: cubic
- [ID: 2, Algorithm: reno, Send time: 0.232096]
- [ID: 0, Algorithm: cubic, Send time: 0.276508]
- [ID: 3, Algorithm: cubic, Send time: 0.281984]
- [ID: 1, Algorithm: cubic, Send time: 0.311223]
- [ID: 4, Algorithm: reno, Send time: 0.302720]
^CSIGINT received - cleaning up
```

## How does it work?

THe congestion randomiser works in the following way: 

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

- This build has been targeted to run on Linux. Won't work on any other operating system.
- Needs further testing on other devices; if you see problems please raise an issue.

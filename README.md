
# memento

Fairly simple hashmap implementation built on top of an epoll TCP server. A toy
project just for learning purpose, fascinated by the topic, i decided to try to
make a simpler version of redis/memcached to better understand how it works.
The project is a minimal Redis-like implementation with a text based protocol,
and like Redis, can be used as key-value in-memory store.


Currently supports some basic operations, all commands are case insensitive, so
not strictly uppercase.

| Command         | Args                       | Description                                                                                                   |
|---------------- | -------------------------- | ------------------------------------------------------------------------------------------------------------- |
| **SET**         | `<key>` `<value>`          | Sets `<key>` to `<value>`                                                                                     |
| **GET**         | `<key>`                    | Get the value identified by `<key>`                                                                           |
| **DEL**         | `<key>` `<key2> .. <keyN>` | Delete values identified by `<key>`..`<keyN>`                                                                 |
| **INC**         | `<key>` `<qty>`            | Increment by `<qty>` the value idenfied by `<key>`, if no `<qty>` is specified increment by 1                 |
| **DEC**         | `<key>` `<qty>`            | Decrement by `<qty>` the value idenfied by `<key>`, if no `<qty>` is specified decrement by 1                 |
| **INCF**        | `<key>` `<qty>`            | Increment by float `<qty>` the value identified by `<key>`, if no `<qty>` is specified increment by 1.0       |
| **DECF**        | `<key>` `<qty>`            | Decrement by `<qty>` the value identified by `<key>`, if no `<qty>` is specified decrement by 1.0             |
| **GETP**        | `<key>`                    | Get all information of a key-value pair represented by `<key>`, like key, value, creation time and expire time|
| **APPEND**      | `<key>` `<value>`          | Append `<value>` to `<key>`                                                                                   |
| **PREPEND**     | `<key>` `<value>`          | Prepend `<value>` to `<key>`                                                                                  |
| **FLUSH**       |                            | Delete all maps stored inside partitions                                                                      |
| **QUIT/EXIT**   |                            | Close connection                                                                                              |


### Distribution

Still in development and certainly bugged, it currently supports some commands
in a distributed context across a cluster of machines. There isn't a replica
system yet, planning to add it soon, currently the database can be started in
cluster mode with the `-c` flag and by creating a configuration file on each
node. As default behaviour, memento search for a `~/.memento` file in `$HOME`,
by the way it is also possible to define a different path.

The configuration file shall strictly follow some rules, a sample could be:

```
    # configuration file
    127.0.0.1   8181    node1   0
    127.0.0.1   8182    node2   1
    127.0.0.1   8183    node3   0
```

every line define a node, with an IP address, a port, his id-name and a flag to
define if the `IP-PORT-ID` refers to self node (ie the machine where the file
reside). `PORT` value refer to the actual listen port of the instance plus 100.
So 8082 means a bus port 8182.

It is possible to generate basic configurations using the helper script
`build_cluster.py`, it accepts just the address and port of every node, so to
create 3 configuration file as the previous example just run (all on the same
machine, but can be easily used to generate configurations for different nodes
in a LAN):

    $ python build_cluster.py 127.0.0.1:8081 127.0.0.1:8082 127.0.0.1:8083

This instruction will generate files `node0.conf`, `node1.conf` and
`node2.conf` containing each the right configuration. It can be renamed to
`.memento` and dropped in the `$HOME` path of every node, or can be passed to
the program as argument using `-f` option.
Every node of the cluster, will try to connect to other, if all goes well,
every node should print a log message like this:

    $ [INF][YYYY-mm-dd hh-mm-ss] - Cluster successfully formed

From now on the system is ready to receive connections and commands.

The distribution of keys and values follows a classic hashing addressing in a
keyspace divided in equal buckets through the cluster. The total number of
slots is 8192, so if the cluster is formed of 2 nodes, every node will get
at most 4096 keys.

### Build

To build the source just run `make`. A `memento` executable will be generated into
a `release` directory that can be started to listen on a defined `hostname`,
ready to receive commands from any TCP client

    $ ./release/memento -a <hostname> -p <port>

`-c` for a cluster mode start

    $ ./release/memento -a <hostname> -p <port> -c

Parameter `<hostname>` and `<port>` fallback to 127.0.0.1 and 6737, memento accept also
a -f parameter, it allows to specify a configuration file for cluster context.

    $ ./release/memento -a <hostname> -p <port> -c -f ./conf/node0.conf

The name of the node can be overridden with the `-i` option

    $ ./release/memento -a <hostname> -p <port> -c -f <path-to-conf> -i <name-id>

It is also possible to stress-test the application by using `memento-benchmark`, previously
generating it with `make memento-benchmark` command

    $ ./release/memento-benchmark <hostname> <port>

To build memento-cli just `make memento-cli` and run it like the following:

    $ ./release/memento-cli <hostname> <port>

### Experimental

The system is already blazing fast, testing it in a simple manner give already
good results. Using a Linux box with an Intel(R) Core(TM) i5-4210U CPU @
1.70Ghz and 4 GB ram I get

```
 Nr. operations 100000

 1) SET keyone valueone
 2) GET keyone
 3) SET with growing keys
 4) All previous operations in 10 threads, each one performing 2000 requests per type

 [SET] - Elapsed time: 0.871047 s  Op/s: 114804.37
 [GET] - Elapsed time: 0.889411 s  Op/s: 112433.95
 [SET] - Elapsed time: 0.923016 s  Op/s: 108340.48

```

#### Performance improvements

Still experimenting different paths, the main bottleneck is the server
interface where clients are served; initially I rewrote the network interface
as a simple epoll main thread that handle EPOLLIN and EPOLLOUT events, using a
thread-safe queue to dispatch incoming data on descriptor to a pool of reader
threads. After being processed, their responsibility was to set the descriptor
to EPOLLOUT, in this case the main thread enqueued the descriptor to a write
queue, and a pool of writer threads constantly polling it, had the
responsibility to send out data.

Later I decided to adopt a model more nginx oriented, using a pool of threads
to spin their own event loop, and using the main thread only for accepting
incoming connection, demanding them to worker reader threads, this time,
allowing the main thread to also handle internal communication between other
nodes, semplifying a bit the process. Still a pool of writer threads is used to
send out data to clients, this approach seems better, but it is still in
development and testing.

### Changelog

Seethe [CHANGELOG](CHANGELOG) file.

### License

See the [LICENSE](LICENSE) file for license rights and limitations (MIT).

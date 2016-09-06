
# SHIBUI

Fairly basic hashmap/message queue implementation built on top of an async TCP
server to handle multiple clients connections featuring epoll, posix thread and
consistent hashing data distribution. A toy project just for learning purpose,
fascinated by the topic, i decided to try to make a simpler version of
redis/memcached to better understand how it works.

The project is a minimal redis-like implementation with a text based protocol,
and like redis, can be used as key-value in-memory store, or a basic message
broker using keys as topic to subscribe to.

![sample](https://github.com/codepr/shibui/tree/master/samples/sample.png)

### Commands

- SET key value               Sets `<key>` to `<value>`
- GET key                     Get the value identified by `<key>`
- DEL key key2 .. keyN        Delete values identified by `<key>..<keyN>`
- INC key qty                 Increment by <qty> the value idenfied by `<key>`, if no `<qty>` is specified increment by 1
- DEC key qty                 Decrement by `<qty>` the value idenfied by `<key>`, if no `<qty>` is specified decrement by 1
- SUB key key2 .. keyN        Subscribe to `<key>..<keyN>`, receiving messages published
- UNSUB key key2 .. keyN      Unsubscribe from `<key>..<keyN>`
- PUB key value               Publish message `<value>` to `<key>` (analog to SET but broadcasting to all subscribed members of `<key>`)
- GETP key                    Get all information of a key-value pair represented by `<key>`, like key, value, creation time and expire time
- APPEND key value            Append `<value>` to `<key>`
- PREPEND key value           Prepend `<value>` to `<key>`
- EXPIRE key ms               Set an expire time in milliseconds after that the `<key>` will be deleted, upon taking -1 as `<ms>` value the expire time will be removed
- KEYS                        List all keys stored into the keyspace
- VALUES                      List all values stored into the keyspace
- COUNT                       Return the number of key-value pair stored
- TAIL key offset             Like SUB, but with an `<offset>` representing how many messages discard starting from 0 (the very first chronologically)
- PREFSCAN key_prefix         Scan the keyspace finding all values associated to keys matching `<key_prefix>` as prefix
- FUZZYSCAN pattern           Scan the keyspace finding all values associated to keys matching `<pattern>` in a fuzzy search way
- FLUSH                       Delete all maps stored inside partitions
- QUIT/EXIT                   Close connection

All commands are case insensitive, so not strictly uppercase. Shibui can be used
as a basic message broker through commands PUB/SUB to publish messages to
multiple subscriber, using a key as a topic.
Shibui server expect an optional arg to set an arbitrary host to listen on port
6373, otherwise localhost will be automatically set.
Actually there's a simple client console called shibui-cli but it can be tested
using a generic tcp client like telnet or netcat.

Out of the common commands like SET, GET or COUNT who everyone probably knows
the behavior, TAIL key number is a command that allow a client to consume an
arbitrary number of chronologically sorted messages published to a specific key,
with number as the offset for the depletion. 0 means from the start of the
publication, increasing number discard the same quantity of messages starting
from the oldest going to younger. E.g:

![pubtail](https://github.com/codepr/shibui/tree/master/samples/pubtail.png)

All subsequent messages published to the key topic will be normally dispatched
like a normal SUB command.

Actually the keyspace is distributed across 1024 partitions based on consistent
hashing of the keys, this way should be simplier to eventually distribute data
across multiple nodes.

### Build

To build the source just run make. A shibui executable will be generated that
can be started to listen on localhost:6737, ready to receive commands from any
TCP client
```sh
    $ ./shibui <hostname>
```
To build shibui-cli just make shibui-cli and run it like the following:
```sh
    $ ./shibui-cli <hostname> <port>
```
### TODO

- Refactoring
- Persistence on disk, maybe through logging (started)
- CLI to send commands and format output (started)
- Additional commands (started)
- Distribution system (fairly hard)
- Write a good README

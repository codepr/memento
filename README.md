
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

ev

See the [CHANGELOG](CHANGELOG) file.

### License

See the [LICENSE](LICENSE) file for license rights and limitations (MIT).

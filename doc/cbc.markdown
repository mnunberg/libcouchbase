# cbc(1) - Couchbase Client Commandline Utility

## SYNOPSIS

`cbc` _COMMAND_ [_OPTIONS_]<br>
`cbc help`<br>
`cbc version`<br>
`cbc cat` _KEYS_ ... [_OPTIONS_]<br>
`cbc create` _KEY_ _-V VALUE_ [_OPTIONS_]<br>
`cbc create` _KEY_ [_OPTIONS_]<br>
`cbc cp` _FILES_ ... [_OPTIONS_]<br>
`cbc incr` _KEY_ [_OPTIONS_]<br>
`cbc decr` _KEY_ [_OPTIONS_]<br>
`cbc rm` _KEY_ [_OPTIONS_]<br>
`cbc hash` _KEY_ [_OPTIONS_]<br>
`cbc stats` _KEYS_ ... [_OPTIONS_]<br>
`cbc observe` _KEYS_ ... [_OPTIONS_]<br>
`cbc view` _VIEWPATH_ [_OPTIONS_]<br>
`cbc lock` _KEY_ [_OPTIONS_]<br>
`cbc unlock` _KEY_ _CAS_ [_OPTIONS_]<br>
`cbc admin` _-P PASSWORD_ _RESTAPI_ [_OPTIONS_]<br>
`cbc bucket-create` _-P PASSWORD_ _NAME_ [_OPTIONS_]<br>
`cbc bucket-delete` _-P PASSWORD_ _NAME_ [_OPTIONS_]<br>
`cbc bucket-flush` _NAME_ [_OPTIONS_]<br>
`cbc connstr` _SPEC_<br>

## DESCRIPTION

`cbc` is a utility for communicating with a Couchbase cluster.

`cbc` should be invoked with the command name first and then a series of command
options appropriate for the specific command. `cbc help` will always show the full
list of available commands.

<a name="OPTIONS"></a>
## OPTIONS

Options may be read either from the command line, or from a configuration file
(see cbcrc(4)):

The following common options may be applied to most of the commands

* `-U`, `--spec`=_SPEC_:
  A string describing the cluster to connect to. The string is in a URI-like syntax,
  and may also contain other options. See the [EXAMPLES](#examples) section for information.
  Typically such a URI will look like `couchbase://host1,host2,host3/bucket`.

  The default for this option is `couchbase://localhost/default`

* `-u`, `--username`=_USERNAME_:
  Specify the _username_ for the bucket. As of Couchbase Server 2.5 this field
  should be either left empty or set to the name of the bucket itself.

* `-P`, `--password`=_SASLPASS_:
  Specify the SASL password for the bucket. This is only needed if the bucket is
  protected with a password. Note that this is _not_ the administrative password
  used to log into the web interface.

* `-C`, `--bootstrap-protocol`=_CCCP|HTTP|BOTH_:
  Specify the bootstrap protocol the client should used when attempting to connect
  to the cluster. Options are: `CCCP`: Bootstrap using the Memcached protocol
  (supported on clusters 2.5 and greater); `HTTP`: Bootstrap using the HTTP REST
  protocol (supported on any cluster version); and `BOTH`: First attempt bootstrap
  over the Memcached protocol, and use the HTTP protocol if Memcached bootstrap fails.
  The default is `BOTH`

* `-t`, `--timeout`=_USECS_:
  Specify the operation timeout in microseconds. This is the time the client will
  wait for an operation to complete before timing it out. The default is
  `2500000`

* `-T`, `--timings`:
  Dump command timings at the end of execution. This will display a histogram
  showing the latencies for the commands executed.

* `-S` `--force-sasl-mech`=_MECH_:
  Force a specific _SASL_ mechanism to be used when performing the initial
  connection. This should only need to be modified for debugging purposes.
  The currently supported mechanisms are `PLAIN` and `CRAM-MD5`

* `-Z`, `--config-cache`:
  Enables the client to make use of a file based configuration cache rather
  than connecting for the bootstrap operation. If the file does not exist, the
  client will first connect to the cluster and then cache the bootstrap information
  in the file.


* `--ssl`=*ON|OFF|NO_VERIFY*:
  Use SSL for connecting to the cluster. The options are `ON` to enable full SSL
  support, `OFF` to disable SSL support, and `NO_VERIFY` to use SSL encryption
  but not attempt to verify the authenticity of the server's certificate.

* `--certpath`=_CERTIFICATE_:
  The path to the server's SSL certificate. This is typically required for SSL
  connectivity unless the certificate has already been added to the openssl
  installation on the system.

* `-v`, `--verbose`:
  Specify more information to standard error about what the client is doing. You may
  specify this option multiple times for increased output detail.


## COMMANDS

The following commands are supported by `cbc`. Unless otherwise specified, each
command supports all of the options above.

### cat

Write the value of keys to standard output.

This command requires that at least one key may be passed to it, but may accept
multiple keys. The keys should be specified as positional arguments after the
command.

In addition to the options in the [OPTIONS](#OPTIONS) section, the following options are supported:

* `r`, `--replica`=_all|INDEX_:
  Read the value from a replica server. The value for this option can either be
  the string `all` which will cause the client to request the value from each
  replica, or `INDEX` where `INDEX` is a 0-based replica index.

* `e`, `--expiry`=_EXPIRATION_:
  Specify that this operation should be a _get-and-touch_ operation in which the
  key's expiry time is updated along with retrieving the item.


### create

### cp

Create a new item in the cluster, or update the value of an existing item.
By default this command will read the value from standard input unless the
`--value` option is specified.

The `cp` command functions the same, except it operates on a list of files. Each file is
stored in the cluster under the name specified on the command line.

In addition to the options in the [OPTIONS](#OPTIONS) section, the following options are supported:

* `-V`, `--value`=_VALUE_:
  The value to store in the cluster. If omitted, the value is read from standard input. This
  option is valid only for the `create` command.

* `f`, `--flags`=_ITEMFLAGS_:
  A 32 bit unsigned integer to be stored alongside the value. This number is returned
  when the item is retrieved again. Other clients commonly use this value to determine
  the type of item being stored.

* `e`, `--expiry`=_EXPIRATION_:
  The number of time in seconds from now at which the item should expire.

* `a`, `--add`:
  Fail the operation if the item already exists in the cluster. Without specifying
  this option, if an existing item is already stored under the specified key

* `p`, `--persist-to`=_NUMNODES_:
  Wait until the item has been persisted to at least `NUMNODES` nodes' disk. If
  `NUMNODES` is 1 then wait until only the master node has persisted the item for
  this key. You may not specify a number greater than the number of nodes actually
  in the cluster.

* `r` `--replicate-to`=_NREPLICAS_:
  Wait until the item has been replicated to at least `NREPLICAS` replica nodes.
  The bucket must be configured with at least one replica, and at least `NREPLICAS`
  replica nodes must be online.


### observe

Retrieve persistence and replication information for items.

This command will print the status of each key to standard error.

See the [OPTIONS](#OPTIONS) for accepted options

### incr

### decr

These commands increment or decrement a _counter_ item in the cluster. A _counter_
is a value stored as an ASCII string which is readable as a number, thus for example
`42`.

These commands will by default refuse to operate on an item which does not exist in
the cluster.

The `incr` and `decr` command differ with how they treat the `--delta` argument. The
`incr` command will treat the value as a _positive_ offset and increment the current
value by the amount specified, whereas the `decr` command will treat the value as a
_negative_ offset and decrement the value by the amount specified.

In addition to [OPTIONS](#OPTIONS), the following options are supported:

* `--initial=_DEFAULT_`:
  Set the initial value for the item if it does not exist in the cluster. The value
  should be an unsigned 64 bit integer. If this option is not specified and the item
  does not exist, the operation will fail. If the item _does_ exist, this option is
  ignored.

* `--delta`=_DELTA_:
  Set the absolute delta by which the value should change. If the command is `incr`
  then the value will be _incremented_ by this amount. If the command is `decr` then
  the value will be _decremented_ by this amount. The default value for this option is
  `1`.

* `-e`, `--expiry`=_EXPIRATION_:
  Set the expiration time for the key, in terms of seconds from now.


### hash

Display mapping information for a key.

This command diplays mapping information about a key. The mapping information
indicates which _vBucket_ the key is mapped to, and which server is currently the
master node for the given _vBucket_.

See the [OPTIONS](#OPTIONS) for accepted options

<a name="lock"></a>
### lock

Lock an item in the cluster.

This will retrieve and lock an item in the cluster, making it inaccessible for
modification until it is unlocked (see [unlock](#unlock)).

In addition to the common options ([OPTIONS](#OPTIONS)), this command accepts the following
options:

* `e`, `--expiry`=_LOCKTIME_:
  Specify the amount of time the lock should be held for. If not specified, it will
  default to the server side maximum of 15 seconds.

<a name="unlock"></a>
### unlock

Unlock a previously locked item.

This command accepts two mandatory positional arguments which are the key and _CAS_ value.
The _CAS_ value should be specified as printed from the [lock][] command (i.e. with the
leading `0x` hexadecimal prefix).

See the [OPTIONS](#OPTIONS) for accepted options


### rm

Remove an item from the cluster.

This command will remove an item from the cluster. If the item does not exist, the
operation will fail.


See the [OPTIONS](#OPTIONS) for accepted options


### stats

Retrieve a list of cluster statistics. If positional arguments are passed to this
command, only the statistics classified under those keys will be retrieved. See the
server documentation for a full list of possible statistics categories.

This command will contact each server in the cluster and retrieve that node's own set
of statistics.

The statistics are printed to standard output in the form of `SERVER STATISTIC VALUE`
where _SERVER_ is the _host:port_ representation of the node from which has provided this
statistic, _STATISTIC_ is the name of the current statistical key, and _VALUE_ is the
value for this statistic.


See the [OPTIONS](#OPTIONS) for accepted options

### version

Display information about the underlying version of _libcouchbase_ to which the
`cbc` binary is linked.

### verbosity

Set the memcached logging versbosity on the cluster. This affects how the memcached
processes write their logs. This command accepts a single positional argument which
is a string describing the verbosity level to be set. The options are `detail`, `debug`
`info`, and `warning`.

### mcflush

Flush a _memcached_ bucket. This command takes no arguments, and will fail if the
bucket specified is not a memcached bucket. You may also use [bucket-flush](#bucket-flush)
to flush any bucket (including a couchbase bucket). The `mcflush` command may be
quicker for memcached buckets, though.

### view

Execute an HTTP request against the server's view (CAPI) interface.

The request may be one to create a design document, view a design document, or query a
view.

To create a design document, the definition of the document (in JSON) should be piped
to the command on standard input.

This command accepts one positional argument which is the _path_ (relative to the
bucket) to execute. Thus to query the `brewery_beers` view in the `beer` design
document within the `beer-sample` bucket one would do:
    cbc view -U couchbase://localhost/beer-sample _design/beer/_view/brewery_beers

In addition to the [OPTIONS](#OPTIONS) specified above, the following options are recognized:

* `-X`, `--method`=_GET|PUT|POST|DELETE_:
  Specify the HTTP method to use for the specific request. The default method is `GET`
  to query a view. To delete an existing design document, specify `DELETE`, and to
  create a new design document, specify `PUT`.


### admin

Execute an administrative request against the management REST API.
Note that in order to perform an administrative API you will need to provide
_administrative_ credentials to `cbc admin`. This means the username and password
used to log into the administration console.

This command accepts a single positional argument which is the REST API endpoint
(i.e. HTTP path) to execute.

If the request requires a _body_, it should be supplied via standard input

In addition to the [OPTIONS](#OPTIONS) specified above, the following options are recognized:

* `-X`, `--method`=_GET|PUT|POST|DELETE_:
  Specify the HTTP method to use for the specific request. The default method is
  `GET`.

### bucket-create

Create a bucket in the cluster.

This command will create a bucket with the name specified as the lone positional
argument on the command line.

As this is an administrative command, the `--username` and `--password` options should
be supplied administrative credentials.

In addition to the [OPTIONS](#OPTIONS) specified above, the following options are recognized:

* `--bucket-type`=_couchbase|memcached_:
  Specify the type of bucket to create. A _couchbase_ bucket has persistence to disk and
  replication. A _memached_ bucket is in-memory only and does not replicate.

* `--ram-quota`=_QUOTA_:
  Specify the maximum amount of memory the bucket should occupy (per node) in megabytes.
  If not specified, the default is _512_.

* `--bucket-password`=_PASSWORD_:
  Specify the password to secure this bucket. If passed, this password will be required
  by all clients attempting to connect to the bucket. If ommitted, this bucket may be
  accessible to everyone for both read and write access.

* `--num-replicas`=_REPLICAS_:
  Specify the amount of replicas the bucket should have. This will set the number of nodes
  each item will be replicated to. If not specified the default is _1_.


### bucket-flush

This command will flush the bucket with the name specified as the lone positional
argument on the command line.

This command does not require administrative level credentials, however it does
require that _flush_ be enabled for the bucket.

See the [OPTIONS](#OPTIONS) for accepted options

### connstr 

This command will parse a connection string into its constituent parts and
display them on the screen. The command takes a single positional argument
which is the string to parse.

## EXAMPLES

Store a file to the cluster:

    $ cbc cp mystuff.txt
    mystuff.txt         Stored. CAS=0xe15dbe22efc1e00


Retrieve persistence/replication information about an item (note that _Status_
is a set of bits):

    $ cbc observe mystuff.txt
    mystuff              [Master] Status=0x80, CAS=0x0


Display mapping information about keys:

    $cbc hash foo bar baz
    foo: [vBucket=115, Index=3] Server: cbnode3:11210, CouchAPI: http://cbnode3:8092/default
    bar: [vBucket=767, Index=0] Server: cbnode1:11210, CouchAPI: http://cbnode1:8092/default
    baz: [vBucket=36, Index=2] Server: cbnode2:11210, CouchAPI: http://cbnode2:8092/default

Create a bucket:

    $ cbc bucket-create --bucket-type=memcached --ram-quota=100 --password=letmein -u Administrator -P 123456 mybucket
    Requesting /pools/default/buckets
    202
      Cache-Control: no-cache
      Content-Length: 0
      Date: Sun, 22 Jun 2014 22:43:56 GMT
      Location: /pools/default/buckets/mybucket
      Pragma: no-cache
      Server: Couchbase Server

Flush a bucket:

    $ cbc bucket-flush default
    Requesting /pools/default/buckets/default/controller/doFlush


    200
      Cache-Control: no-cache
      Content-Length: 0
      Date: Sun, 22 Jun 2014 22:53:44 GMT
      Pragma: no-cache
      Server: Couchbase Server

Delete a bucket:

    $ cbc bucket-delete mybucket -P123456
    Requesting /pools/default/buckets/mybucket
    200
      Cache-Control: no-cache
      Content-Length: 0
      Date: Sun, 22 Jun 2014 22:55:58 GMT
      Pragma: no-cache
      Server: Couchbase Server

Use `cbc stats` to determine the minimum and maximum timeouts for a lock operation:

    $ cbc stats | grep ep_getl
    localhost:11210 ep_getl_default_timeout 15
    localhost:11210 ep_getl_max_timeout 30


Create a design document:

    $ echo '{"views":{"all":{"map":"function(doc,meta){emit(meta.id,null)}"}}}' | cbc view -X PUT _design/blog
    201
      Cache-Control: must-revalidate
      Content-Length: 32
      Content-Type: application/json
      Date: Sun, 22 Jun 2014 23:03:40 GMT
      Location: http://localhost:8092/default/_design/blog
      Server: MochiWeb/1.0 (Any of you quaids got a smint?)
    {"ok":true,"id":"_design/blog"}


Query a view:

    $ cbc view _design/blog/_view/all?limit=5
    200
      Cache-Control: must-revalidate
      Content-Type: application/json
      Date: Sun, 22 Jun 2014 23:06:09 GMT
      Server: MochiWeb/1.0 (Any of you quaids got a smint?)
      Transfer-Encoding: chunked
    {"total_rows":20,"rows":[
    {"id":"bin","key":"bin","value":null},
    {"id":"check-all-libev-unit-tests.log","key":"check-all-libev-unit-tests.log","value":null},
    {"id":"check-all-libevent-unit-tests.log","key":"check-all-libevent-unit-tests.log","value":null},
    {"id":"check-all-select-unit-tests.log","key":"check-all-select-unit-tests.log","value":null},
    {"id":"cmake_install.cmake","key":"cmake_install.cmake","value":null}
    ]
    }


## FILES

cbc(1) and cbc-pillowfight(1) may also read options from cbcrc(4)

## BUGS

The options in this utility and their behavior are subject to change. This script
should be used for experiemntation only and not inside production scripts.


## SEE ALSO

cbc-pillowfight(1), cbcrc(4)


## History

The cbc command first appeared in version 0.3.0 of the library. It was significantly
rewritten in version 2.4.0

# cbc-pillowfight(1) - Stress Test for Couchbase Client and Cluster

## SYNOPSIS

`cbc-pillowfight` [_OPTIONS_]

## DESCRIPTION

`cbc-pillowfight` creates a specified number of threads each looping and
performing get and set operations within the cluster.

The stress test operates in the following order

1. It will pre-load the items in the cluster (set by the `--num-items` option)

2. Once the items are all loaded into the cluster, it will access all the items
(within the `--num-items`) specification, using a combination of storage and
retrieval operations (the proportion of retrieval and storage operations are
controlled via the `--set-pct` option).

3. Operations are scheduled in _batches_. The batches represent a single pipeline
(or network buffer) which is filled with a certain amount of operations (see the
`--batch-size` option). These batch sizes are then sent over to the cluster and
the requests are serviced by it.


### Tuning

Getting the right benchmark numbers highly depends on the type of environment
the client is being run in. The following provides some information about
specific settings which may make `pillowfight` generate more operations.

* Increasing the batch size will typically speed up operations, but increasing
  the batch size too much will actually slow it down. Additionally, very high
  batch sizes will cause high memory usage.

* Adding additional threads will create additional client objects and connections,
  potentially increasing performance. Adding too many threads will cause local
  and network resource congestion.

* Decreasing the item sizes (the `--min-size` and `--max-size` options) will
  always yield higher performance in terms of operationd-per-second.

* Limiting the working set (i.e. `--num-items`) will decrease the working set
  within the cluster, thereby increasing the chance that a given item will be
  inside the server's CPU cache (which is extremely fast), rather than in main
  memory (slower), or disk (much slower)

## OPTIONS

Options may be read either from the command line, or from a configuration file
(see cbcrc(4)):

The following options control workload generation:

* `-B`, `--batch-size`=_BATCHSIZE_:
  This controls how many commands are scheduled per cycles. To simulate one operation
  at a time, set this value to 1.

* `-I`, `--num-items`=_NUMITEMS_:
  Set the _total_ number of items the workload will access within the cluster. This
  will also determine the working set size at the server and may affect disk latencies
  if set to a high number.

* `-p`, `--key-prefix`=_PREFIX_:
  Set the prefix to prepend to all keys in the cluster. Useful if you do not wish the items
  to conflict with existing data.

* `-t`, `--num-threads`=_NTHREADS_:
  Set the number of threads (and thus the number of client instances) to run
  concurrently. Each thread is assigned its own client object.

* `-r`, `--set-pct`=_PERCENTAGE_:
  The percentage of operations which should be mutations. A value of 100 means
  only mutations while a value of 0 means only retrievals.

* `-n`, `--no-population`:
  By default `cbc-pillowfight` will load all the items (see `--num-items`) into
  the cluster and then begin performing the normal workload. Specifying this
  option bypasses this stage. Useful if the items have already been loaded in a
  previous run.

* `-m`, `--min-size`=_MINSIZE_:
* `-M`, `--max-size`=_MAXSIZE_:
  Specify the minimum and maximum value sizes to be stored into the cluster.
  This is typically a range, in which case each value generated will be between
  `--min-size` and `--max-size` bytes.

* `-E`, `--pause-at-end`:
  When the workload completes, do not exit immediately, but wait for user input.
  This is helpful for analyzing open socket connections and state.

* `-c`, `--num-cycles`:
  Specify the number of times the workload should cycle. During each cycle
  an amount of `--batch-size` operations are executed. Setting this to `-1`
  will cause the workload to run infinitely.

* `--sequential`:
  Specify that the access pattern should be done in a sequential manner. This
  is useful for bulk-loading many documents in a single server.

* `--start-at`:
  This specifies the starting offset for the items. The items by default are
  generated with the key prefix (`--key-prefix`) up to the number of items
  (`--num-items`). The `--start-at` value will increase the lower limit of
  the items. This is useful to resume a previously cancelled load operation.

* `-T`, `--timings`:
  Dump a histogram of command timings and latencies to the screen every second.


The following options control how `cbc-pillowfight` connects to the cluster

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


## TODO

Rather than spawning threads for multiple instances, offer a way to have multiple
instances function cooperatively inside an event loop.

## BUGS

This command's options are subject to change.

## SEE ALSO

cbc(1), cbcrc(4)

## HISTORY

The `cbc-pillowfight` tool was first introduced in libcouchbase 2.0.7

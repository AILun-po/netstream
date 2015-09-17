Netstream - the parallel network streamer
=========================================

Netstream is a simple streamer with one input and many outputs. Input and
outputs could be files, sockets or standard input/output. Streaming is parallel
and can be restarted on failure. 

Netstream is designed for a parallel streaming of a music or video from one
source to many receivers. Because of it, netstream is not designed for a
lossless data transmission, on a slow connection some data can be dropped. The
order of the data does not change. 

Compilation 
----------- 

Required libraries: libyaml (http://pyyaml.org/wiki/LibYAML)

To compile run

`$ make`

`netstream` binary will be created.


Usage 
-----
For simple use, just run `./netstream`. It will use netstream.conf as
configuration file and start working.

## Command line options
 - `-c <file>`  use `file` as a configuration file
 - `-d`	 	run as a daemon
 - `-v [level]`	set verbosity (0 - quiet, 7 - most verbose)
 - `-t`		only test connection to neighbours and exit

## Configuration file syntax
Configuration file is written in YAML. It is an array of mappings. Each mapping
describes one input or output. There can be only 1 input. Maximal number of
outputs is defined by a constant `MAX_OUTPUTS` in the source.

Compulsory keys for any endpoint
  - `Direction`:  `input` or `output`
  - `Type`: `socket`,`file` or `std`

Optional keys for any endpoint
  - `Retry`: `yes` for retrying after failure, `no` for exit after failure
    (default) and `ignore` for don't exit and don't retry after failure

Compulsory keys for `Type: socket`:
  - `Name`: hostname or IP of the target computer
  - `Port`: port number
  - `Protocol`: `TCP` or `UDP`

Optional keys for `Type: socket` and `Protocol: TCP`:
  - `Keepalive`: send TCP keepalive every n seconds 

Compulsory keys for `Type: file`:
  - `Name`: filename

If there is a syntax error in config or some compulsory keys are missing,
program will exit with error. Unnecessary keys are ignored.

## Configuration file parameters explanation
When the `Retry` key is set to `yes` for some endpoint, then after EOF or error is
the socket or file closed and netstream tries to open it again until it
succeeds. There is a delay between tries.

When the `Retry` key is set to ignore, then after a failure is the socket or
file closed and it is not used anymore.

If the `Type` key has a value `std`, netstream reads or writes to standard input
or output.

When `Keepalive` is not set or it is set to 0, system default keepalive is used.


Tests 
-----

There are some simple tests in the `test/` directory. The tests covers:
  1. from file to file
  2. from file to TCP connection
  3. from file to UDP connection (tries at most 10 times)
  4. from TCP connection to TCP connection
  5. from UDP to UDP connection
  6. from file to more files
  7. from file to multiple files
  8. exit unsuccessfully on wrong config file

Tests can be started by a `./run_tests` command.

Test are not rock-solid, failure of the test can be caused by too slow flushing
buffers, it is recommended to re-run tests to make sure.

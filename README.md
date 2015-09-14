Netstream - the parallel network streamer
=========================================

Nestream is a simple streamer with one input and many outputs. Input and
outputs could be files, sockets or standard input/output. Streaming is parallel
and can be restarted on failure.

Compilation 
----------- 

Required libraries: libyaml (http://pyyaml.org/wiki/LibYAML)
To compile, run

`$ make`

netstream binary will be created.


Usage 
-----
For simple use, just run `./netstream`. It will use netstream.conf as
configuration file and start working.

## Command line options
 - `-c <file>` - use `file` as a configuration file
 - `-d` - run as a daemon
 - `-v [level]` - set verbosity (0 - quiet, 7 - most verbose)
 - `-t` - only test connection to neighbours and exit

## Configuration file syntax
Configuration file is written in YAML. 

Tests 
-----



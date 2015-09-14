Netstream - the parallel network streamer
=========================================

Nestream is a simple streamer with one input and many outputs. Input and
outputs could be files, sockets or standard input/output. Streaming is parallel
and can be restarted on failure.

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

Optional keys for `Type: socket`:
  - `Keepalive`: send TCP keepalive every n seconds

Compulsory keys for `Type: file`:
  - `Name`: filename

If there is a syntax error in config or some compulsory keys are missing,
program will exit with error. Unnecessary keys are ignored.

Tests 
-----



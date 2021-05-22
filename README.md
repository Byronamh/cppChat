# cppChat




## About
This repository has the makefile and source code for a chat application using TCP sockets.

This project is made in c++ for a linux environment. It has two files and executables, `client` and `server`.
The `client` requires a `server` instance running somewhere in the network (can be external).


## Dependencies
This project uses [protocol buffers](https://developers.google.com/protocol-buffers) as message format protocol.
You can see the instructions on how to install the compiler [here](https://developers.google.com/protocol-buffers/docs/downloads).

## Building
The Makefile has three different options, `protocol` (builds the protobuf cpp library), `server`(builds the server) and `client`(builds the client).

## Running

To run the server: `$ ./server <PORT>`

To run the client `$ ./client <PORT> <UNAME> <SERVER IP>`

all: protocol server client

protocol: payload.proto
	protoc -I=. --cpp_out=. payload.proto
server: server.cpp payload.pb.cc
	g++ -std=c++11 -o server server.cpp payload.pb.cc -lpthread -lprotobuf
client: client.cpp payload.pb.cc
	g++ -std=c++11 -o client client.cpp payload.pb.cc -lpthread -lprotobuf

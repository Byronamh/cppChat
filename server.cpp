#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <signal.h>
#include<ctime>

#include "payload.pb.h"

#include <atomic>

#define MAX_CLIENT_CT 100
#define BUFFER_SIZE 2048
#define DEFAULT_SERVER_IP "127.0.0.1"
#define DEFAULT_SERVER_PORT 3000
#define MAX_USERNAME_LENGTH  30
#define DC_WORD "endconn"

using namespace std;


/* Client structure */
class Client {
public:
    Client();

    Client(sockaddr_in _address, int _socket) {
        address = _address;
        socket = _socket;
        name = "";
        status = 0;
        lastActionTimestamp = time(0);
        serverReady = false;
    }


    void setName(string _name) {
        name = _name;
        serverReady = true;
    }

    void setStatus(int _status) {
        status = _status;
    }

    void updateTimestamp() {
        lastActionTimestamp = time(0);
    }

    sockaddr_in getAddr() {
        return address;
    }

    int getSocket() {
        return socket;
    }

    string getName() {
        return name;
    }

    bool isReady() {
        return serverReady;
    }

    int getStatus() {
        return status;
    }

private:
    struct sockaddr_in address;
    int socket;
    string name;
    int status;
    int lastActionTimestamp;
    bool serverReady;
};

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
vector <pthread_t> threadRegistry;
string statusList[3] = {"ACTIVO", "OCUPADO", "INACTIVO"};
int srvrPort;


/**@example:
{
    {<string>'someUsername':<Client>{...}}
}
 */
map<string, Client *> clientRegistry = {};

void pushMessageToClient(Payload messagePayload, Client *recieverDataPtr) {
    cout << "PUSH MSG REQUEST" << endl;
    Client recieverData = *recieverDataPtr;

    string messageBuffer;
    messagePayload.SerializeToString(&messageBuffer);

    pthread_mutex_lock(&clients_mutex);
    if (clientRegistry.count(recieverData.getName()) > 0) {
        cout << "CLIENT FOUND, SENDING MSG" << endl;
        if (write(recieverData.getSocket(), messageBuffer.c_str(), messageBuffer.length()) < 0) {
            cout << "COULD NOT WRITE TO SOCKET OF USER: " << recieverData.getName() << ". Removing from userlist"
                 << endl;
            clientRegistry.erase(recieverData.getName());
        } else {
            cout << "MESSAGE DELIVERY OK" << endl;
        }
    } else {
        cout << "COULD NOT FIND " << recieverData.getName() << endl;
    }
    pthread_mutex_unlock(&clients_mutex);

}

void broadCastMessage(Payload messagePayload, Client *senderPtr) {
    cout << "BROADCAST REQUEST" << endl;
    Client sender = *senderPtr;
    map<string, Client *>::iterator it = clientRegistry.begin();

    for (auto const &it:clientRegistry) {
        pushMessageToClient(messagePayload, it.second);
    }
}

void routeMessage(char *encodedMessagePtr, Client *senderPtr) {
    Client sender = *senderPtr;

    string encodedMessage(encodedMessagePtr);
    Payload inMessagePayload;
    Payload outMessagePayload;
    Client *destinationClient = senderPtr;

    inMessagePayload.ParseFromString(encodedMessage);
    string outputMessage;

    switch (inMessagePayload.flag()) {
        case Payload_PayloadFlag::Payload_PayloadFlag_user_list: {
            cout << "GOT USERLIST REQUEST" << endl;
            map<string, Client *>::iterator it = clientRegistry.begin();
            for (auto const &it:clientRegistry) {
                Client *currentClientPtr = it.second;
                Client currentClient = *currentClientPtr;
                if (currentClient.isReady() && strcmp(currentClient.getName().c_str(), sender.getName().c_str()) != 0) {
                    outputMessage = outputMessage + "\n" + currentClient.getName() + ": " +
                                    statusList[currentClient.getStatus()] +
                                    ".";
                }
            }
            if (outputMessage.empty()) {
                outputMessage = "You are alone in this server...";
            }
            outMessagePayload.set_sender("server");
            break;

        };
        case Payload_PayloadFlag::Payload_PayloadFlag_user_info: {
            string wantedUserName = inMessagePayload.extra();
            if (clientRegistry.count(wantedUserName) > 0) {
                Client wantedClientData = *clientRegistry[wantedUserName];
                outputMessage = "Username: " + wantedClientData.getName() +
                                ". Status: " + statusList[wantedClientData.getStatus()];
            } else {
                outputMessage = "User not found";
            }
            break;
        };
        case Payload_PayloadFlag::Payload_PayloadFlag_private_chat: {

            string recieverUname = inMessagePayload.extra();

            outMessagePayload.set_sender(inMessagePayload.sender());

            outputMessage = "Failed to send a message to " + recieverUname + ". Not found.";

            if (clientRegistry.count(recieverUname) > 0) {
                Client *wantedClientDataPtr = clientRegistry[recieverUname];
                Client wantedClientData = *wantedClientDataPtr;
                if (wantedClientData.isReady()) {
                    outputMessage = sender.getName() + "<prv>: " + inMessagePayload.message();
                    destinationClient = wantedClientDataPtr;
                }
            }
            break;

        };
        case Payload_PayloadFlag::Payload_PayloadFlag_update_status: {

            string requestedClientStatus = inMessagePayload.extra();
            string currentClientStatus = statusList[sender.getStatus()];
            outputMessage = "You already have that status";

            if (strcmp(requestedClientStatus.c_str(), currentClientStatus.c_str()) != 0) {
                int i;
                for (i = 0; i < 3; i++) {
                    if (strcmp(requestedClientStatus.c_str(), statusList[i].c_str()) == 0) {
                        outputMessage = "Updated status from: " + requestedClientStatus + " to " + statusList[i];
                        sender.setStatus(i);
                    }
                }
            }
            outMessagePayload.set_sender("server");
            break;

        };
        case Payload_PayloadFlag::Payload_PayloadFlag_general_chat: {
            broadCastMessage(inMessagePayload, senderPtr);
            break;
        };
        default: {
            cout << "Invalid message recieved" << endl;
            break;
        }
    }

    outMessagePayload.set_code(200);
    outMessagePayload.set_message(outputMessage);
    pushMessageToClient(outMessagePayload, destinationClient);
}


void kickClient(Client *clientDataPtr) {
    Client clientData = *clientDataPtr;

    Payload kickMessagePayload;
    kickMessagePayload.set_code(401);
    kickMessagePayload.set_sender("server");
    kickMessagePayload.set_message("You were removed from the server");

    pushMessageToClient(kickMessagePayload, clientDataPtr);
    clientRegistry.erase(clientData.getName());//remove from registry
}

void *userThreadFn(void *arg) {
    char inMessageBuffer[BUFFER_SIZE + MAX_USERNAME_LENGTH];
    int *incomingConnectionPID = (int *) arg;
    cout << "USER THREAD EXECUTE OK" << endl;
    pthread_mutex_lock(&clients_mutex);


    map<string, Client *>::iterator it;

    it = clientRegistry.find(to_string(*incomingConnectionPID));
    Client *clientDataPtr = it->second;
    Client clientData = *clientDataPtr;

    pthread_mutex_unlock(&clients_mutex);

    bool exitFlag = false;

    if (recv(clientData.getSocket(), inMessageBuffer, BUFFER_SIZE + MAX_USERNAME_LENGTH, 0) <= 0) {
        cout << "GOT 0 BYTES OF DATA FROM CLIENT, CLOSING CONNECTION" << endl;
        exitFlag = true;
    }
    cout << "CLIENT HANDSHAKE OK" << endl;

    string inMessageStr(inMessageBuffer); //contains the incoming message
    Payload register_payload;
    register_payload.ParseFromString(inMessageStr);

    string incomingClientName = register_payload.sender();

    cout << "GET REGISTER MESSAGE OK" << endl;
    if (incomingClientName.length() < 4 || incomingClientName.length() > MAX_USERNAME_LENGTH) {
        cout << "ERROR: INVALID USERNAME LENGTH OR MISSING";
        exitFlag = true;
    }
    if (strcmp(incomingClientName.c_str(), "server") == 0) {
        cout << "ERROR: INVALID USERNAME, RESERVED";
        exitFlag = true;
    }
    if (clientRegistry.count(incomingClientName) > 0 && !exitFlag) {
        cout << "ERROR: CLIENT WITH THAT NAME ALREADY EXISTS";
        exitFlag = true;
    } else {

        //remove original reference from map, start new reference with username
        clientData.setName(incomingClientName);
        clientRegistry[clientData.getName()] = &clientData;
        clientRegistry.erase(to_string(*incomingConnectionPID));

        //create message
        cout << clientData.getName() << " JOINED THE SERVER" << endl;

        clientDataPtr = &clientData;

        Payload newUserMessagePayload;
        newUserMessagePayload.set_message(incomingClientName + " JOINED THE SERVER");
        newUserMessagePayload.set_sender("server");
        newUserMessagePayload.set_flag(Payload_PayloadFlag::Payload_PayloadFlag_general_chat);
        newUserMessagePayload.set_code(200);

        broadCastMessage(newUserMessagePayload, clientDataPtr);

        Payload reigstrationConfirmMessage;
        reigstrationConfirmMessage.set_message("REGISTRATION OK");
        reigstrationConfirmMessage.set_sender("server");
        cout << "REGISTRATION OK" << endl;
        reigstrationConfirmMessage.set_flag(Payload_PayloadFlag::Payload_PayloadFlag_private_chat);
        reigstrationConfirmMessage.set_code(200);

        pushMessageToClient(reigstrationConfirmMessage, clientDataPtr);
    }
    cout << "MAIN CLIENT THREAD LOOP STARTED" << endl;
    for (;;) {
        if (exitFlag) {
            break;
        }
        bzero(inMessageBuffer, BUFFER_SIZE);//reset message buffer every iteration
        int incomingData = recv(clientData.getSocket(), inMessageBuffer, BUFFER_SIZE, 0);
        cout << "incoming data: " << incomingData << endl;
        if (incomingData >= 0) {
            if (strlen(inMessageBuffer) > 0) {
                routeMessage(inMessageBuffer, clientDataPtr);
                cout << clientData.getName() << " GOT A MESSAGE" << endl;
            } else if (incomingData == 0 || strcmp(inMessageBuffer, DC_WORD) == 0) {
                cout << clientData.getName() << " HAS LEFT THE CHAT" << endl;

                Payload userLeftTheChatPayload;
                userLeftTheChatPayload.set_message(clientData.getName() + " LEFT THE SERVER");
                userLeftTheChatPayload.set_sender("server");
                userLeftTheChatPayload.set_flag(Payload_PayloadFlag::Payload_PayloadFlag_general_chat);
                userLeftTheChatPayload.set_code(200);
                broadCastMessage(userLeftTheChatPayload, clientDataPtr);
                clientRegistry.erase(clientData.getName());

                exitFlag = true;
            } else {
                cout << "??? something happened, kill the user" << endl;
                kickClient(clientDataPtr);
                exitFlag = true;
            }
        }
        bzero(inMessageBuffer, BUFFER_SIZE);

    }
    kickClient(clientDataPtr);
    close(clientData.getSocket());
    pthread_detach(pthread_self());//close thread before ending

    return nullptr;
}

int main(int argc, char **argv) {
    if (strcmp(argv[1], "-h") == 0) {
        cout << "Params: <PORT>" << endl;
        return 0;
    }
    if (argv[1]) {
        srvrPort = atoi(argv[1]);
    } else {
        srvrPort = DEFAULT_SERVER_PORT;
    }

    int option = 1;
    int connSocket = 0, incomingConnectionPID = 0;
    struct sockaddr_in serverConfigs;


    /* Socket settings */
    connSocket = socket(AF_INET, SOCK_STREAM, 0);
    serverConfigs.sin_family = AF_INET;
    serverConfigs.sin_addr.s_addr = inet_addr(DEFAULT_SERVER_IP);
    serverConfigs.sin_port = htons(srvrPort);


    if (setsockopt(connSocket, SOL_SOCKET, (SO_REUSEPORT | SO_REUSEADDR), (char *) &option, sizeof(option)) < 0) {
        cout << "ERROR: COULD NOT INSTANCE setsockopt" << endl;
        return 1;
    }

    if (bind(connSocket, (struct sockaddr *) &serverConfigs, sizeof(serverConfigs)) < 0) {
        cout << "ERROR: COULD NOT BIND SOCKET" << endl;
        return 1;
    }

    if (listen(connSocket, 10) < 0) {
        cout << "ERROR: COULD NOT START SOCKET LISTEN" << endl;
        return 1;
    }
    cout << "SERVER START OK" << endl;

    struct sockaddr_in clientSockAddr;
    pthread_t userThread;

    for (;;) {
        socklen_t clilen = sizeof(clientSockAddr) + MAX_USERNAME_LENGTH;
        incomingConnectionPID = accept(connSocket, (struct sockaddr *) &clientSockAddr, &clilen);

        int clientCount = clientRegistry.size();
        if (clientCount < MAX_CLIENT_CT) {
            cout << "ACCEPTED CONNECTION " << to_string(incomingConnectionPID) << endl;
            Client *newClientPtr = new Client(clientSockAddr, incomingConnectionPID);
            clientRegistry.insert(make_pair(to_string(incomingConnectionPID), newClientPtr));
            cout << "REGISTRY CREATION FOR NEW USER OK" << endl;
            pthread_create(&userThread, NULL, &userThreadFn, (void *) &incomingConnectionPID);

            cout << "THREAD CREATION FOR NEW USER OK" << endl;
            threadRegistry.push_back(userThread);

        } else {
            cout << "MAX CLIENT CONNECTIONS REACHED" << endl;
            close(incomingConnectionPID);
            continue;
        }
    }
    //kill remaining threads
    for (pthread_t uthread: threadRegistry) {
        pthread_join(uthread, nullptr);
    }
    cout << "SERVER EXIT" << endl;
    return 0;
}

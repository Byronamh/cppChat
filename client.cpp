#include <iostream>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <atomic>
#include <ifaddrs.h>
#include "payload.pb.h"

#define BUFFER_SIZE  2048
#define MAX_USERNAME_LENGTH  30
#define DEFAULT_SERVER_PORT 3000
#define DEFAULT_SERVER_IP "127.0.0.1"
#define DEFAULT_CLIENT_NAME "bmota"
#define LOCAL_IP "127.0.0.1"
#define DC_WORD "endconn"

using namespace std;

string statusList[3] = {"ACTIVO", "OCUPADO", "INACTIVO"};

atomic<int> exitFLag(0);
int connSocket = 0;
int srvrPort;
string srvrIp;
string clientName;

pthread_t sendMessageThread;
pthread_t getMessageThread;

void printMenu() {
    cout << "1) Print menu" << endl;
    cout << "2) Broadcast message" << endl;
    cout << "3) Private message" << endl;
    cout << "4) Change status" << endl;
    cout << "5) View User list" << endl;
    cout << "6) View User info" << endl;
    cout << "7) Exit" << endl;
    cout << "Enter the desired option (number): ";
}

void *sendMessageThreadFn(void *arg) {
    int opt;//menu option
    char outMessageBuffer[BUFFER_SIZE + MAX_USERNAME_LENGTH]; //
    printMenu();
    for (;;) {
        opt = 1;//default show menu
        if (exitFLag.load()) {
            break;
        }
        Payload actionPayload;

        bool sendMessage = false;
        try {
            cin >> opt;
            cin.ignore();
        } catch (exception &e) {
            cout << endl << "INVALID OPTION FORMAR, INPUT NUMBER" << endl;
            continue;
        }
        switch (opt) {
            case 1: {
                printMenu();
                break;
            };
            case 2: {
                actionPayload.set_flag(Payload_PayloadFlag::Payload_PayloadFlag_general_chat);

                string messageBody;
                cout << "Input broadcast message: ";
                getline(cin, messageBody);
                if ((messageBody.empty())) {
                    continue;
                }
                actionPayload.set_message(messageBody);
                sendMessage = true;
                break;
            };
            case 3: {
                actionPayload.set_flag(Payload_PayloadFlag::Payload_PayloadFlag_private_chat);

                string receiverUsername;
                string messageBody;
                cout << "Input the receiver username: ";
                getline(cin, receiverUsername);

                cout << endl << "Input the message: " << endl;
                getline(cin, messageBody);

                actionPayload.set_message(messageBody);
                actionPayload.set_extra(receiverUsername);
                if (messageBody.empty() && receiverUsername.empty()) {
                    continue;
                }
                sendMessage = true;
                break;
            };
            case 4: {
                actionPayload.set_flag(Payload_PayloadFlag::Payload_PayloadFlag_update_status);
                cout << "Input status option: ";
                int optionIndex;
                int i;
                for (i = 0; i < 3; i++) {
                    cout << i << ") " << statusList[i] << endl;
                }
                try {
                    cin >> optionIndex;
                } catch (exception &e) {
                    cout << endl << "INVALID OPTION FORMAR, INPUT NUMBER" << endl;
                    continue;
                }
                if (optionIndex < 0 || optionIndex > 3) {
                    cout << "INVALID OPTION" << endl;
                    continue;
                }

                actionPayload.set_message(statusList[optionIndex]);
                actionPayload.set_extra(statusList[optionIndex]);
                sendMessage = true;
                break;

            };
            case 5: {
                actionPayload.set_flag(Payload_PayloadFlag::Payload_PayloadFlag_user_list);
                actionPayload.set_message("<GET ULIST>");

                sendMessage = true;
                break;

            };
            case 6: {
                actionPayload.set_flag(Payload_PayloadFlag::Payload_PayloadFlag_user_info);

                string usernameToCheck;
                cout << "Input the username: ";
                getline(cin, usernameToCheck);
                if (usernameToCheck.empty()) {
                    cout << "Warning: Didn't input username";
                    continue;
                }
                if (strcmp(usernameToCheck.c_str(), clientName.c_str()) == 0) {
                    cout << "Warning: You can't check yourself";
                    continue;
                }

                actionPayload.set_extra(usernameToCheck);
                actionPayload.set_message(usernameToCheck);
                sendMessage = true;
                break;

            };
            case 7: {
                exitFLag = 1;
                actionPayload.set_flag(Payload_PayloadFlag::Payload_PayloadFlag_general_chat);
                actionPayload.set_message(DC_WORD);
                sendMessage = true;
                break;
            };
            default: {
                cout << "INVALID OPTION, TRY AGAIN (1-7)" << endl;
                break;

            };
        }
        if (sendMessage) {
            actionPayload.set_sender(clientName);
            actionPayload.set_ip(LOCAL_IP);

            string outMessageStr;
            actionPayload.SerializeToString(&outMessageStr);
            char outMessageBuffer[BUFFER_SIZE + MAX_USERNAME_LENGTH];
            strcpy(outMessageBuffer, outMessageStr.c_str());
            send(connSocket, outMessageBuffer, strlen(outMessageBuffer), 0);
            bzero(outMessageBuffer, BUFFER_SIZE + MAX_USERNAME_LENGTH);//reset message outMessageBuffer every iteration
        }
    }
    cout << "INPUT LOOP BROKEN" << endl;
    return nullptr;
}

void *getMessageThreadFn(void *arg) {

    char rawMessage[BUFFER_SIZE];
    for (;;) {
        if (exitFLag.load()) {
            break;
        }
        int sockIncomingMessage = recv(connSocket, rawMessage, BUFFER_SIZE, 0);
        if (sockIncomingMessage > 0) {
            Payload srvrMessagePayload;
            srvrMessagePayload.ParseFromString(rawMessage);
            switch (srvrMessagePayload.code()) {
                //MSG OK
                case 200: {
                    cout << srvrMessagePayload.sender() << ": " << srvrMessagePayload.message() << endl;
                    break;
                };
                    //Removed from server
                case 400:
                case 401: {
                    cout << srvrMessagePayload.sender() << ": " << srvrMessagePayload.message() << endl;
                    cout << "YOU HAVE BEEN KICKED FROM THE SERVER" << endl;
                    break;
                }
                    //any other, assume error and close cnn if not broadcast message
                default: {
                    if (srvrMessagePayload.flag() == Payload_PayloadFlag::Payload_PayloadFlag_general_chat) {
                        cout << srvrMessagePayload.sender() << ": " << srvrMessagePayload.message() << endl;
                    } else {
                        cout << "SERVER ERROR" << endl;
                    }
                    break;
                }
            }
        } else if (sockIncomingMessage == 0) {
            break; //error or closed message
        }
        memset(rawMessage, 0, sizeof(rawMessage));
    }
    cout << "GET MESSAGE LOOP BROKEN" << endl;
    return nullptr;
}

int main(int argc, char **argv) {

    if (strcmp(argv[1], "-h") == 0) {
        cout << "Params: <PORT> <UNAME> <IP>" << endl;
        return 0;
    }
    //set default flags
    if (argv[1]) {
        srvrPort = stoi(argv[1]);
    } else {
        srvrPort = DEFAULT_SERVER_PORT;
    }

    if (argv[2]) {
        clientName = argv[2];
    } else {
        clientName = DEFAULT_CLIENT_NAME;
    }

    if (argv[3]) {
        srvrIp = argv[3];
    } else {
        srvrIp = DEFAULT_SERVER_IP;
    }

    //required flags set, validate data
    if (srvrPort < 0) {
        cout << "ERROR: INVALID PORT" << endl;
        return 1;
    }
    if (strlen(srvrIp.c_str()) < 8) {//min ip is composed of single digits and points
        cout << "ERROR: INVALID IP" << endl;
        return 1;
    }
    if (strlen(clientName.c_str()) > MAX_USERNAME_LENGTH || strlen(clientName.c_str()) < 4) {
        cout << "ERROR: INVALID USERNAME: MUST BE BETWEEN 4 AND 30 CHARS IN LENGTH" << endl;
        return 1;
    }

//    Connection configs
    struct sockaddr_in serverConfigs;
    connSocket = socket(AF_INET, SOCK_STREAM, 0);
    serverConfigs.sin_family = AF_INET;
    serverConfigs.sin_addr.s_addr = inet_addr(srvrIp.c_str());
    serverConfigs.sin_port = htons(srvrPort);

//    Do connection
    if (connect(connSocket, (struct sockaddr *) &serverConfigs, sizeof(serverConfigs)) == -1) {
        cout << "ERROR: CAN'T CONNECT" << endl;
        return 1;
    }
    cout << "SOCKET CONNECTION OK" << endl;

//     Prepare client data
    Payload initialPayload;
    initialPayload.set_sender(clientName);
    initialPayload.set_ip(LOCAL_IP);
    initialPayload.set_flag(Payload_PayloadFlag::Payload_PayloadFlag_register_);


    string msgBinary;
    initialPayload.SerializeToString(&msgBinary);
    char wBuffer[BUFFER_SIZE + MAX_USERNAME_LENGTH];
    strcpy(wBuffer, msgBinary.c_str());

//     Send client data
    send(connSocket, wBuffer, strlen(wBuffer), 0);

    cout << "SERVER CONNECTION OK" << endl;

//     Start Message I/O threads

    int senderThreadCreateFlag = pthread_create(&sendMessageThread, nullptr, &sendMessageThreadFn, nullptr);
    int getThreadCreateFlag = pthread_create(&getMessageThread, nullptr, &getMessageThreadFn, nullptr);

    if (senderThreadCreateFlag != 0 || getThreadCreateFlag != 0) {
        cout << "ERROR: FAILED TO CREATE I/O MESSAGE THREADS" << endl;
        return 1;
    }

    while (!exitFLag.load()) {}
    pthread_join(sendMessageThread, nullptr);
    pthread_join(getMessageThread, nullptr);
    cout << "I/O MESSAGE THREADS ENDED" << endl;
    close(connSocket);
    cout << "SERVER CONNECTION ENDED" << endl;

    return 0;
}

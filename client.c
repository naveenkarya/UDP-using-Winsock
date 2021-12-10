#include <stdio.h>
#include <winsock2.h>

#pragma comment(lib, "Ws2_32.lib")

#define SELF_PORT 8004
#define SELF_ADDRESS "127.0.0.1"
#define SERVER_PORT 8005
#define SERVER_ADDRESS "127.0.0.1"
#define BUFFER_LENGTH 512
#define PACKET_TYPE_DATA 65521 //0xFFF1
#define PACKET_TYPE_ACK 65522 //0xFFF2
#define PACKET_TYPE_REJECT 65523 //0xFFF3
#define CLIENT_ID 200 //0xC8
#define MAX_SEND_ATTEMPTS 4
#define SUCCESS 0
#define TIMEOUT_ERROR 120
#define ACK_TIMEOUT_SECONDS 3
const char *subCodes[4] = {"Packet Out of Sequence", "Payload length mismatch", "End of packet not found",
                           "Duplicate Packet"};

/**
* Build address for the server where the packets need to be sent
*/
struct sockaddr_in getServerAddress();

/**
 * Winsock specific boilerplate code for setup
 */
void setupWSA(WSADATA *wsaData);

/**
 * Create socket and bind to local address and port
 */
SOCKET createAndBindSocket();

/**
 * This function builds the packet to be sent to the server and then sends it using multiple retries if timeout happens
 */
void sendPacketWithRetry(SOCKET clientSocket, unsigned short segmentNumber, char *payload);

/**
 * This function receives and handles response from the server.
 * Receiving response is non-blocking with a timer
 * Returns code 120 if timeout happens
 * Returns code 0 for Success
 */
int receiveResponse(SOCKET clientSocket);

int main() {
    WSADATA wsaData;
    setupWSA(&wsaData);
    SOCKET clientSocket = createAndBindSocket();
    printf("Client is up and running.\n");
    int segmentNumber = 1;
    while (1) {
        char payload[BUFFER_LENGTH];
        printf("Enter payload to send.\n");
        fgets(payload, BUFFER_LENGTH, stdin);
        sendPacketWithRetry(clientSocket, segmentNumber++, payload);
    }
}

/*
 * This function builds the packet to be sent to the server and then sends it using multiple retries if timeout happens
 */
void sendPacketWithRetry(SOCKET clientSocket, unsigned short segmentNumber, char *payloadBuffer) {
    int payloadLength = strlen(payloadBuffer);
    unsigned char sendBuffer[BUFFER_LENGTH];
    // Pre-clear the buffer
    memset(sendBuffer, '\0', BUFFER_LENGTH);
    unsigned short startOrEndPacketId = -1;
    memcpy(sendBuffer, &startOrEndPacketId, sizeof(unsigned short));
    sendBuffer[2] = (unsigned char) CLIENT_ID;
    unsigned short packetType = (unsigned short) PACKET_TYPE_DATA;
    memcpy(sendBuffer + 3, &packetType, sizeof(unsigned short));
    sendBuffer[5] = (unsigned char) segmentNumber;
    sendBuffer[6] = (unsigned char) payloadLength;
    // Add payload to the packet
    memcpy(sendBuffer + 7, payloadBuffer, payloadLength);
    // Simulation for end of packet missing
    memcpy(sendBuffer + 7 + payloadLength, &startOrEndPacketId, sizeof(unsigned short));
    struct sockaddr_in serverAddress = getServerAddress();
    int numberOfAttempts = 1;
    int responseStatus = 0;
    // Keep sending the packet until maximum attempts are exhausted.
    while (numberOfAttempts <= MAX_SEND_ATTEMPTS) {
        printf("Payload %d. Attempt # %d.\n", segmentNumber, numberOfAttempts);
        int sendStatus = sendto(clientSocket,
                                sendBuffer, BUFFER_LENGTH, 0, (SOCKADDR *) &serverAddress, sizeof(serverAddress));
        if (sendStatus == SOCKET_ERROR) {
            printf("sendto failed with error: %d\n", WSAGetLastError());
            closesocket(clientSocket);
            WSACleanup();
        }
        responseStatus = receiveResponse(clientSocket);
        if (responseStatus != TIMEOUT_ERROR) {
            break;
        }
        numberOfAttempts++;
    }
    // Check for timeout
    if (responseStatus == TIMEOUT_ERROR) {
        printf("Server did not respond for sequence number: %d\n", segmentNumber);
    }
}

/* This function receives and handles response from the server.
 * Receiving response is non-blocking with a timer
 * Returns code 120 if timeout happens
 * Returns code 0 for Success
 */
int receiveResponse(SOCKET clientSocket) {
    fd_set fds;
    int status;
    // Setup Timeout
    struct timeval timeout;
    timeout.tv_sec = ACK_TIMEOUT_SECONDS;
    timeout.tv_usec = 0;
    // file descriptor for client socket
    FD_ZERO(&fds);
    FD_SET(clientSocket, &fds);
    unsigned char receiverBuffer[BUFFER_LENGTH];
    memset(receiverBuffer, '\0', BUFFER_LENGTH);
    struct sockaddr_in senderAddr;
    int SenderAddrSize = sizeof(senderAddr);

    // Check the status of socket to see if packet is received, with a timeout specified
    status = select(clientSocket, &fds, NULL, NULL, &timeout);
    // if status is zero, timeout happened
    if (status == 0) return TIMEOUT_ERROR;
    else if (status == SOCKET_ERROR) return SOCKET_ERROR;
    // Receive response from the server
    int receivedLength = recvfrom(clientSocket, receiverBuffer, BUFFER_LENGTH, 0, (SOCKADDR *) &senderAddr,
                                  &SenderAddrSize);
    if (receivedLength == SOCKET_ERROR) {
        printf("receive failed with error code : %d", WSAGetLastError());
        return WSAGetLastError();
    }
    unsigned short startOfPacket = (receiverBuffer[1] << 8) | receiverBuffer[0];
    unsigned char clientId = receiverBuffer[2];
    unsigned short packetType = (receiverBuffer[4] << 8) | receiverBuffer[3];
    unsigned short segmentNumber;
    unsigned short subCode;
    if (packetType == PACKET_TYPE_ACK) {
        segmentNumber = receiverBuffer[5];
        unsigned short endOfPacket = (receiverBuffer[7] << 8) | receiverBuffer[6];
        printf("ACK from the server: startOfPacket: %#02X, clientId: %d, packet type: %#X, \nsequence number: %d, endOfPacket: %#X\n",
               startOfPacket, clientId, packetType, segmentNumber, endOfPacket);
    } else if (packetType == PACKET_TYPE_REJECT) {
        subCode = (receiverBuffer[6] << 8) | receiverBuffer[5];
        segmentNumber = receiverBuffer[7];
        unsigned short endOfPacket = (receiverBuffer[9] << 8) | receiverBuffer[8];
        printf("Reject from the server: startOfPacket: %#02X, clientId: %d, packet type: %#X, \nsub code: %#X, sequence number: %d, endOfPacket: %#X\n",
               startOfPacket, clientId, packetType, subCode, segmentNumber, endOfPacket);
        printf("Packet with sequence number %d was rejected by the server with sub-code: %#X - %s\n", segmentNumber,
               subCode, subCodes[subCode - 65524]);
    }
    return SUCCESS;
}

// Create socket and bind to local address and port
SOCKET createAndBindSocket() {
    SOCKET clientSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (clientSocket == INVALID_SOCKET) {
        printf("Error creating socket: %d.\n", WSAGetLastError());
    }
    struct sockaddr_in selfAddress;
    selfAddress.sin_family = AF_INET;
    selfAddress.sin_addr.s_addr = inet_addr(SELF_ADDRESS);
    selfAddress.sin_port = SELF_PORT;
    // Binding socket to local address and port
    int bindStatus = bind(clientSocket, (SOCKADDR *) &selfAddress, sizeof(selfAddress));
    if (bindStatus != 0) {
        printf("Bind failed with error: %d.\n", WSAGetLastError());
        closesocket(clientSocket);
        WSACleanup();
    }
    return clientSocket;
}

// Build address for the server where the packets need to be sent
struct sockaddr_in getServerAddress() {
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_ADDRESS);
    serverAddr.sin_port = htons(SERVER_PORT);
    return serverAddr;
}

// Winsock specific boilerplate code for setup
void setupWSA(WSADATA *wsaData) {
    int code = WSAStartup(MAKEWORD(2, 2), wsaData);
    if (code != 0) {
        printf("WSAStartup failed with error code: %d.\n", code);
        WSACleanup();
    }
}

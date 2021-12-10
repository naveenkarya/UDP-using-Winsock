#include <stdio.h>
#include <winsock2.h>
#include <windows.h>

#pragma comment(lib, "Ws2_32.lib")

#define SELF_PORT 8005
#define SELF_ADDRESS "127.0.0.1"
#define BUFFER_LENGTH 512
#define PACKET_TYPE_ACK 65522 //0xFFF2
#define PACKET_TYPE_REJECT 65523 //0xFFF3
#define REJECT_OUT_OF_SEQ 65524 //0xFFF4
#define REJECT_LENGTH_MISMATCH 65525 //0xFFF5
#define REJECT_END_OF_PACKET_MISSING 65526 //0xFFF6
#define REJECT_DUPLICATE_PACKET 65527 //0xFFF7

/**
 * Winsock specific boilerplate code for setup
 */
void setupWSA(WSADATA *wsaData);

/**
 * Create socket and bind to local address and port
 */
SOCKET createAndBindSocket();

/**
 * This function receives the packet from the client and processes it.
 */
void receiveMessage(SOCKET serverSocket, unsigned short *lastSequenceNumber);

/**
 * Function that takes clientId, segmentNumber, responseCode and subCode to build the response and then
 * reply it back to the client that sent the request.
 */
void replyBack(SOCKET serverSocket, struct sockaddr_in *senderAddr, unsigned char clientId, unsigned char segmentId,
               unsigned short responseCode, unsigned short responseSubCode);

int main() {
    // Initialize WinSock
    WSADATA wsaData;
    setupWSA(&wsaData);

    // Create a socket and bind to local address and port
    SOCKET serverSocket = createAndBindSocket();
    printf("Server is up and running.\n");
    // Counter to keep track of sequence numbers already received
    unsigned short lastSequenceNumber = 0;
    // Keep the server running to receive messages
    while (1) {
        receiveMessage(serverSocket, &lastSequenceNumber);
        printf("\n");
    }
}

// This function receives a message from the client one at a time using recvFrom() blocking call.
void receiveMessage(SOCKET serverSocket, unsigned short *lastSequenceNumber) {
    // Initialize default Packet type and responseSubCode
    unsigned short responseCode = PACKET_TYPE_ACK;
    unsigned short responseSubCode = 0;
    printf("Last Successful sequence number received: %d\n", *lastSequenceNumber);
    unsigned char receiverBuffer[BUFFER_LENGTH];
    memset(receiverBuffer, '\0', BUFFER_LENGTH);
    struct sockaddr_in senderAddr;
    int SenderAddrSize = sizeof(senderAddr);
    // Receive packet from the client
    int receivedLength = recvfrom(serverSocket, receiverBuffer, BUFFER_LENGTH, 0, (SOCKADDR *) &senderAddr,
                                  &SenderAddrSize);
    if (receivedLength == SOCKET_ERROR) {
        printf("receive failed with error responseCode : %d", WSAGetLastError());
        return;
    }

    // Getting information out of packet like client id, sequence number and payload length
    // Get start of packet from the first 2 bytes
    unsigned short startOfPacket = (receiverBuffer[1] << 8) | receiverBuffer[0];
    unsigned char clientId = receiverBuffer[2];
    // Get packet type from 4th and 5th byte
    unsigned short packetType = (receiverBuffer[4] << 8) | receiverBuffer[3];
    unsigned short sequenceNumber = receiverBuffer[5];
    unsigned short payloadLength = receiverBuffer[6];
    printf("Packet from client: startOfPacket: %#02X, clientId: %d, packet type: %#X, sequence number: %d, payloadLength: %d, \n",
           startOfPacket, clientId, packetType, sequenceNumber, payloadLength);
    // If sequence number is greater than the expected sequence number, it is out of sequence
    if (sequenceNumber > *lastSequenceNumber + 1) {
        responseCode = PACKET_TYPE_REJECT;
        responseSubCode = REJECT_OUT_OF_SEQ;
    }
    // If sequence number is less than the expected sequence number, it is already received and duplicate
    else if (sequenceNumber < *lastSequenceNumber + 1) {
        responseCode = PACKET_TYPE_REJECT;
        responseSubCode = REJECT_DUPLICATE_PACKET;
    }
    // Processing correct sequence number
    else {
        // 0xFFFF = -1
        unsigned short endOfPacket = -1;
        // This variable keeps track of the actual payload length
        int countPayloadBytes = 0;
        // endFound = 1 is end of packet id is found. Else it will remain 0.
        short endFound = 0;
        printf("payload content: ");
        for (unsigned short i = 7; i < BUFFER_LENGTH; i++) {
            // Check if end of packet found by matching 2 bytes with 0xFFFF (-1)
            if (memcmp(receiverBuffer + i, &endOfPacket, 2) == 0) {
                endFound = 1;
                break;
            }
            countPayloadBytes++;
            printf("%c", (char) receiverBuffer[i]);
        }
        // Check if end of packet was missing
        if (endFound == 0) {
            printf("\nEnd of packet is missing");
            responseCode = PACKET_TYPE_REJECT;
            responseSubCode = REJECT_END_OF_PACKET_MISSING;
        }
        // Check if payload length matches with the actual length of payload
        else if (countPayloadBytes != payloadLength) {
            printf("Payload length mismatch. Length in header: %d. Actual Length: %d\n", payloadLength,
                   countPayloadBytes);
            responseCode = PACKET_TYPE_REJECT;
            responseSubCode = REJECT_LENGTH_MISMATCH;
        }
        printf("\n");
    }
    if (responseSubCode == REJECT_OUT_OF_SEQ) {
        printf("Packet with sequence number %d is out of sequence. Expecting sequence number %d.\n", sequenceNumber,
               *lastSequenceNumber + 1);
    }
    if (responseSubCode == REJECT_DUPLICATE_PACKET) {
        printf("Packet received with Sequence number %d is duplicate.\n", sequenceNumber);
    }
    // If packet is successfully received, increment the last successful sequence number received
    if (responseCode == PACKET_TYPE_ACK) *lastSequenceNumber = *lastSequenceNumber + 1;
    // Reply back to the client.
    replyBack(serverSocket, &senderAddr, clientId, sequenceNumber, responseCode, responseSubCode);

}

// This function replies back to the client using the responseCode and responseSubCode. responseSubCode 0 is ignored.
void replyBack(SOCKET serverSocket, struct sockaddr_in *senderAddr, unsigned char clientId, unsigned char segmentNumber,
               unsigned short responseCode, unsigned short responseSubCode) {
    // Build response packet start
    int bufferLength = 8;
    unsigned char sendBuffer[10];
    memset(sendBuffer, '\0', 10);
    unsigned short startOrEndPacketId = -1;
    memcpy(sendBuffer, &startOrEndPacketId, sizeof(unsigned short));

    // Copy clientId that was received from the client
    sendBuffer[2] = (unsigned char) clientId;
    memcpy(sendBuffer + 3, &responseCode, sizeof(unsigned short));

    // responseSubCode 0 means ACK scenario. For ACK scenario, responseSubCode will not be part of the packet
    if (responseSubCode == 0) {
        sendBuffer[5] = (unsigned char) segmentNumber;
        memcpy(sendBuffer + 6, &startOrEndPacketId, sizeof(unsigned short));
    }
        // Handle Reject scenario. Need to add responseSubCode in the response packet
    else {
        bufferLength = 10;
        memcpy(sendBuffer + 5, &responseSubCode, sizeof(unsigned short));
        sendBuffer[7] = (unsigned char) segmentNumber;
        memcpy(sendBuffer + 8, &startOrEndPacketId, sizeof(unsigned short));
    }
    // Build response packet end
    // Send response packet to client
    int sendStatus = sendto(serverSocket,
                            sendBuffer, bufferLength, 0, (SOCKADDR *) senderAddr, sizeof((*senderAddr)));
    if (sendStatus == SOCKET_ERROR) {
        printf("sendto failed with error: %d\n", WSAGetLastError());
        closesocket(serverSocket);
        WSACleanup();
    }
}

// Create socket and bind to local address and port
SOCKET createAndBindSocket() {
    SOCKET serverSocket;
    serverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (serverSocket == INVALID_SOCKET) {
        printf("Error creating socket: %d.\n", WSAGetLastError());
    }
    struct sockaddr_in selfAddr;
    selfAddr.sin_family = AF_INET;
    selfAddr.sin_addr.s_addr = inet_addr(SELF_ADDRESS);
    selfAddr.sin_port = htons(SELF_PORT);
    // Binding socket to local address and port
    int bindStatus = bind(serverSocket, (SOCKADDR *) &selfAddr, sizeof(selfAddr));
    if (bindStatus != 0) {
        printf("Bind failed with error: %d.\n", WSAGetLastError());
        closesocket(serverSocket);
        WSACleanup();
    }
    return serverSocket;
}

// Winsock specific boilerplate code for setup
void setupWSA(WSADATA *wsaData) {
    int code = WSAStartup(MAKEWORD(2, 2), wsaData);
    if (code != 0) {
        printf("WSAStartup failed with error code: %d.\n", code);
        WSACleanup();
    }
}

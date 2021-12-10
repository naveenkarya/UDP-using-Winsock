# UDP-using-Winsock

This is a demo application to simulate UDP packets between the client and server application.  

## How to execute  
**Prerequisite:** This can be run only on a windows machine, with gcc compiler installed and configured in environment PATH variable.

Please follow below steps to run these programs:

1. Open command prompt and go to the folder where these files are present.

2. client.c represents the client application. Compile it using this command:
gcc -o client.exe client.c -lws2_32

3. server.c represents the server application. Compile it using this command:
gcc -o server.exe server.c -lws2_32

Above 2 steps will generate client.exe and server.exe files in the same folder.

4. Double click on server.exe first to start the server.

5. Then double click on client.exe to start the client.

6. In order to run again, close both client and server applications and then follow above steps.

## Request/Response

### Request packet format

| Bytes      | 2 | 1      | 2 | 1      | 1 | 255 | 2 |
|    :----:   |   :----:    |    :----:   |   :----:  |    :----:   |   :----:    |    :----:   |   :----:    |  
|       | Start of packet  Id | clientID | Packet type (Data) | Segment No. | Length | Payload | End of packet Id  |

Start of Packet identifier: 0XFFFF  
End of Packet identifier: 0XFFFF  
Client Id: Maximum 0XFF (255 Decimal)  
Length: Maximum 0XFF (255 Decimal)  
  
### Packet Types:  
DATA: 0XFFF1  
ACK (Acknowledge): 0XFFF2  
REJECT: 0XFFF3  

### Acknowledgement packet format
| Bytes      | 2 | 1      | 2 | 1      | 2 |
|    :----:   |   :----:    |    :----:   |   :----:  |    :----:   |   :----:    |
|       | Start of packet Id | clientID | Packet type (ACK) | Received Segment No. | End of packet Id |

### Reject packet Format
| Bytes      | 2 | 1      | 2 | 2      | 1 | 2 |
|    :----:   |   :----:    |    :----:   |   :----:  |    :----:   |   :----:    |    :----:   | 
|       | Start of packet Id | clientID  | Packet type (Reject) | Reject Sub-code | Received Segment No. |  End of packet Id |
  
### Reject sub codes:
Packet out of sequence: 0XFFF4  
Length mismatch: 0XFFF5  
End of packet missing: 0XFFF6  
Duplicate packet: 0XFFF7  

/*
Author: Sammy EL-Sherif
*/

#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <math.h>

#define STDIN 0
#define MAXKV 30
#define MAXMSG 999
#define MAX_PARTNERS 99
#define VERSION 8
#define TTL 3
#define RESEND 2

// #define DEBUG 1 /* Below is an my 'if-defined' macro, comment it in/out to get debug statements printed */
/* #ifdef DEBUG
#endif */

struct kv
{
    char key[100];
    char value[100];
};

struct messages
{
    struct kv pair[MAXKV];
    int kvCount;
    int typeAckFlag;
    int typeMoveFlag;
    int resendCount;
    int init;
};

struct drones
{
    int seqNumber;
    char ip_address[100], port[100], location[100];
    int mostRecentAck, lastSentSeq, partnerAmount;
};

/* -------------- SERVER SETUP -------------- */
int verifyPortNumber(char portNumberArg[]);
struct sockaddr_in setupAddressAndBind(int portNumber, int sd);
int argValidityCheck(int argc, char ip[], char portNum[]);
struct sockaddr_in serverAddressConfig(char serverIpArg[], int portNumber);

/* -------------- RCV MSG -------------- */
char *recieveMsg(char *bufferReceived, int sd);
char *encodeMessage(char *bufferReceived);
int parseMessage(struct messages *messages, struct messages *msg, char *bufferReceived, int messageCount, char *myPortNumber);
char *parseBufferGivenKey(char *bufferReceived, char *givenKey, char outputBuffer[]);
void displayMessage(struct messages *msg, char *cliPort);

/* -------------- SEND MSG -------------- */
void sendMessage(struct sockaddr_in server_address, int sd, char bufferOut[200]);
void sendMessagesToServers(struct drones *partners, int sd, int argc, char msgToSend[200], char *cliPort, char *myLocation);
void forwardMessagesToServers(struct drones *partners, int sd, int argc, struct messages *msg, char *cliPort, char *myLocation, int ackFlag);
char *msgFromStructToStringFWD(struct messages *msg, char messageToSend[], char *myLocation, char *cliPort);
char *msgFromStructToStringResend(struct messages *msg, char messageToSend[], char *myLocation, char *cliPort);

/* MESSAGE STRUCT FUNCTIONS (Message Storage CRUD) */
char *findValueFromKey(struct messages *msg, char keyToFind[]);
void updateValueGivenKeyAndNewValue(struct messages *msg, char *key, int newValue);
void updateMyLocation(struct drones *partners, char *myPortNumber, char newLocation[]);
void messageStorageInit(struct messages *msg);
void reSend(struct messages *msg, int sd, struct drones *partners, char myLocation[], char myPortNumber[]);
void deleteMessage(struct messages *msg, int messageNumber);

/* BUFFERED MESSAGE FUNCTIONS (String Manipulation) */
char *addKvPairToMsg(char *key, char *value, char *msg);
char *addKvPairToMsgSpaceAfter(char *key, char *value, char *msg);
char *msgFromStructToString(struct messages *msg, char messageToSend[]);

/* GRID AWARENESS FUNCTIONS */
int euclideanDistance(int location, int myLocation, int rows, int cols);

/* ACKNLOWEDGMENT FUNCTIONS */
void broadcastAcknowledgment(int sd, int portNumber, char msgToSend[200], struct messages *msg, char *cliPort, char *myLocation);
char *msgFromStructToStringACK(struct messages *msg, char messageToSend[]);

/* DRONES STRUCT FUNCTIONS */
char *droneStructInit(struct drones *partners, char *myPortNumber, char myLocation[]);
char *findPartnerLocationGivenPort(struct drones *partners, char *port, char outputBuffer[]);
int incrementMraForDrone(struct drones *partners, char location[]);
int findPartnerGivenPort(struct drones *partners, char port[]);
void displayDroneConfig(struct drones *partners, int numPartners);
int duplicateMessageCheck(struct messages *messages, char *seqNumber, char *toPort, char *fromPort);

int main(int argc, char *argv[])
{
    /* INITIALIZATION */
    struct messages msg[MAXMSG];
    struct drones partners[MAX_PARTNERS];
    messageStorageInit(msg);
    char *myPortNumber = argv[1]; /* portNumber will always be the key for the drone */
    char locHelper[200];
    char locationBuffer[200];
    char *myLocation = droneStructInit(partners, myPortNumber, locHelper);
    printf("This drone is at location: %s (port: %s)\n", myLocation, myPortNumber);

    /* SERVER MESSAGING VARS */
    int sd;                    /* socket descriptor */
    int rc;                    /* return code from recvfrom */
    int portNumber;            /* get this from command line */
    int msgCount = -1;         /* used for message differentiation */
    char bufferReceived[1000]; /* used in recvfrom */

    /* INPUT ERROR CHECK */
    if (argc < 4)
    {
        printf("usage is: drone <portnumber> <row> <col>\n");
        exit(1);
    }

    /* DEFINE GRID DIMENSIONS */
    int rows = atoi(argv[2]);
    int cols = atoi(argv[3]);
    int gridMN = rows * cols;

    /* Create a socket, specifying the AF and Type, which we will later assign our server address to */
    sd = socket(AF_INET, SOCK_DGRAM, 0); /* create a socket */

    /* ERROR CHECK:
      - "On success, a file descriptor for the new socket is returned. On error, -1 is returned, and errno is set to indicate the error." */
    if (sd == -1)
    {
        perror("socket");
        exit(1);
    }

    /* Check port and format port number to int for message filtering */
    portNumber = verifyPortNumber(argv[1]);

    /* Fill in address struct and bind socket descriptor to set up address */
    setupAddressAndBind(portNumber, sd);

    /* For the select: allows us to send msg's from cli and recieve incoming messages */
    fd_set socketFDS; // the socket descriptor set
    int maxSD;        // tells the OS how many sockets are set
    FD_ZERO(&socketFDS);
    struct timeval timeout;

    while (1)
    {
        timeout.tv_sec = 20; /* TODO: change before testing */
        timeout.tv_usec = 0;
        /* Select Setup */
        FD_SET(sd, &socketFDS);    // sets the bit for the initial sd socket
        FD_SET(STDIN, &socketFDS); // tell it you will look at STDIN too
        if (STDIN > sd)
        { // figure out what the max sd is. biggest number
            maxSD = STDIN;
        }
        else
        {
            maxSD = sd;
        }
        rc = select(maxSD + 1, &socketFDS, NULL, NULL, &timeout); // block until something arrives or it will timeout -> check return code
        /* printf("select popped rc is %d\n", rc); */
        if (rc == -1)
        {
            perror("select");
            exit(1);
        }
        else if (rc == 0) /* TIMEOUT CASE */
        {
            printf("timeout!\n");
            reSend(msg, sd, partners, myLocation, myPortNumber);
            continue;
        }

        /* User entered message from command line, broadcast message to drone network */
        if (FD_ISSET(STDIN, &socketFDS))
        {
            memset(bufferReceived, '\0', 100);
            fgets(bufferReceived, sizeof(bufferReceived), stdin);                                // user input -> buffer
            bufferReceived[strlen(bufferReceived) - 1] = 0;                                      // get rid of \n
            sendMessagesToServers(partners, sd, argc, bufferReceived, myPortNumber, myLocation); // send user input message to all server
        }
        /* Message Recieved from the Network */
        if (FD_ISSET(sd, &socketFDS))
        {
            msgCount++; // used for message differentiation & storage
            if (msgCount == MAXMSG)
                msgCount = 0;
            /* printf("Message #%d Recieved!\n", msgCount) */;

            /* recieve a message, pass to parseMessage for tokenization and storage. Returns the location of message recieved for euclideanDistance() */
            int location = parseMessage(msg, &msg[msgCount], recieveMsg(bufferReceived, sd), msgCount, myPortNumber);
            /* If location is -1, that means that the message was deleted */
            if (location == -1)
            {
                continue;
            }

            /* Calculate euclidean distance between msg recieved and this drone for message filtering */
            int distance = euclideanDistance(location, atoi(myLocation), rows, cols);
            /* [MESSAGE FILTERING] When an ACK message is recieved, it is only displayed upon the following conditions:
                - The message's toPort == portNumber of this drone
                - The message's version == VERSION of this drone
                - The message's TTL > 0
                - The ack flag == 1 */

            /* displayMessage(&msg[msgCount], myLocation);
            printf("%d\n", distance);
            printf("%s\n", myLocation); */

            if (msg[msgCount].typeAckFlag == 1 && atoi(findValueFromKey(&msg[msgCount], "version")) == VERSION && atoi(findValueFromKey(&msg[msgCount], "TTL")) > 0)
            {
                if (distance <= 2 && distance >= 0)
                {
                    /* ACK FOUND ITS DESTINATION */
                    if (atoi(findValueFromKey(&msg[msgCount], "toPort")) == portNumber)
                    {
                        /* ACK CASE */
                        if (strcmp(findValueFromKey(&msg[msgCount], "type"), "ACK") == 0)
                        {
                            char *seqNumPrint = findValueFromKey(&msg[msgCount], "seqNumber");
                            char *fromPortPrint = findValueFromKey(&msg[msgCount], "fromPort");
                            int partnerNumber = findPartnerGivenPort(partners, fromPortPrint);

                            /* if this partners last seq number is equal to the most recent ack, it is a duplicate ack */
                            if (partners[partnerNumber].mostRecentAck == partners[partnerNumber].lastSentSeq)
                            {
                                char *sendPathCheck = findValueFromKey(&msg[msgCount], "send-path");
                                printf("DUPLICATE ACK: send-path is '%s'\n", sendPathCheck);
                            }
                            else if (partners[partnerNumber].mostRecentAck < partners[partnerNumber].lastSentSeq)
                            {
                                partners[partnerNumber].mostRecentAck = atoi(seqNumPrint);
                                printf("\nreceived an ACK for SeqNum %s fromPort %s\n", seqNumPrint, fromPortPrint);
                                displayMessage(&msg[msgCount], myLocation);
                            }
                        }
                    }
                    /* ACK NEEDS FORWARDING */
                    else
                    {
                        printf("[FORWARDING-ACK] ");
                        forwardMessagesToServers(partners, sd, argc, &msg[msgCount], myPortNumber, myLocation, 0); // send user input message to all server
                    }
                }
                else
                {
                    /* printf("DISTANCE > 2: %d\n", distance); */
                }
            }

            /* [MESSAGE FILTERING] When a standard message is recieved, it is only displayed upon the following conditions:
                - The message's toPort == portNumber of this drone
                - The message's fromPort != portNumber of this drone (filtering messages sent to itself out)
                - The message's version == VERSION of this drone
                - The message's TTL > 0
                - The ack flag == 0
            */
            if (msg[msgCount].typeAckFlag == 0 && atoi(findValueFromKey(&msg[msgCount], "fromPort")) != portNumber && atoi(findValueFromKey(&msg[msgCount], "version")) == VERSION && atoi(findValueFromKey(&msg[msgCount], "TTL")) > 0)
            {
                /* Move message case */
                if (msg[msgCount].typeMoveFlag == 1 && atoi(findValueFromKey(&msg[msgCount], "toPort")) == portNumber)
                {
                    // change my location to the value of move:x
                    char *moveLocation = findValueFromKey(&msg[msgCount], "move");
                    printf("[MOVE] Changing my location to %s\n", moveLocation);
                    updateMyLocation(partners, myPortNumber, moveLocation);
                    strcpy(locationBuffer, moveLocation);
                    myLocation = locationBuffer;
                    /* displayDroneConfig(partners, 3); */

                    reSend(msg, sd, partners, myLocation, myPortNumber);
                }
                else
                {
                    /* Message location is within grid bounds */
                    if (location <= gridMN && location >= 0)
                    {
                        /* euclidean distance between sender and reciever is not greater than 2 */
                        if (distance <= 2 && distance >= 0)
                        {
                            /* Message was destined for me since its destination port == my port number */
                            if (atoi(findValueFromKey(&msg[msgCount], "toPort")) == portNumber)
                            {
                                printf("[SUCCESS] Message made it to its destination!!!\n");
                                /* Respond to the sender drone with a acknowledgment message (ACK) */
                                char *recvMsgSeqNumber = findValueFromKey(&msg[msgCount], "seqNumber");
                                char *recvMsgFromPort = findValueFromKey(&msg[msgCount], "fromPort");
                                int partnerNumber = findPartnerGivenPort(partners, recvMsgFromPort);
                                printf("\nsending an ack for seqNum %s partner # %d, fromPort %s\n", recvMsgSeqNumber, partnerNumber, recvMsgFromPort);

                                displayMessage(&msg[msgCount], myLocation);

                                /* Create ACK message and start forwarding process */
                                char ackMsg[6000];
                                memset(ackMsg, 0, 6000);
                                broadcastAcknowledgment(sd, portNumber, ackMsg, &msg[msgCount], myPortNumber, myLocation);
                            }
                            else
                            {
                                /* FORWARD ALONG */
                                printf("[FORWARDING] ");
                                forwardMessagesToServers(partners, sd, argc, &msg[msgCount], myPortNumber, myLocation, 0); // send user input message to all server
                            }
                        }
                        else
                        {
                            /* printf("DISTANCE > 2: %d\n", distance); */
                        }
                    }
                    else
                    {
                        // printf("!!! NOT IN GRID !!!\n");
                    }
                }
            }
        }
    }
}

/* -------------- SERVER SETUP -------------- */
int verifyPortNumber(char portNumberArg[])
{
    int portNumber; // get this from command line
    int i;
    for (i = 0; i < strlen(portNumberArg); i++) // loop through the number of digits in port number
    {
        if (!isdigit(portNumberArg[i])) /* The isdigit() function checks whether a character is numeric character (0-9) or not. */
        {
            printf("The Portnumber isn't a number!\n");
            exit(1);
        }
    }

    // setting portnumber to the port num passed in, in base 10 format
    portNumber = strtol(portNumberArg, NULL, 10); /* many ways to do this */

    /* Port numbers range from 0 to 65536, but only ports numbers 0 to 1024 are
    reserved for privileged services and designated as well-known ports. */
    if ((portNumber > 65535) || (portNumber < 0))
    {
        printf("you entered an invalid port number (must be between 0 - 65535)\n");
        exit(1);
    }
    return portNumber;
}

struct sockaddr_in setupAddressAndBind(int portNumber, int sd)
{
    struct sockaddr_in server_address; /* my address */
    int rc;                            // always need to check return codes!

    /* now fill in the address data structure we use to sendto the server */
    server_address.sin_family = AF_INET;         /* use AF_INET addresses */
    server_address.sin_port = htons(portNumber); /* convert port number */
    server_address.sin_addr.s_addr = INADDR_ANY; /* any adapter */

    /* BIND TO ADDRESS:
    - Now that our server_address struct is filled in, we can assign the
    servers address to the socket descriptor of the socket we created */
    rc = bind(sd, (struct sockaddr *)&server_address,
              sizeof(struct sockaddr));

    if (rc < 0)
    {
        perror("bind");
        exit(1);
    }
    return server_address;
}

/* -------------- CLIENT SETUP -------------- */
int argValidityCheck(int argc, char ip[], char portNum[])
{
    int portNumber;
    int i;                     /* loop variable */
    struct sockaddr_in inaddr; /* structures for checking addresses */

    /* check to see if the right number of parameters was entered */
    if (argc > 4)
    {
        printf("usage is ./drone <portnumber> <row> <col>\n");
        exit(1); /* just leave if wrong number entered */
    }

    /* this code checks to see if the ip address is a valid ip address */
    /* meaning it is in dotted notation and has valid numbers          */
    if (!inet_pton(AF_INET, ip, &inaddr))
    {
        printf("error, bad ip address\n");
        exit(1); /* just leave if is incorrect */
    }

    /* check that the port number is a number..... */
    for (i = 0; i < strlen(portNum); i++)
    {
        if (!isdigit(portNum[i]))
        {
            printf("The Portnumber isn't a number!\n");
            exit(1);
        }
    }

    /* Using strtol to convert our port number to base 10 */
    portNumber = strtol(portNum, NULL, 10); /* many ways to do this */

    /* exit if a portnumber too big or too small  */
    /* Port numbers range from 0 to 65536, but only ports numbers 0 to 1024 are reserved for privileged services and designated as well-known ports. */
    if ((portNumber > 65535) || (portNumber < 0))
    {
        printf("you entered an invalid socket number\n");
        exit(1);
    }
    return portNumber;
}

struct sockaddr_in serverAddressConfig(char serverIpArg[], int portNumber)
{
    struct sockaddr_in server_address; /* structures for addresses */
    char serverIP[20];                 // provided by the user on the command line
    /* char *strcpy(char *dest, const char *src)
    Copying the second arguement passed in from terminal command into serverIP var */
    strcpy(serverIP, serverIpArg); /* copy the ip address */

    /* Addresses for AF_INET sockets are IP addresses and port numbers */
    server_address.sin_family = AF_INET;                  /* use AF_INET addresses */
    server_address.sin_port = htons(portNumber);          /* convert port number */
    server_address.sin_addr.s_addr = inet_addr(serverIP); /* convert IP addr */
    /* The htons() function converts the unsigned short integer hostshort from host byte order to network byte order. */
    return server_address;
}

/* -------------- RECIEVE MESSAGES -------------- */
char *recieveMsg(char *bufferReceived, int sd)
{
    struct sockaddr_in from_address; /* address of sender */
    socklen_t fromLength;
    int rc;        // always check err codes
    int flags = 0; // used in recvfrom

    memset(bufferReceived, 0, 1000); // zero out the buffers in C

    /* NOTE - you MUST MUST MUST give fromLength an initial value */
    fromLength = sizeof(struct sockaddr_in);

    /* By default, Recvfrom() is blocking: when a process issues a Recvfrom()
     that cannot be completed immediately (because there is no packet),
     the process is put to sleep waiting for a packet to arrive at the socket. */
    rc = recvfrom(sd, bufferReceived, 1000, flags, (struct sockaddr *)&from_address, &fromLength);

    /* check for any possible errors */
    if (rc <= 0)
    {
        perror("recvfrom");
        printf("leaving, due to socket error on recvfrom\n");
        exit(1);
    }
    /* printf("received '%s'\n", bufferReceived); */
    return encodeMessage(bufferReceived);
}

char *encodeMessage(char *bufferReceived)
{
    char quote = '\"';
    char *ptr = strchr(bufferReceived, quote); // returns a ptr to the first time " appears in the string

    if (ptr)
    {
        int where = ptr - bufferReceived;
        where++;

        while (bufferReceived[where] != '\"') // lopp while not the end quote or end of string
        {
            if (bufferReceived[where] == ' ') // if " " replace with "^"
            {
                bufferReceived[where] = '^';
            }
            where++;
        }
    }
    /* printf("ENC: %s\n", bufferReceived); */
    return bufferReceived;
}

int parseMessage(struct messages *messages, struct messages *msg, char *bufferReceived, int messageCount, char *myPortNumber)
{
    /**
     * Message Parsing Fucntion
     * * Important Information
     * TODO: Thing to do
     * @param myParam The parameter for this method
     */
    /* When I get a message I parse it and store it, im currently aware of the message count
    - I need to resend the message until its resendCount is 0
    - I can make a resend array that keeps a queue of message to resend
    */
    /* printf("BUFFRECV:'%s'\n", bufferReceived); */
    /* printf("Msg #%d\n", messageCount); */
    msg->kvCount = 0;
    msg->typeAckFlag = 0;
    msg->typeMoveFlag = 0;
    msg->init = 1;
    int count = 0;
    char *token;
    char *key;
    char *val;
    int location = 0;
    int delete = 0;

    char *sn;
    char *tp;
    char *fp;

    /* Parse message current loops through a buffer and tokenizes each kv pair and stores them, but,
    we need to store messages only when:
    - we are the original sender
    - we are not the destination, and msg is not a dup (fromPort, toPort, seqNumber)
    */

    token = strtok(bufferReceived, ":"); // set seperator to colon
    key = token;

    while (token != NULL) // iterates through each kv pair
    {
        if (count != 0)
        {
            token = strtok(NULL, ":"); // set seperator to colon
            key = token;
        }

        token = strtok(NULL, " "); // set seperator back to a space
        val = token;

        if (val != NULL && key != NULL)
        {
            count += 1;
            char *decode = strchr(val, '\"');
            if (decode)
            {
                int i = 1;
                while (val[i] != '\"')
                {
                    if (val[i] == '^')
                    {
                        val[i] = ' ';
                    }
                    i++;
                }
            }
            // at this point we have the key and value, store in struct
            if (strcmp(key, "Location") == 0 || strcmp(key, "location") == 0)
            {
                location = atoi(val);
            }
            /* set message ack flag */
            if (strcmp(key, "type") == 0)
            {
                msg->typeAckFlag = 1;
            }
            if (strcmp(key, "move") == 0)
            {
                msg->typeMoveFlag = 1;
                delete = 1;
            }

            /* maybe when we see the toPort, fromPort, and seq# we could save them in a charPtr, then call a function named msgDeleteConditions
                this function would return a 1 or 0 flag indicating whether or not to delete the message or not, all it needs to check if its a duplicate
                message (loop through all of our messages), and if the toPort is our Port
            */
            // Set delete msg flag if it was destined for me or if we already have a msg with that seq#, toPort, and fromPort
            if (strcmp(key, "toPort") == 0)
            {
                tp = val;
                if (strcmp(myPortNumber, val) == 0)
                {
                    /* Destination Port of message is myPortNumber, delete it! */
                    delete = 1;
                }
            }

            /* The two if statements below are for duplicate checking function */
            if (strcmp(key, "fromPort") == 0)
            {
                fp = val;
            }
            if (strcmp(key, "seqNumber") == 0)
            {
                sn = val;
            }
            /* duplicateMessageCheck(seqNum, tp, fp); */
            strcpy(msg->pair[count].key, key);
            strcpy(msg->pair[count].value, val);
            msg->kvCount += 1;

            /* printf("STORE[%d]: %s:%s\n", msg->kvCount, msg->pair[count].key, msg->pair[count].value); */
        }
    }
    /* Now that I have looped through all of the tokens, I can first check if the the delete flag was set by matching toPort==myPort or if duplicate check returns 1 */
    /* printf("[DUP:%d]sn:%s, tp:%s, fp:%s\n", duplicateMessageCheck(messages, sn, tp, fp), sn, tp, fp); */
    if (delete == 1 || duplicateMessageCheck(messages, sn, tp, fp) == 1)
    {
        /* What if instead of deleting the message, we just set the current messages resend count to 5 */
        msg->resendCount = 0;
    }
    else
    {
        /* printf("!!! STORING THIS MESSAGE !!!\n");
        displayMessage(msg, "TEST"); */
        /* deleteMessage(msg, messageCount); */
        msg->resendCount = RESEND;
    }

    return location;
}

/** Duplicate Message Checker
 * @param messages The struct containing all stored messages
 * @param sn Sequence number of the message being parsed
 * @param tp Destination port of the message being parsed
 * @param fp Origin port of the message being parsed
 * * Loop through all of the messages stored, check if all three msg params (sn, tp, fp) are in each message
 * * If they are all found, return a delete flag of 1 to let the parseMessage function know it needs to delete it
 * ! Return Value: 1 for deletion, 0 for store
 */
int duplicateMessageCheck(struct messages *messages, char *sn, char *tp, char *fp)
{
    int uniqueFlag = 0;

    /* LOOPS THROUGH ALL MESSAGES */
    for (int i = 0; i < MAXMSG; i++)
    {
        /* LOOPS THROUGH ALL KVPAIRS IN A MSG */
        for (int j = 0; j < messages[i].kvCount; j++)
        {
            /* printf("DMO i is %d, j is %d count %d\n", i, j, messages[i].kvCount); */
            // find the toPort of this message
            if (strcmp(messages[i].pair[j].key, "toPort") == 0)
            {
                // compare the toPort to the toPort (tp) tp passed in
                if (strcmp(messages[i].pair[j].value, tp) == 0)
                {
                    uniqueFlag += 1;
                }
            }
            // find the fromPort, add 1 to unique flag if it is not fp
            if (strcmp(messages[i].pair[j].key, "fromtPort") == 0)
            {
                // compare the fromPort to the fromPort (fp) passed in
                if (strcmp(messages[i].pair[j].value, fp) == 0)
                {
                    // since fp mathes, increment unique flag
                    uniqueFlag += 1;
                }
            }
            if (strcmp(messages[i].pair[j].key, "seqNumber") == 0)
            {
                // compare the seqNum to the seqNum (sn) passed in
                if (strcmp(messages[i].pair[j].value, sn) == 0)
                {
                    // since sn mathes, increment unique flag
                    uniqueFlag += 1;
                }
            }
        }
    }

    if (uniqueFlag == 3)
    {
        return 1; /* DELETE THE MESSAGE */
    }
    else
    {
        return 0; /* NOT DUP, KEEP MESSAGE */
    }
}

char *parseBufferGivenKey(char *bufferReceived, char *givenKey, char outputBuffer[])
{
    /* Parse a buffer and return the value of whatever key is provided */
    /* printf("PARSE-BUFF:'%s'\n", bufferReceived); */
    int count = 0;
    char *token;
    char *key;
    char *val;

    token = strtok(bufferReceived, ":"); // set seperator to colon
    key = token;

    while (token != NULL) // iterates through each kv pair
    {
        if (count != 0)
        {
            token = strtok(NULL, ":"); // set seperator to colon
            key = token;
        }

        token = strtok(NULL, " "); // set seperator back to a space
        val = token;

        if (val != NULL && key != NULL)
        {
            count += 1;
            char *decode = strchr(val, '\"');
            if (decode)
            {
                int i = 1;
                while (val[i] != '\"')
                {
                    if (val[i] == '^')
                    {
                        val[i] = ' ';
                    }
                    i++;
                }
            }

            if (strcmp(key, givenKey) == 0)
            {
                strcpy(outputBuffer, val);
                return outputBuffer;
            }
        }
    }
    return "not found";
}

void displayMessage(struct messages *msg, char *location)
{
    /* Prints all kv pairs for a single message */
    printf("\n************************************************************\n");
    printf("%20s %20s\n", "Name", "Value");
    for (int i = 1; i < msg->kvCount + 1; i++)
    {
        printf("%20s %20s\n", msg->pair[i].key, msg->pair[i].value);
    }
    printf("%20s %20s\n", "myLocation", location);
    /* printf("resendCount: %d\n", msg->resendCount) */;
    printf("************************************************************\n\n");
}

/* -------------- SEND MESSAGES -------------- */
void sendMessage(struct sockaddr_in server_address, int sd, char bufferOut[200])
{
    int rc; /* Return Code for err checks */

    /* strncat(bufferOut,  */
    rc = sendto(sd, bufferOut, strlen(bufferOut), 0,
                (struct sockaddr *)&server_address, sizeof(server_address));
    /* Upon successful completion, sendto() shall return the number of bytes sent.
    Otherwise, -1 shall be returned and errno set to indicate the error. */
    if (rc < strlen(bufferOut))
    {
        perror("[SENDTO-ERR] return code is less than message length");
        exit(1);
    }
}

void sendMessagesToServers(struct drones *partners, int sd, int argc, char msgToSend[200], char *cliPort, char *myLocation)
{
    /* Loops through all servers in config.file and calls sendMessaages which sends all lines in messages.txt */
    char bufferOut[200];       /* Buffer for each address */
    memset(bufferOut, 0, 200); /* ALWAYS null out buffers in C before using them */
    char *ip;                  /* Char ptr used for ip token sourced from strtok */
    char *port;                /* Char ptr used for port number token sourced from strtok */
    char *location;

    FILE *file = fopen("config.file", "r");
    if (file == NULL)
    {
        perror("Error opening file");
        exit(1);
    }

    /* printf("LOOOOCAATION: %s\n", locRes); */
    addKvPairToMsg("location", myLocation, msgToSend);

    /* APPEND fromPort to MSG */
    addKvPairToMsg("fromPort", cliPort, msgToSend);

    /* APPEND TTL TO MSG */
    char ttl[] = {(TTL - 1) + '0', '\0'};
    addKvPairToMsg("TTL", ttl, msgToSend);

    /* APPEND TTL TO MSG */
    char version[] = {(VERSION) + '0', '\0'};
    addKvPairToMsg("version", version, msgToSend);

    /* APPEND send-path */
    addKvPairToMsg("send-path", cliPort, msgToSend);

    // loop through all the lines in config.file (line e.g. '127.0.0.1 1818')
    int firstPassFlag = 0;
    while (fgets(bufferOut, sizeof(bufferOut), file) != NULL)
    {
        struct sockaddr_in server_address; /* structure for servers address */
        ip = strtok(bufferOut, " ");       // tokenize by spaces
        port = strtok(NULL, " ");          // iterate to next token
        location = strtok(NULL, " ");
        /* location[strlen(location) - 1] = '\0'; // replace newline with null char */
        if (location[strlen(location) - 1] == '\n')
        {
            location[strlen(location) - 1] = '\0'; // replace newline with null char
        }

        int portNumber = 0;
        portNumber = argValidityCheck(argc, ip, port); /* Check arg format before setting up srv_addr with them */

        server_address = serverAddressConfig(ip, portNumber); /* for each line, set up a server address to send to */

        /* JUST SENT TO A DRONE, INCREMENT THE MRA FOR THAT DRONE IN DRONES STRUCT */

        /* APPEND seqNumber TO MSG */
        if (firstPassFlag == 0)
        {
            /* !!! I need to only increment the Mra for the drone which has to toPort  */
            char parseBufferOut[200];
            char foundDestinationPort[200];
            /* char *bufferReceived, char *givenKey, char outputBuffer[] */
            char tempBuffer[200];
            strcpy(tempBuffer, msgToSend);
            char *encodedMsg = encodeMessage(tempBuffer);

            strcpy(foundDestinationPort, parseBufferGivenKey(encodedMsg, "toPort", parseBufferOut));

            int droneIndex = incrementMraForDrone(partners, foundDestinationPort); /* function finds drone w/ that location and updates mra */
            /* printf("[#%d] lastSendSeq:%d\n", droneIndex, partners[droneIndex].lastSentSeq); */

            char seqNumber[] = {partners[droneIndex].lastSentSeq + '0', '\0'};
            addKvPairToMsg("seqNumber", seqNumber, msgToSend);
            /* printf("SEQ:%s\n", seqNumber); */
        }
        firstPassFlag = 1;
        /* printf("SEND-TO-ALL:%s\n", msgToSend); */
        printf("[%s/%s-L%s] Sending message from cmd prompt to Server\n", port, ip, location);
        sendMessage(server_address, sd, msgToSend); /* Send all messages in text file to server_address setup from config.file */
        memset(bufferOut, 0, 100);                  /* ALWAYS null out buffers in C before using them */
    }
}

void forwardMessagesToServers(struct drones *partners, int sd, int argc, struct messages *msg, char *cliPort, char *myLocation, int ackFlag)
{
    /* SAME PROCESS FOR STANDARD & ACK MSG'S
        1. Decrement the TTL
        2. Append cliPort to the send-path
        3. Change the location to the current location
    */

    /* -------------------------------- FWD Message Edits ------------------------------------ */
    char fwdBuffer[200];
    char builtMessage[200];
    memset(fwdBuffer, 0, 200);
    memset(builtMessage, 0, 200);

    /* Build ack buffered message from message structure */
    strcpy(builtMessage, msgFromStructToStringFWD(msg, fwdBuffer, myLocation, cliPort)); /* returns a message that needs: send-path, location */

    /* -------------------------------- Network Broadcast ------------------------------------ */
    for (int i = 0; i <= partners->partnerAmount; i++)
    {
        /* printf("[%s/%s-L%s] struct check\n", partners[i].port, partners[i].ip_address, partners[i].location); */
        struct sockaddr_in server_address; /* structure for servers address */

        int portNumber = 0;
        portNumber = argValidityCheck(argc, partners[i].ip_address, partners[i].port); /* Check arg format before setting up srv_addr with them */
        server_address = serverAddressConfig(partners[i].ip_address, portNumber);      /* for each line, set up a server address to send to */

        char *sp = findValueFromKey(msg, "send-path");
        /* If strstr(sp, port), the port of this drone is not in the send-path, therefore send it to it */
        if (strstr(sp, partners[i].port) == NULL)
        {
            printf("[%s/%s-L%s] Message Updated\n", partners[i].port, partners[i].ip_address, partners[i].location);
            sendMessage(server_address, sd, builtMessage); /* Send all messages in text file to server_address setup from config.file */
        }
    }
}

void broadcastAcknowledgment(int sd, int portNumber, char msgToSend[200], struct messages *msg, char *cliPort, char *myLocation)
{
    char ackMsgForFunction[6000];
    memset(ackMsgForFunction, 0, 6000);
    char ackMsg[6000];

    /* Build ack buffered message from message structure */
    strcpy(ackMsg, msgFromStructToStringACK(msg, ackMsgForFunction)); /* returns a message that needs: send-path, location */
    addKvPairToMsgSpaceAfter("location", myLocation, ackMsg);
    addKvPairToMsgSpaceAfter("send-path", cliPort, ackMsg);
    strcpy(msgToSend, ackMsg);
    /* printf("Fully Built ACK Message: '%s'\n", msgToSend); */

    /* If you get a message that is in range and NOT for you, after checking
    if the TTL is positive, you will then check to the send-path.
    You will NOT forward the message to any node already in the send-path
    - get the send-path from the message
    - sendMessage if port is !in send-path
    */

    /* Sends message to all drones within config.file */
    char bufferOut[200];       /* Buffer for each drone */
    memset(bufferOut, 0, 200); /* ALWAYS null out buffers in C before using them */
    char *ip;                  /* Char ptr used for ip token sourced from strtok */
    char *port;                /* Char ptr used for port number token sourced from strtok */
    char *location;

    FILE *file = fopen("config.file", "r");
    if (file == NULL)
    {
        perror("Error opening file");
        exit(1);
    }

    /* loop through all the lines (which are drone configurations) in config.file (line e.g. '127.0.0.1 1818') */
    while (fgets(bufferOut, sizeof(bufferOut), file) != NULL)
    {
        struct sockaddr_in server_address; // structure for servers address
        ip = strtok(bufferOut, " ");       // tokenize by spaces
        port = strtok(NULL, " ");          // iterate to next token
        location = strtok(NULL, " ");
        if (location[strlen(location) - 1] == '\n')
        {
            location[strlen(location) - 1] = '\0'; // replace newline with null char
        }

        /* for each line, set up a server address to send to */
        server_address = serverAddressConfig(ip, atoi(port));
        if (strcmp(port, cliPort) != 0)
        {
            printf("[%s/%s-L%s] Broadcasting Acknowledgment\n", port, ip, location);
            sendMessage(server_address, sd, msgToSend); /* Send all messages in text file to server_address setup from config.file */
        }
        memset(bufferOut, 0, 100); /* ALWAYS null out buffers in C before using them */
    }
}

/* -------------- CUSTOM HELPER FUNCTIONS -------------- */
char *findValueFromKey(struct messages *msg, char keyToFind[])
{
    int i;
    for (i = 1; i < msg->kvCount + 1; i++)
    {
        if (strcmp(msg->pair[i].key, keyToFind) == 0)
        {
            return msg->pair[i].value;
        }
    }
    return NULL;
}

int euclideanDistance(int location, int myLocation, int rows, int cols)
{
    /* printf("[E-DISTANCE] myLocation: %d, senderLocation: %d, rows: %d, cols: %d\n", myLocation, location, rows, cols); */
    double distance = 0;

    /* Mod Math for coordinates of recieving drone */
    int x1 = ((myLocation - 1) % cols);
    int y1 = ((myLocation - 1) / cols);

    /* Mod Math for coordinates of sending drone */
    int x2 = ((location - 1) % cols);
    int y2 = ((location - 1) / cols);

    /* Euclidean Distance Formula  */
    distance = sqrt(pow(x2 - x1, 2) + pow(y2 - y1, 2));

    /* printf("[E-DISTANCE] myCoordinate(%d, %d) -> senderCoordinate(%d, %d) = %f, %f(db)\n", x1, y1, x2, y2, trunc(distance), distance); */
    return trunc(distance);
}

char *addKvPairToMsg(char *key, char *value, char *msg)
{
    char kvPair[200];
    sprintf(kvPair, " %s:%s", key, value);
    strcat(msg, kvPair);
    return msg;
}

char *addKvPairToMsgSpaceAfter(char *key, char *value, char *msg)
{
    char kvPair[200];
    sprintf(kvPair, "%s:%s ", key, value);
    strcat(msg, kvPair);
    return msg;
}

char *msgFromStructToString(struct messages *msg, char messageToSend[])
{
    memset(messageToSend, 0, 600);
    for (int i = 1; i < msg->kvCount + 1; i++)
    {
        char kvPair[200];
        if (strcmp(msg->pair[i].key, "location") == 0)
        {
            continue;
        }
        else
        {
            if (i == 1)
            {
                sprintf(kvPair, "%s:%s", msg->pair[i].key, msg->pair[i].value);
                strcat(messageToSend, kvPair);
            }
            else
            {
                sprintf(kvPair, " %s:%s", msg->pair[i].key, msg->pair[i].value);
                strcat(messageToSend, kvPair);
            }
            /* printf("kvPair: %s\n", kvPair); */
        }
    }
    return messageToSend;
}

char *msgFromStructToStringACK(struct messages *msg, char messageToSend[])
{
    memset(messageToSend, 0, 600);
    char newToPort[200];
    char newFromPort[200];
    char ttl[] = {(TTL - 1) + '0', '\0'};

    for (int i = 1; i < msg->kvCount + 1; i++)
    {
        char kvPair[200];
        /* Do not include the msg, TTL, location, or send-path from the previous message */
        if (strcmp(msg->pair[i].key, "msg") == 0 || strcmp(msg->pair[i].key, "send-path") == 0 || strcmp(msg->pair[i].key, "TTL") == 0 || strcmp(msg->pair[i].key, "location") == 0)
        {
            continue;
        }
        if (strcmp(msg->pair[i].key, "toPort") == 0) /* Switch the toPort and fromPort when acking the message */
        {
            strcpy(newFromPort, msg->pair[i].value);
            continue;
        }
        if (strcmp(msg->pair[i].key, "fromPort") == 0)
        {
            strcpy(newToPort, msg->pair[i].value);
            continue;
        }
        else /* add the rest of the old kv pairs to ack message */
        {
            sprintf(kvPair, "%s:%s ", msg->pair[i].key, msg->pair[i].value);
            strcat(messageToSend, kvPair);
        }
    }

    addKvPairToMsgSpaceAfter("type", "ACK", messageToSend);
    addKvPairToMsgSpaceAfter("toPort", newToPort, messageToSend);
    addKvPairToMsgSpaceAfter("fromPort", newFromPort, messageToSend);
    addKvPairToMsgSpaceAfter("TTL", ttl, messageToSend);

    return messageToSend;
}

char *msgFromStructToStringFWD(struct messages *msg, char messageToSend[], char *myLocation, char *cliPort)
{
    /* Forwarding Process:
        1. Decrement the TTL
        2. Append cliPort to the send-path
        3. Change the location to the current location
    */
    memset(messageToSend, 0, 200);
    int newTtl = 0;

    for (int i = 1; i < msg->kvCount + 1; i++)
    {
        char kvPair[200];
        /* Do not include the msg, TTL, location, or send-path from the previous message */
        if (strcmp(msg->pair[i].key, "TTL") == 0)
        {
            /* Decrement TTL  & update in struct*/
            newTtl = atoi(findValueFromKey(msg, "TTL")) - 1;
            updateValueGivenKeyAndNewValue(msg, "TTL", newTtl);
            continue;
        }
        if (strcmp(msg->pair[i].key, "location") == 0)
        {
            /* Change location to myLocation */
            sprintf(kvPair, "%s:%s ", msg->pair[i].key, myLocation);
            strcat(messageToSend, kvPair);
            continue;
        }
        if (strcmp(msg->pair[i].key, "send-path") == 0)
        {
            /* Append cliPort to send-path */
            strcat(msg->pair[i].value, ",");
            strcat(msg->pair[i].value, cliPort);
            /* printf("FWD-SEND-PATH: %s\n", msg->pair[i].value); */
            sprintf(kvPair, "%s:%s ", msg->pair[i].key, msg->pair[i].value);
            strcat(messageToSend, kvPair);
            continue;
        }
        else /* add the rest of the old kv pairs to ack message */
        {
            sprintf(kvPair, "%s:%s ", msg->pair[i].key, msg->pair[i].value);
            strcat(messageToSend, kvPair);
        }
    }
    char newTTLChar[] = {newTtl + '0', '\0'}; /* convert newTtl to char for string */
    addKvPairToMsgSpaceAfter("TTL", newTTLChar, messageToSend);
    return messageToSend;
}

char *msgFromStructToStringResend(struct messages *msg, char messageToSend[], char *myLocation, char *cliPort)
{
    /* Forwarding Process:
        1. Decrement the TTL
        2. Append cliPort to the send-path
        3. Change the location to the current location
    */
    memset(messageToSend, 0, 200);
    /* int newTtl = 0; */

    for (int i = 1; i < msg->kvCount + 1; i++)
    {
        char kvPair[200];
        /* Do not include the msg, TTL, location, or send-path from the previous message */
        // if (strcmp(msg->pair[i].key, "TTL") == 0)
        // {
        //     /* Decrement TTL  & update in struct*/
        //     newTtl = atoi(findValueFromKey(msg, "TTL")) - 1;
        //     updateValueGivenKeyAndNewValue(msg, "TTL", newTtl);
        //     continue;
        // }
        if (strcmp(msg->pair[i].key, "location") == 0)
        {
            /* Change location to myLocation */
            sprintf(kvPair, "%s:%s ", msg->pair[i].key, myLocation);
            strcat(messageToSend, kvPair);
            continue;
        }
        // if (strcmp(msg->pair[i].key, "send-path") == 0)
        // {
        //     /* Append cliPort to send-path */
        //     strcat(msg->pair[i].value, ",");
        //     strcat(msg->pair[i].value, cliPort);
        //     /* printf("FWD-SEND-PATH: %s\n", msg->pair[i].value); */
        //     sprintf(kvPair, "%s:%s ", msg->pair[i].key, msg->pair[i].value);
        //     strcat(messageToSend, kvPair);
        //     continue;
        // }
        else /* add the rest of the old kv pairs to ack message */
        {
            sprintf(kvPair, "%s:%s ", msg->pair[i].key, msg->pair[i].value);
            strcat(messageToSend, kvPair);
        }
    }
    /* char newTTLChar[] = {newTtl + '0', '\0'}; */
    /* addKvPairToMsgSpaceAfter("TTL", newTTLChar, messageToSend); */
    return messageToSend;
}

void updateValueGivenKeyAndNewValue(struct messages *msg, char *key, int newValue)
{
    for (int i = 1; i < msg->kvCount + 1; i++)
    {
        if (strcmp(msg->pair[i].key, key) == 0)
        {
            char newVal[200];
            sprintf(newVal, "%d", newValue);
            memset(msg->pair[i].value, '0', 100);
            strcpy(msg->pair[i].value, newVal);
        }
    }
}

void displayDroneConfig(struct drones *partners, int partnerCount)
{
    printf("------ DRONE CONFIG ------\n");
    for (int i = 0; i < partnerCount; i++)
    {
        printf("ip:%s, port: %s, location: %s, lastSentSeq: %d\n", partners[i].ip_address, partners[i].port, partners[i].location, partners[i].lastSentSeq);
    }
}

int incrementMraForDrone(struct drones *partners, char port[])
{
    for (int i = 0; i < MAX_PARTNERS; i++)
    {
        if (strcmp(partners[i].port, port) == 0)
        {
            partners[i].lastSentSeq += 1;
            /* printf("FOUND DRONE W PORT: %s, LSS: %d\n", port, partners[i].lastSentSeq); */
            return i;
        }
    }
    return 99;
}

int findPartnerGivenPort(struct drones *partners, char port[])
{
    for (int i = 0; i < MAX_PARTNERS; i++)
    {
        if (strcmp(partners[i].port, port) == 0)
        {
            return i;
        }
    }
    return 99;
}

char *findPartnerLocationGivenPort(struct drones *partners, char *port, char outputBuffer[])
{
    for (int i = 0; i < MAX_PARTNERS; i++)
    {
        if (strcmp(partners[i].port, port) == 0)
        {
            strcpy(outputBuffer, partners[i].location);
            return outputBuffer;
        }
    }
    return "location not found";
}

/* DRONE STRUCT HELPER FUNCTIONS */

char *droneStructInit(struct drones *partners, char *myPortNumber, char myLocation[])
{
    /*
    Description:
        - Loops through all 'drone' configurations in config.file.
        - Fills each drone struct instance with their respective port numbers (unique drone key).
        - Initializes lastSentSeq and mostRecentAck to 0.
    Returns: the location of this drone
    */
    char bufferOut[200]; /* Buffer for each address */
    memset(bufferOut, 0, 200);

    char *port;
    char *location;
    char *ip;

    FILE *file = fopen("config.file", "r");
    if (file == NULL)
    {
        perror("Error opening file");
        exit(1);
    }
    int partnerCount = -1;

    // loop through all the lines in config.file (line e.g. '127.0.0.1 1818')
    while (fgets(bufferOut, sizeof(bufferOut), file) != NULL)
    {
        partnerCount += 1;
        ip = strtok(bufferOut, " "); // tokenize by spaces
        port = strtok(NULL, " ");    // iterate to next token
        location = strtok(NULL, " ");
        /* location[strlen(location) - 1] = '\0'; */ // replace newline with null char
        if (location[strlen(location) - 1] == '\n')
        {
            location[strlen(location) - 1] = '\0'; // replace newline with null char
        }

        // set each drone's port (drone's are unaware of other drones locations)
        strcpy(partners[partnerCount].port, port);
        strcpy(partners[partnerCount].location, location);
        strcpy(partners[partnerCount].ip_address, ip);
        if (strcmp(myPortNumber, port) == 0)
        {
            strcpy(myLocation, location);
        }
        partners[partnerCount].mostRecentAck = 0;
        partners[partnerCount].lastSentSeq = 0;
        memset(bufferOut, 0, 100);
    }
    partners->partnerAmount = partnerCount;
    return myLocation;
}

void updateMyLocation(struct drones *partners, char *myPortNumber, char newLocation[])
{
    for (int i = 0; i < MAX_PARTNERS; i++)
    {
        /* found this drone in struct */
        if (strcmp(partners[i].port, myPortNumber) == 0)
        {
            memset(partners[i].location, '0', 100);    /* clear location */
            strcpy(partners[i].location, newLocation); /* set new location */
        }
    }
}

void messageStorageInit(struct messages *msg)
{
    for (int i = 0; i < MAXMSG; i++)
    {
        msg[i].resendCount = 0;
        msg[i].init = 0;
        msg[i].kvCount = 0;
    }
}

/** Resending Messages
 * * When a drone moves, or has a select timeout, resend all of the msg's with a resendCount > 0
 * * Otherwise, the message has "died" and we delete the message
 */
void reSend(struct messages *msg, int sd, struct drones *partners, char myLocation[], char myPortNumber[])
{
    /* int counter = 0; */
    for (int i = 0; i < MAXMSG; i++)
    {
        if (msg[i].resendCount > 0 && msg[i].init == 1)
        {
            /* printf("### RESENDING MSG #%d ###\n", counter++); */
            char outBuff[200];
            memset(outBuff, 0, 200);
            char *loc = findPartnerLocationGivenPort(partners, myPortNumber, outBuff);
            /* displayMessage(msg, myLocation); */

            /* -------------------------------- FWD Message Edits ------------------------------------ */
            char fwdBuffer[200];
            char builtMessage[200];
            memset(fwdBuffer, 0, 200);
            memset(builtMessage, 0, 200);

            /* Build ack buffered message from message structure */
            strcpy(builtMessage, msgFromStructToStringResend(msg, fwdBuffer, loc, myPortNumber)); /* returns a message that needs: send-path, location */

            /* -------------------------------- Network Broadcast ------------------------------------ */
            for (int i = 0; i <= partners->partnerAmount; i++)
            {
                /* printf("[%s/%s-L%s] struct check\n", partners[i].port, partners[i].ip_address, partners[i].location); */
                struct sockaddr_in server_address; /* structure for servers address */

                int portNumber = 0;
                portNumber = argValidityCheck(4, partners[i].ip_address, partners[i].port); /* Check arg format before setting up srv_addr with them */
                server_address = serverAddressConfig(partners[i].ip_address, portNumber);   /* for each line, set up a server address to send to */

                char *sp = findValueFromKey(msg, "send-path");
                /* If strstr(sp, port), the port of this drone is not in the send-path, therefore send it to it */
                if (strstr(sp, partners[i].port) == NULL)
                {
                    printf("[%s/%s-L%s] Resending Message\n", partners[i].port, partners[i].ip_address, partners[i].location);
                    sendMessage(server_address, sd, builtMessage); /* Send all messages in text file to server_address setup from config.file */
                }
            }
            /* --------------------------------------------------------------------------------------- */

            msg[i].resendCount--;
            /* printf("[%d] RESEND-COUNT: %d\n", i, msg[i].resendCount); */
        }
        else if (msg[i].resendCount <= 0 && msg[i].init == 1)
        {
            deleteMessage(&msg[i], i);
        }
    }
}

void deleteMessage(struct messages *msg, int messageNumber)
{
    /* wipe key value pairs */
    for (int i = 0; i < msg->kvCount + 1; i++)
    {
        memset(msg->pair[i].key, 0, 100);
        memset(msg->pair[i].value, 0, 100);
    }
    /* printf("!!! MESSAGE DELETED !!!\n");
    displayMessage(msg, "none"); */

    /* resent flags */
    msg->init = 0;
    msg->resendCount = 0;
    msg->kvCount = 0;
    msg->typeAckFlag = 0;
}

void deleteMsg(struct messages *msg, int messageNumber, char *fromPort, char *toPort, char *seqNumber)
{
    /* wipe key value pairs */
    for (int i = 0; i < msg->kvCount + 1; i++)
    {
        memset(msg->pair[i].key, 0, 100);
        memset(msg->pair[i].value, 0, 100);
    }
    /* printf("!!! MESSAGE DELETED !!!\n");
    displayMessage(msg, "none"); */

    /* resent flags */
    msg->init = 0;
    msg->resendCount = 0;
    msg->kvCount = 0;
    msg->typeAckFlag = 0;
}

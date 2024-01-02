
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <netdb.h>
#include <arpa/inet.h>

#define MAX_MESSAGES 100
#define MAX_THREADS 10
int exit_status = 0;

// Shared memory structure for message queue
struct SharedMemory
{
    int messages[MAX_MESSAGES];
    int readIdx;
    int writeIdx;
    int produced;
    int consumed;
    char status_message[256];
};

struct SharedMemory *shared_memory;
// char status_message[256];

// Mutex for shared resources
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t roomAvailable, dataAvailable;

// Function to simulate the producer
void *producer(void *arg)
{
    int item = 0;
    while (1)
    {

        // Produce Item
        srand(time(NULL));
        item = rand();

        pthread_mutex_lock(&mutex);
        if ((shared_memory->writeIdx + 1) % MAX_MESSAGES == shared_memory->readIdx)
        {
            pthread_cond_wait(&roomAvailable, &mutex);
        }

        shared_memory->messages[shared_memory->writeIdx] = item;
        shared_memory->writeIdx = (shared_memory->writeIdx + 1) % MAX_MESSAGES;
        shared_memory->produced++;

        pthread_cond_signal(&dataAvailable);
        pthread_mutex_unlock(&mutex);

        // sleep(1);
    }
}

// Function to simulate a consumer
void *consumer(void *arg)
{
    int consumer_id = *(int *)arg;
    int item;
    while (1)
    {
        pthread_mutex_lock(&mutex);

        // Consume a message
        if (shared_memory->readIdx == shared_memory->writeIdx)
        {
            pthread_cond_wait(&dataAvailable, &mutex);
        }

        item = shared_memory->messages[shared_memory->readIdx];
        shared_memory->readIdx = (shared_memory->readIdx + 1) % MAX_MESSAGES;

        shared_memory->consumed++;

        pthread_mutex_unlock(&mutex);

        sleep(30);
    }
}

static void *monitor(void *arg)
{

    while (1)
    {
        pthread_mutex_lock(&mutex);
        sprintf(shared_memory->status_message, "Queue Length: %d, Produced: %d, Consumed: %d\n",
                (shared_memory->writeIdx - shared_memory->readIdx + MAX_MESSAGES) % MAX_MESSAGES,
                shared_memory->produced,
                shared_memory->consumed);
        pthread_mutex_unlock(&mutex);
    }
}

/* Receive routine: use recv to receive from socket and manage
   the fact that recv may return after having read less bytes than
   the passed buffer size
   In most cases recv will read ALL requested bytes, and the loop body
   will be executed once. This is not however guaranteed and must
   be handled by the user program. The routine returns 0 upon
   successful completion, -1 otherwise */
static int receive(int sd, char *retBuf, int size)
{
    int totSize, currSize;
    totSize = 0;
    while (totSize < size)
    {
        currSize = recv(sd, &retBuf[totSize], size - totSize, 0);
        if (currSize <= 0)
            /* An error occurred */
            return -1;
        totSize += currSize;
    }
    return 0;
}

/* Handle an established  connection
   routine receive is listed in the previous example */
static void handleConnection(int currSd)
{
    unsigned int netLen;
    int len;
    char *command, *answer;
    for (;;)
    {
        /* Get the command string length
           If receive fails, the client most likely exited */
        if (receive(currSd, (char *)&netLen, sizeof(netLen)))
            break;
        /* Convert from network byte order */
        len = ntohl(netLen);
        command = malloc(len + 1);
        /* Get the command and write terminator */
        receive(currSd, command, len);
        command[len] = 0;
        if (strcmp(command, "help") == 0)
        {
            // answer = strdup("server is active...\n");
            pthread_mutex_lock(&mutex);
            answer = strdup(shared_memory->status_message);
            pthread_mutex_unlock(&mutex);
            if (answer == NULL)
            {
                perror("strdup");
                // Handle the error, possibly by sending an error response to the client
                // or by setting a default response.
                answer = strdup("Internal server error");
            }
        }
        else if (strcmp(command, "stop") == 0)
        {
            answer = strdup("closing server connection");
            exit_status = 1;
        }
        else
            answer = strdup("invalid command (try help).");
        /* Send the answer back */
        len = strlen(answer);
        /* Convert to network byte order */
        netLen = htonl(len);
        /* Send answer character length */
        if (send(currSd, &netLen, sizeof(netLen), 0) == -1)
            break;
        /* Send answer characters */
        if (send(currSd, answer, len, 0) == -1)
            break;
        free(command);
        free(answer);
        if (exit_status)
        {
            printf("Force stoping server...\n");
            exit(0);
        }
    }
    /* The loop is most likely exited when the connection is terminated */
    printf("Connection terminated\n");
    close(currSd);
}

/* Thread routine. It calls routine handleConnection()
   defined in the previous program. */
static void *connectionHandler(void *arg)
{
    int currSock = *(int *)arg;
    handleConnection(currSock);
    free(arg);
    pthread_exit(0);
    return NULL;
}

int startServer(int port)
{
    int sd, *currSd;
    int sAddrLen;
    // int port;
    int len;
    unsigned int netLen;
    char *command, *answer;
    struct sockaddr_in sin, retSin;
    pthread_t threads[MAX_THREADS];

    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("socket");
        exit(1);
    }
    /* set socket options REUSE ADDRESS */
    int reuse = 1;
    if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");
#ifdef SO_REUSEPORT
    if (setsockopt(sd, SOL_SOCKET, SO_REUSEPORT, (const char *)&reuse, sizeof(reuse)) < 0)
        perror("setsockopt(SO_REUSEPORT) failed");
#endif
    /* Initialize the address (struct sokaddr_in) fields */
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(port);

    /* Bind the socket to the specified port number */
    if (bind(sd, (struct sockaddr *)&sin, sizeof(sin)) == -1)
    {
        perror("bind");
        exit(1);
    }
    /* Set the maximum queue length for clients requesting connection to 5 */
    if (listen(sd, 5) == -1)
    {
        perror("listen");
        exit(1);
    }
    sAddrLen = sizeof(retSin);
    /* Accept and serve all incoming connections in a loop */
    for (int i = 0; i < MAX_THREADS; ++i)
    {
        /* Allocate the current socket.
           It will be freed just before thread termination. */
        currSd = (int *)malloc(sizeof(int));
        if ((*currSd = accept(sd, (struct sockaddr *)&retSin, &sAddrLen)) == -1)
        {
            perror("accept");
            exit(1);
        }
        printf("Connection received from %s\n", inet_ntoa(retSin.sin_addr));
        /* Connection received, start a new thread serving the connection */
        pthread_create(&threads[i], NULL, connectionHandler, currSd);
    }

    return 0; // nrever reached
}
/* Main Program */
int main(int argc, char *argv[])
{
    printf("startServer");
    int port;
    if (argc < 2)
    {
        printf("Usage: server <port>\n");
        exit(0);
    }
    sscanf(argv[1], "%d", &port);

    // Create shared memory
    key_t key = ftok(".", 's');
    int shmid = shmget(key, sizeof(struct SharedMemory), IPC_CREAT | 0666);
    if (shmid < 0)
    {
        perror("Error creating shared memory");
        exit(1);
    }

    shared_memory = (struct SharedMemory *)shmat(shmid, NULL, 0);
    if ((long)shared_memory == -1)
    {
        perror("Error attaching shared memory");
        exit(1);
    }

    // Initialize shared memory variables
    shared_memory->readIdx = 0;
    shared_memory->writeIdx = 0;
    shared_memory->produced = 0;
    shared_memory->consumed = 0;
    strcpy(shared_memory->status_message, "Hello, World!");

    // Create the producer thread
    pthread_t producer_thread;
    pthread_create(&producer_thread, NULL, producer, NULL);

    // Create multiple consumer threads
    pthread_t consumer_threads[MAX_THREADS];
    int consumer_ids[3] = {1, 2, 3};
    for (int i = 0; i < 2; ++i)
    {
        pthread_create(&consumer_threads[i], NULL, consumer, &consumer_ids[i]);
    }

    // Create the monitor thread
    pthread_t monitor_thread;
    pthread_create(&monitor_thread, NULL, monitor, NULL);

    if (startServer(port) != 0)
    {
        perror("Error starting the server");
        exit(1);
    }
    // Join threads (wait for them to finish)
    pthread_join(producer_thread, NULL);
    for (int i = 0; i < 2; ++i)
    {
        pthread_join(consumer_threads[i], NULL);
    }
    pthread_join(monitor_thread, NULL);

    // Detach and remove shared memory
    shmdt((void *)shared_memory);
    shmctl(shmid, IPC_RMID, NULL);

    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>

#define BUFFER_SIZE 1024
#define QUEUE_SIZE 10
#define TEST_INTERVAL_MS 33  // Send test message every 33ms

// Enum for selecting client mode
typedef enum {
    MODE_INTERACTIVE = 1,
    MODE_TEST = 2
} ClientMode;

// structure for FIFO message queue
typedef struct {
    char messages[QUEUE_SIZE][BUFFER_SIZE];
    int front;
    int rear;
    int count;
    pthread_mutex_t lock;
} MessageQueue;

// structure holding TDMA timing and slot control info
typedef struct {
    int my_slot;
    int current_slot;
    int slot_duration_ms;
    int my_turn;
    long long time_to_my_slot;
    pthread_mutex_t lock;
} TDMAInfo;

// stats used in test mode
typedef struct {
    unsigned long messages_sent;
    unsigned long messages_queued;
    pthread_mutex_t lock;
} TestStats;

int sock = 0;
int running = 1;
int client_id = 0;
ClientMode client_mode = MODE_INTERACTIVE;
MessageQueue msg_queue;
TDMAInfo tdma_info;
TestStats test_stats = {0, 0};

// Get current time in milliseconds
long long get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)(tv.tv_sec) * 1000 + (tv.tv_usec) / 1000;
}

void init_message_queue() {
    msg_queue.front = 0;
    msg_queue.rear = -1;
    msg_queue.count = 0;
    pthread_mutex_init(&msg_queue.lock, NULL);
}

void init_tdma_info() {
    tdma_info.my_slot = -1;
    tdma_info.current_slot = -1;
    tdma_info.slot_duration_ms = 0;
    tdma_info.my_turn = 0;
    tdma_info.time_to_my_slot = 0;
    pthread_mutex_init(&tdma_info.lock, NULL);
}

void init_test_stats() {
    test_stats.messages_sent = 0;
    test_stats.messages_queued = 0;
    pthread_mutex_init(&test_stats.lock, NULL);
}

// add mesage to queue
int enqueue_message(const char *msg) {
    pthread_mutex_lock(&msg_queue.lock);
    
    // if queue full, reject
    if (msg_queue.count >= QUEUE_SIZE) {
        pthread_mutex_unlock(&msg_queue.lock);
        return 0;  // Queue full
    }
    
    msg_queue.rear = (msg_queue.rear + 1) % QUEUE_SIZE;
    strncpy(msg_queue.messages[msg_queue.rear], msg, BUFFER_SIZE - 1);
    msg_queue.messages[msg_queue.rear][BUFFER_SIZE - 1] = '\0';
    msg_queue.count++;
    
    pthread_mutex_unlock(&msg_queue.lock);
    return 1;
}

// remove messge from queue
int dequeue_message(char *msg) {
    pthread_mutex_lock(&msg_queue.lock);
    
    // if queue empty, return nothing
    if (msg_queue.count == 0) {
        pthread_mutex_unlock(&msg_queue.lock);
        return 0;  // Queue empty
    }
    
    strcpy(msg, msg_queue.messages[msg_queue.front]);
    msg_queue.front = (msg_queue.front + 1) % QUEUE_SIZE;
    msg_queue.count--;
    
    pthread_mutex_unlock(&msg_queue.lock);
    return 1;
}

// parses server welcome messge containing clot conig
void parse_welcome_message(const char *msg) {

    // Format: WELCOME|client_id=X|slot=Y|slot_duration=Z
    sscanf(msg, "WELCOME|client_id=%d|slot=%d|slot_duration=%d",
           &client_id, &tdma_info.my_slot, &tdma_info.slot_duration_ms);
    
           // if in interacting mode, print assigned parameters
    if (client_mode == MODE_INTERACTIVE) {
        printf("\n=== TDMA Configuration ===\n");
        printf("Client ID: %d\n", client_id);
        printf("Assigned Slot: %d\n", tdma_info.my_slot);
        printf("Slot Duration: %d ms\n", tdma_info.slot_duration_ms);
        printf("==========================\n\n");
    } else {
        printf("[TEST MODE] Client ID: %d, Slot: %d\n", client_id, tdma_info.my_slot);
    }
}

// parses periodic TDMA timing/status messages
void parse_tdma_info(const char *msg) {
    // Format: TDMA_INFO|slot=X|slot_duration=Y|frame=Z|time_to_slot=W
    int frame;
    pthread_mutex_lock(&tdma_info.lock);
    sscanf(msg, "TDMA_INFO|slot=%d|slot_duration=%d|frame=%d|time_to_slot=%lld",
           &tdma_info.my_slot, &tdma_info.slot_duration_ms, 
           &frame, &tdma_info.time_to_my_slot);
    pthread_mutex_unlock(&tdma_info.lock);
}

// parses messages informing whether it's our turn
void parse_slot_active(const char *msg) {
    // Format: SLOT_ACTIVE|your_turn=X|...
    int your_turn;
    pthread_mutex_lock(&tdma_info.lock);
    
    // if message fromat starts with your_turn, update TDMA turn info
    if (sscanf(msg, "SLOT_ACTIVE|your_turn=%d", &your_turn) == 1) {
        tdma_info.my_turn = your_turn;
        
        // If it's our turn, parse active slot and duration
        if (your_turn) {
            sscanf(msg, "SLOT_ACTIVE|your_turn=%d|slot=%d|duration=%d",
                   &your_turn, &tdma_info.current_slot, &tdma_info.slot_duration_ms);
        } else {
            long long wait_time;
            sscanf(msg, "SLOT_ACTIVE|your_turn=%d|current_slot=%d|your_slot=%d|wait_time=%lld",
                   &your_turn, &tdma_info.current_slot, &tdma_info.my_slot, &wait_time);
        }
    }
    
    pthread_mutex_unlock(&tdma_info.lock);
}

// Parses a normal message sent by another client
void parse_message(const char *msg) {
    // Format: MESSAGE|from=X|slot=Y|text=Z
    int from_id, from_slot;
    char text[BUFFER_SIZE];
    
    // If message matches the expected format
    if (sscanf(msg, "MESSAGE|from=%d|slot=%d|text=%[^\n]", 
               &from_id, &from_slot, text) == 3) {
        if (client_mode == MODE_INTERACTIVE) {
            printf("\n[Client %d, Slot %d]: %s\n", from_id, from_slot, text);
        }
        // In test mode, silently receive (observe via Wireshark)
    }
}

// handle collisions
void parse_collision(const char *msg) {
    // Format: COLLISION|your_slot=X|current_slot=Y|message_dropped
    int your_slot, current_slot;
    
    if (sscanf(msg, "COLLISION|your_slot=%d|current_slot=%d", 
               &your_slot, &current_slot) == 2) {
        if (client_mode == MODE_INTERACTIVE) {
            printf("\n[COLLISION DETECTED!] You transmitted in Slot %d, but your assigned slot is %d\n",
                   current_slot, your_slot);
            printf("Message was dropped. Please wait for your time slot.\n");
        }
    }
}

// allows for Ctrl+C shutdown
void signal_handler(int sig) {
    running = 0;
    if (sock > 0) {
        close(sock);
    }
    exit(0);
}

// processes teh incoming traffic from server
void *receive_messages(void *arg) {
    char buffer[BUFFER_SIZE];
    int valread;
    
    // while on
    while (running) {
        memset(buffer, 0, BUFFER_SIZE);
        valread = read(sock, buffer, BUFFER_SIZE - 1);
        
        if (valread > 0) {
            buffer[valread] = '\0';
            
            // Parse different message types
            if (strncmp(buffer, "WELCOME|", 8) == 0) {
                parse_welcome_message(buffer);
            } else if (strncmp(buffer, "TDMA_INFO|", 10) == 0) {
                parse_tdma_info(buffer);
            } else if (strncmp(buffer, "SLOT_ACTIVE|", 12) == 0) {
                parse_slot_active(buffer);
            } else if (strncmp(buffer, "MESSAGE|", 8) == 0) {
                parse_message(buffer);
                if (client_mode == MODE_INTERACTIVE) {
                    printf("Enter message: ");
                    fflush(stdout);
                }
            } else if (strncmp(buffer, "COLLISION|", 10) == 0) {
                parse_collision(buffer);
                if (client_mode == MODE_INTERACTIVE) {
                    printf("Enter message: ");
                    fflush(stdout);
                }
            } else {
                if (client_mode == MODE_INTERACTIVE) {
                    printf("\n%s", buffer);
                    printf("Enter message: ");
                    fflush(stdout);
                }
            }
        } else if (valread == 0) {
            printf("\nServer disconnected\n");
            running = 0;
            break;
        }
    }
    
    return NULL;
}

// sends messages during our TDMA time slot
void *transmit_messages(void *arg) {
    char msg[BUFFER_SIZE];
    
    while (running) {
        // Check if it's our turn to transmit
        pthread_mutex_lock(&tdma_info.lock);
        int can_transmit = tdma_info.my_turn;
        pthread_mutex_unlock(&tdma_info.lock);
        
        if (can_transmit && msg_queue.count > 0) {
            if (dequeue_message(msg)) {
                if (send(sock, msg, strlen(msg), 0) < 0) {
                    printf("\nSend failed\n");
                    running = 0;
                    break;
                }
                
                // Update statistics in test mode
                if (client_mode == MODE_TEST) {
                    pthread_mutex_lock(&test_stats.lock);
                    test_stats.messages_sent++;
                    pthread_mutex_unlock(&test_stats.lock);
                }
            }
        }
        
        usleep(5000);  // Check every 5ms for faster response with 100ms slots
    }
    
    return NULL;
}

// generate message seuqnce for test mode eveery 33 ms
void *test_message_generator(void *arg) {
    unsigned long sequence = 0;
    char test_msg[BUFFER_SIZE];
    long long last_send_time = get_time_ms();
    
    printf("[TEST MODE] Starting automatic message generation every %d ms\n", TEST_INTERVAL_MS);
    printf("[TEST MODE] Messages will be queued and sent during assigned TDMA slots\n");
    printf("[TEST MODE] Press Ctrl+C to stop\n\n");
    
    // Wait for TDMA slot/time info to be received
    sleep(1);
    
    while (running) {
        long long current_time = get_time_ms();
        
        // Check if it's time to generate a new test message
        if (current_time - last_send_time >= TEST_INTERVAL_MS) {
            snprintf(test_msg, BUFFER_SIZE, "[TEST] Client %d, Seq %lu, Time %lld\n", 
                     client_id, sequence++, current_time);
            
            if (enqueue_message(test_msg)) {
                pthread_mutex_lock(&test_stats.lock);
                test_stats.messages_queued++;
                pthread_mutex_unlock(&test_stats.lock);
            }
            
            last_send_time = current_time;
        }
        
        usleep(1000);  // Check every 1ms for precise timing
    }
    
    return NULL;
}

// test mode states printed every 5 seconds
void *statistics_reporter(void *arg) {
    long long start_time = get_time_ms();
    
    // when running and on test mode
    while (running && client_mode == MODE_TEST) {
        sleep(5);  // Report every 5 seconds
        
        pthread_mutex_lock(&test_stats.lock);
        unsigned long queued = test_stats.messages_queued;
        unsigned long sent = test_stats.messages_sent;
        pthread_mutex_unlock(&test_stats.lock);
        
        long long elapsed = (get_time_ms() - start_time) / 1000;  // seconds
        
        printf("[TEST STATS] Runtime: %lld s | Queued: %lu | Sent: %lu | Queue: %d\n",
               elapsed, queued, sent, msg_queue.count);
    }
    
    return NULL;
}

// print user status
void display_status() {
    pthread_mutex_lock(&tdma_info.lock);
    printf("\n=== TDMA Status ===\n");
    printf("Your Slot: %d\n", tdma_info.my_slot);
    printf("Current Slot: %d\n", tdma_info.current_slot);
    printf("Your Turn: %s\n", tdma_info.my_turn ? "YES" : "NO");
    printf("Queued Messages: %d\n", msg_queue.count);
    pthread_mutex_unlock(&tdma_info.lock);
    
    if (client_mode == MODE_TEST) {
        pthread_mutex_lock(&test_stats.lock);
        printf("Test Messages Queued: %lu\n", test_stats.messages_queued);
        printf("Test Messages Sent: %lu\n", test_stats.messages_sent);
        pthread_mutex_unlock(&test_stats.lock);
    }
    printf("==================\n");
}

int main(int argc, char *argv[]) {
    struct sockaddr_in serv_addr;
    pthread_t recv_thread, tx_thread, test_gen_thread, stats_thread;
    char buffer[BUFFER_SIZE];
    
    // Setup clean exit 
    signal(SIGINT, signal_handler);
    
    //error checking for correct number of args - server ip, mode required
    if (argc != 3) {
        printf("Usage: %s <server_ip> <mode>\n", argv[0]);
        printf("Modes:\n");
        printf("  1 - Interactive mode (manual message entry)\n");
        printf("  2 - Test mode (automatic messages every 33ms)\n");
        printf("Example: %s 192.168.25.1 1\n", argv[0]);
        return -1;
    }
    
    // parse arguments to determine interactive or test mode function
    int mode = atoi(argv[2]);
    if (mode == 1) {
        client_mode = MODE_INTERACTIVE;
    } else if (mode == 2) {
        client_mode = MODE_TEST;
    } else {
        printf("Invalid mode. Use 1 for interactive or 2 for test mode.\n");
        return -1;
    }
    
    init_message_queue();
    init_tdma_info();
    init_test_stats();
    
    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Socket creation error\n");
        return -1;
    }
    
    //socket settings - port and IP of TCP server
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(8080);
    
    // Convert IPv4 address from text to binary
    if (inet_pton(AF_INET, argv[1], &serv_addr.sin_addr) <= 0) {
        printf("Invalid address / Address not supported\n");
        return -1;
    }
    
    // Connect to server
    printf("Connecting to TDMA server at %s:8080...\n", argv[1]);
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Connection failed. Make sure the server is running.\n");
        return -1;
    }
    
    printf("Server connection succesful.\n");
    
    if (client_mode == MODE_INTERACTIVE) {
        printf("Mode: INTERACTIVE - Manual message entry\n");
    } else {
        printf("Mode: TEST - Automatic message generation (33ms interval)\n");
    }
    
    printf("Waiting for TDMA slot assignment.\n");
    
    // create thread for receiving messages
    if (pthread_create(&recv_thread, NULL, receive_messages, NULL) != 0) {
        printf("Failed to create receive thread\n");
        close(sock);
        return -1;
    }
    
    // create thread for transmitting messages
    if (pthread_create(&tx_thread, NULL, transmit_messages, NULL) != 0) {
        printf("Failed to create transmit thread\n");
        close(sock);
        return -1;
    }
    
    // Wait for initial TDMA info
    sleep(1);
    
    //execute flood test mode function
    if (client_mode == MODE_TEST) {
        //  test message generator thread
        if (pthread_create(&test_gen_thread, NULL, test_message_generator, NULL) != 0) {
            printf("Failed to create test generator thread\n");
            close(sock);
            return -1;
        }
        
        //  statistics reporter thread
        if (pthread_create(&stats_thread, NULL, statistics_reporter, NULL) != 0) {
            printf("Failed to create statistics thread\n");
            close(sock);
            return -1;
        }
        
        // test mode, wait for Ctrl+C to exit
        pthread_join(test_gen_thread, NULL);
        pthread_join(stats_thread, NULL);
    } else {
        // Client-client interactive mode
        printf("\n=== TDMA Client Ready ===\n");
        printf("Type 'status' to see TDMA status\n");
        printf("Type your message and press Enter to queue it\n");
        printf("Messages will be sent automatically during your time slot\n");
        printf("Press Ctrl+C to exit\n\n");
        
        // Main loop for queuing messages
        while (running) {
            printf(">> ");
            fflush(stdout);
            
            if (fgets(buffer, BUFFER_SIZE, stdin) == NULL) {
                break;
            }
            
            if (!running) break;
            
            // Remove trailing newline
            size_t len = strlen(buffer);
            if (len > 0 && buffer[len - 1] == '\n') {
                buffer[len - 1] = '\0';
            }
            
            // Skip empty messages
            if (strlen(buffer) == 0) {
                continue;
            }
            
            // Check for status command
            if (strcmp(buffer, "status") == 0) {
                display_status();
                continue;
            }
            
            // Queue the message for transmission
            if (enqueue_message(buffer)) {
                //Buffer not full, notify user of message queing 
                pthread_mutex_lock(&tdma_info.lock);
                if (tdma_info.my_turn) {
                    printf("Message queued and will be sent immediately (in your slot)\n");
                } else {
                    printf("Message queued. Will be sent when your slot becomes active (Slot %d)\n", 
                           tdma_info.my_slot);
                }
                pthread_mutex_unlock(&tdma_info.lock);
            } else {
                //Buffer full, warn user -- potential loss of data
                printf("Message queue full! Please wait.\n");
            }
        }
    }
    
    // Cleanup
    running = 0;
    pthread_join(recv_thread, NULL);
    pthread_join(tx_thread, NULL);
    pthread_mutex_destroy(&msg_queue.lock);
    pthread_mutex_destroy(&tdma_info.lock);
    pthread_mutex_destroy(&test_stats.lock);
    close(sock);
    
    return 0;
}

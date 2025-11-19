#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>

#define PORT 8080
#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024
#define SLOT_DURATION_MS 100  // 100 milliseconds per time slot

// structure to stroe client data
typedef struct {
    int socket;
    struct sockaddr_in address;
    int active;
    int slot_number;  // TDMA slot assignment
} Client;

// structure to see how many clients we have to split
typedef struct {
    int frame_number;
    int current_slot;
    long long frame_start_time;
    int active_slots;  // Number of slots currently in use
} TDMAScheduler;

Client clients[MAX_CLIENTS];
int client_count = 0;
TDMAScheduler tdma;

// Get current time in milliseconds
long long get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)(tv.tv_sec) * 1000 + (tv.tv_usec) / 1000;
}

void initialize_tdma() {
    tdma.frame_number = 0;
    tdma.current_slot = 0;
    tdma.frame_start_time = get_time_ms();
    tdma.active_slots = 1;  // Start with at least 1 slot to avoid division by zero
}

// update tdma slots fro scalability
void update_tdma_slot() {
    long long current_time = get_time_ms();
    long long elapsed = current_time - tdma.frame_start_time;
    
    // Calculate frame duration based on number of active slots
    int frame_duration = tdma.active_slots * SLOT_DURATION_MS;
    
    // Calculate current slot based on elapsed time
    int new_slot = (elapsed / SLOT_DURATION_MS) % tdma.active_slots;
    
    // Check if we've moved to a new frame
    if (elapsed >= frame_duration) {
        tdma.frame_number++;
        tdma.frame_start_time = current_time;
        tdma.current_slot = 0;
        // Removed repetitive frame start message
    } else if (new_slot != tdma.current_slot) {
        tdma.current_slot = new_slot;
        // Removed repetitive slot active message
    }
}

// return client index whose slot matches the active slot
int get_current_active_client() {
    // Find which client has the current slot
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].slot_number == tdma.current_slot) {
            return i;
        }
    }
    return -1;
}

// update teh number of tdma slot acording to clients
void update_active_slots() {
    // Count the number of active clients to determine active slots
    int count = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            count++;
        }
    }
    tdma.active_slots = (count > 0) ? count : 1;  // Minimum 1 slot
    printf("[TDMA] Active slots updated: %d\n", tdma.active_slots);
}

// return time remaining until next TDMA slot
long long get_time_until_next_slot() {
    long long current_time = get_time_ms();
    long long elapsed = current_time - tdma.frame_start_time;
    long long slot_elapsed = elapsed % SLOT_DURATION_MS;
    return SLOT_DURATION_MS - slot_elapsed;
}

// return time until clients slot becomes active
long long get_time_to_client_slot(int client_index) {
    if (client_index < 0 || client_index >= MAX_CLIENTS || !clients[client_index].active) {
        return 0;
    }
    
    int slot = clients[client_index].slot_number;
    
    if (slot == tdma.current_slot) {
        return get_time_until_next_slot();
    } else if (slot > tdma.current_slot) {
        return (slot - tdma.current_slot) * SLOT_DURATION_MS;
    } else {
        return (tdma.active_slots - tdma.current_slot + slot) * SLOT_DURATION_MS;
    }
}

void initialize_clients() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].socket = -1;
        clients[i].active = 0;
        clients[i].slot_number = -1;
    }
}

// add a new client and assign time slot
int add_client(int socket, struct sockaddr_in address) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) {
            clients[i].socket = socket;
            clients[i].address = address;
            clients[i].active = 1;
            clients[i].slot_number = i;  // Assign slot based on index
            client_count++;
            update_active_slots();  // Update TDMA frame based on new client count
            return i;
        }
    }
    return -1;
}

// cleen up client entry when disconnected
void remove_client(int index) {
    if (clients[index].active) {
        close(clients[index].socket);
        clients[index].socket = -1;
        clients[index].active = 0;
        clients[index].slot_number = -1;
        client_count--;
        update_active_slots();  // Update TDMA frame based on new client count
    }
}

// send timing info to a specific client
void send_tdma_info_to_client(int client_index) {
    char tdma_msg[BUFFER_SIZE];
    long long time_to_slot = get_time_to_client_slot(client_index);
    
    snprintf(tdma_msg, sizeof(tdma_msg),
             "TDMA_INFO|slot=%d|slot_duration=%d|frame=%d|time_to_slot=%lld|active_slots=%d\n",
             clients[client_index].slot_number,
             SLOT_DURATION_MS,
             tdma.frame_number,
             time_to_slot,
             tdma.active_slots);
    
    send(clients[client_index].socket, tdma_msg, strlen(tdma_msg), 0);
}

// inform client when their turn
void broadcast_slot_change() {
    char slot_msg[BUFFER_SIZE];
    int active_client = get_current_active_client();
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            if (i == active_client) {
                snprintf(slot_msg, sizeof(slot_msg), 
                        "SLOT_ACTIVE|your_turn=1|slot=%d|duration=%d|active_slots=%d\n",
                        tdma.current_slot, SLOT_DURATION_MS, tdma.active_slots);
            } else {
                long long time_to_slot = get_time_to_client_slot(i);
                
                snprintf(slot_msg, sizeof(slot_msg),
                        "SLOT_ACTIVE|your_turn=0|current_slot=%d|your_slot=%d|wait_time=%lld|active_slots=%d\n",
                        tdma.current_slot, clients[i].slot_number, time_to_slot, tdma.active_slots);
            }
            send(clients[i].socket, slot_msg, strlen(slot_msg), 0);
        }
    }
}

// THIS IS A FUNCTION that sends message from one client to others
void broadcast_message(const char *message, int sender_index) {
    char formatted_msg[BUFFER_SIZE + 50];
    snprintf(formatted_msg, sizeof(formatted_msg), 
             "MESSAGE|from=%d|slot=%d|text=%s\n",
             sender_index + 1, clients[sender_index].slot_number, message);
    
    printf("Broadcasting from Client %d (Slot %d): %s\n", 
           sender_index + 1, clients[sender_index].slot_number, message);
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && i != sender_index) {
            if (send(clients[i].socket, formatted_msg, strlen(formatted_msg), 0) < 0) {
                printf("Failed to send to client %d\n", i + 1);
            }
        }
    }
}

int main() {
    int server_socket, new_socket, max_sd, activity;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    fd_set read_fds;
    struct timeval timeout;
    char buffer[BUFFER_SIZE];
    
    initialize_clients();
    initialize_tdma();
    
    // Create socket
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    // Set socket options to reuse address
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }
    
    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    // Bind socket
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    
    // Listen for connections
    if (listen(server_socket, 3) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    
    printf("=== TDMA Server Started ===\n");
    printf("Port: %d\n", PORT);
    printf("Slot Duration: %d ms\n", SLOT_DURATION_MS);
    printf("Dynamic frame sizing enabled\n");
    printf("Waiting for client connections...\n\n");
    
    while (1) {
        // Update TDMA scheduling
        int prev_slot = tdma.current_slot;
        update_tdma_slot();
        
        // Notify clients when slot changes
        if (prev_slot != tdma.current_slot && prev_slot != -1) {
            broadcast_slot_change();
        }
        
        // Clear the socket set
        FD_ZERO(&read_fds);
        
        // Add server socket to set
        FD_SET(server_socket, &read_fds);
        max_sd = server_socket;
        
        // Add client sockets to set
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = clients[i].socket;
            
            if (clients[i].active) {
                FD_SET(sd, &read_fds);
            }
            
            if (sd > max_sd) {
                max_sd = sd;
            }
        }
        
        // Set timeout for select (10mS to check TDMA timing frequently to prevent drift from 100mS)
        timeout.tv_sec = 0;
        timeout.tv_usec = 10000;  // 10mS
        
        // Wait for activity on sockets
        activity = select(max_sd + 1, &read_fds, NULL, NULL, &timeout);
        
        if ((activity < 0) && (errno != EINTR)) {
            printf("Select error\n");
        }
        
        // Check for new connection
        if (FD_ISSET(server_socket, &read_fds)) {
            if ((new_socket = accept(server_socket, (struct sockaddr *)&client_addr, &addr_len)) < 0) {
                perror("Accept failed");
                continue;
            }
            
            printf("New connection from %s:%d\n", 
                   inet_ntoa(client_addr.sin_addr), 
                   ntohs(client_addr.sin_port));
            
            int client_index = add_client(new_socket, client_addr);
            if (client_index >= 0) {
                printf("Client %d connected and assigned to Slot %d. Total clients: %d\n", 
                       client_index + 1, clients[client_index].slot_number, client_count);
                
                // Send welcome message with TDMA info
                char welcome[200];
                snprintf(welcome, sizeof(welcome), 
                        "WELCOME|client_id=%d|slot=%d|slot_duration=%d\n",
                        client_index + 1, 
                        clients[client_index].slot_number,
                        SLOT_DURATION_MS);
                send(new_socket, welcome, strlen(welcome), 0);
                
                // Send initial TDMA timing info
                send_tdma_info_to_client(client_index);
            } else {
                printf("Maximum clients reached. Connection rejected.\n");
                close(new_socket);
            }
        }
        
        // Check for I/O operation on client sockets
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = clients[i].socket;
            
            if (clients[i].active && FD_ISSET(sd, &read_fds)) {
                memset(buffer, 0, BUFFER_SIZE);
                int valread = read(sd, buffer, BUFFER_SIZE - 1);
                
                if (valread <= 0) {
                    // Client disconnected
                    getpeername(sd, (struct sockaddr *)&client_addr, &addr_len);
                    printf("Client %d (Slot %d) disconnected from %s:%d\n", 
                           i + 1,
                           clients[i].slot_number,
                           inet_ntoa(client_addr.sin_addr),
                           ntohs(client_addr.sin_port));
                    
                    remove_client(i);
                    printf("Total clients: %d\n", client_count);
                } else {
                    buffer[valread] = '\0';
                    
                    // Check if client is transmitting in their assigned slot
                    if (clients[i].slot_number == tdma.current_slot) {
                        // Client is in their slot - allow transmission
                        broadcast_message(buffer, i);
                    } else {
                        // Client is transmitting outside their slot - collision detected
                        char error_msg[200];
                        snprintf(error_msg, sizeof(error_msg),
                                "COLLISION|your_slot=%d|current_slot=%d|message_dropped\n",
                                clients[i].slot_number, tdma.current_slot);
                        send(sd, error_msg, strlen(error_msg), 0);
                        
                        printf("[COLLISION] Client %d attempted transmission in Slot %d (assigned Slot %d)\n",
                               i + 1, tdma.current_slot, clients[i].slot_number);
                    }
                }
            }
        }
    }
    
    close(server_socket);
    return 0;
}

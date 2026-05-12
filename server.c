#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>
#include <ctype.h>

#define PORT 0
#define MAX_BUFFER 4096
#define RATING_FILE "ratings.txt"

typedef struct {
    int board[3][3];
    int score;
} Player;

typedef struct {
    struct sockaddr_in addr;
    int active;
    char username[50];
    int rating;
} Client;

typedef struct {
    char username[50];
    char password[50];
    int wins;
    int losses;
    int draws;
    int rating;
} User;

Client clients[2];
Player players[2];
int current_player = 0;
int game_started = 0;
int server_socket;
int game_port;
User users[100];
int user_count = 0;

void die(const char *msg) {
    perror(msg);
    exit(1);
}

void load_users() {
    FILE *f = fopen(RATING_FILE, "r");
    if (!f) {
        printf("No rating file found. Creating new one.\n");
        return;
    }
    
    user_count = 0;
    while (fscanf(f, "%49s %49s %d %d %d %d", 
                  users[user_count].username,
                  users[user_count].password,
                  &users[user_count].wins,
                  &users[user_count].losses,
                  &users[user_count].draws,
                  &users[user_count].rating) == 6) {
        user_count++;
        if (user_count >= 100) break;
    }
    fclose(f);
    printf("Loaded %d users from rating file\n", user_count);
}

void save_users() {
    FILE *f = fopen(RATING_FILE, "w");
    if (!f) {
        printf("Error saving ratings!\n");
        return;
    }
    
    for (int i = 0; i < user_count; i++) {
        fprintf(f, "%s %s %d %d %d %d\n",
                users[i].username,
                users[i].password,
                users[i].wins,
                users[i].losses,
                users[i].draws,
                users[i].rating);
    }
    fclose(f);
}

User* find_user(const char *username) {
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].username, username) == 0) {
            return &users[i];
        }
    }
    return NULL;
}

void add_user(const char *username, const char *password) {
    if (user_count >= 100) return;
    strcpy(users[user_count].username, username);
    strcpy(users[user_count].password, password);
    users[user_count].wins = 0;
    users[user_count].losses = 0;
    users[user_count].draws = 0;
    users[user_count].rating = 1000;
    user_count++;
    save_users();
    printf("New user registered: %s\n", username);
}

void update_rating(int winner, int loser, int is_draw) {
    if (is_draw) {
        User *u1 = find_user(clients[0].username);
        User *u2 = find_user(clients[1].username);
        if (u1) { u1->draws++; }
        if (u2) { u2->draws++; }
        printf("Draw game!\n");
    } else {
        User *winner_u = find_user(clients[winner].username);
        User *loser_u = find_user(clients[loser].username);
        if (winner_u) { 
            winner_u->wins++; 
            winner_u->rating += 25;
            printf("%s wins! Rating +25 (now %d)\n", winner_u->username, winner_u->rating);
        }
        if (loser_u) { 
            loser_u->losses++; 
            loser_u->rating -= 25;
            if (loser_u->rating < 0) loser_u->rating = 0;
            printf("%s loses! Rating -25 (now %d)\n", loser_u->username, loser_u->rating);
        }
    }
    save_users();
}

void send_rating_list(int client_idx) {
    char temp[MAX_BUFFER];

    User sorted_users[100];
    memcpy(sorted_users, users, sizeof(User) * user_count);
    
    for (int i = 0; i < user_count - 1; i++) {
        for (int j = 0; j < user_count - i - 1; j++) {
            if (sorted_users[j].rating < sorted_users[j + 1].rating) {
                User tmp = sorted_users[j];
                sorted_users[j] = sorted_users[j + 1];
                sorted_users[j + 1] = tmp;
            }
        }
    }
    
    sprintf(temp, "RATING_LIST %d", user_count);
    sendto(server_socket, temp, strlen(temp), 0,
           (struct sockaddr*)&clients[client_idx].addr, sizeof(clients[client_idx].addr));
    usleep(10000);
    
    for (int i = 0; i < user_count && i < 20; i++) {
        sprintf(temp, "RATING_ENTRY %d %s %d %d %d %d", 
                i + 1,
                sorted_users[i].username,
                sorted_users[i].rating,
                sorted_users[i].wins,
                sorted_users[i].losses,
                sorted_users[i].draws);
        sendto(server_socket, temp, strlen(temp), 0,
               (struct sockaddr*)&clients[client_idx].addr, sizeof(clients[client_idx].addr));
        usleep(5000);
    }
    
    sendto(server_socket, "RATING_END", 10, 0,
           (struct sockaddr*)&clients[client_idx].addr, sizeof(clients[client_idx].addr));
    printf("Rating list sent to client %d\n", client_idx);
}

int calculate_score(int board[3][3]) {
    int total = 0;
    for (int col = 0; col < 3; col++) {
        int values[3];
        int count = 0;
        for (int row = 0; row < 3; row++) {
            if (board[col][row] != 0) {
                values[count++] = board[col][row];
            }
        }
        if (count == 0) continue;
        
        int sum = 0;
        int freq[7] = {0};
        for (int i = 0; i < count; i++) {
            sum += values[i];
            freq[values[i]]++;
        }
        
        int has_two = 0, has_three = 0;
        for (int v = 1; v <= 6; v++) {
            if (freq[v] == 2) has_two = 1;
            if (freq[v] == 3) has_three = 1;
        }
        
        int multiplier = 1;
        if (has_three) multiplier = 5;
        else if (has_two) multiplier = 3;
        
        total += sum * multiplier;
    }
    return total;
}

void apply_gravity(int board[3][3]) {
    for (int col = 0; col < 3; col++) {
        int temp[3] = {0, 0, 0};
        int idx = 2;
        for (int row = 2; row >= 0; row--) {
            if (board[col][row] != 0) {
                temp[idx--] = board[col][row];
            }
        }
        for (int row = 0; row < 3; row++) {
            board[col][row] = temp[row];
        }
    }
}

void destroy_opposite(Player *current, Player *opponent, int col, int value) {
    int destroyed = 0;
    for (int row = 0; row < 3; row++) {
        if (opponent->board[col][row] == value) {
            opponent->board[col][row] = 0;
            destroyed = 1;
        }
    }
    if (destroyed) {
        apply_gravity(opponent->board);
        printf("Destroyed value %d in opponent's column %d\n", value, col);
    }
}

int can_place(Player *p, int col) {
    for (int row = 0; row < 3; row++) {
        if (p->board[col][row] == 0) return 1;
    }
    return 0;
}

void place_die(Player *p, int col, int value) {
    for (int row = 2; row >= 0; row--) {
        if (p->board[col][row] == 0) {
            p->board[col][row] = value;
            printf("Placed %d at column %d, row %d\n", value, col, row);
            break;
        }
    }
}

void send_to_client(int client_idx, const char *msg) {
    sendto(server_socket, msg, strlen(msg), 0,
           (struct sockaddr*)&clients[client_idx].addr, sizeof(clients[client_idx].addr));
}

void broadcast(const char *msg, int exclude) {
    for (int i = 0; i < 2; i++) {
        if (clients[i].active && i != exclude) {
            send_to_client(i, msg);
        }
    }
}

void send_state() {
    char state_msg[MAX_BUFFER];
    sprintf(state_msg, "STATE %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %s %d %s %d",
            players[0].board[0][0], players[0].board[0][1], players[0].board[0][2],
            players[0].board[1][0], players[0].board[1][1], players[0].board[1][2],
            players[0].board[2][0], players[0].board[2][1], players[0].board[2][2],
            players[1].board[0][0], players[1].board[0][1], players[1].board[0][2],
            players[1].board[1][0], players[1].board[1][1], players[1].board[1][2],
            players[1].board[2][0], players[1].board[2][1], players[1].board[2][2],
            players[0].score, players[1].score, current_player,
            clients[0].username, clients[0].rating,
            clients[1].username, clients[1].rating);
    broadcast(state_msg, -1);
}

void send_state_to_client(int client_idx) {
    char state_msg[MAX_BUFFER];
    sprintf(state_msg, "STATE %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %s %d %s %d",
            players[0].board[0][0], players[0].board[0][1], players[0].board[0][2],
            players[0].board[1][0], players[0].board[1][1], players[0].board[1][2],
            players[0].board[2][0], players[0].board[2][1], players[0].board[2][2],
            players[1].board[0][0], players[1].board[0][1], players[1].board[0][2],
            players[1].board[1][0], players[1].board[1][1], players[1].board[1][2],
            players[1].board[2][0], players[1].board[2][1], players[1].board[2][2],
            players[0].score, players[1].score, current_player,
            clients[0].username, clients[0].rating,
            clients[1].username, clients[1].rating);
    send_to_client(client_idx, state_msg);
}

void game_over() {
    int s1 = calculate_score(players[0].board);
    int s2 = calculate_score(players[1].board);
    
    int winner = -1;
    int is_draw = 0;
    
    if (s1 > s2) winner = 0;
    else if (s2 > s1) winner = 1;
    else is_draw = 1;
    
    update_rating(winner, 1 - winner, is_draw);
    
    char result[MAX_BUFFER];
    sprintf(result, "GAME_OVER %d %d %s %s", s1, s2, clients[winner].username, is_draw ? "draw" : "");
    broadcast(result, -1);
    
    printf("Game over! Score: %d - %d\n", s1, s2);
    
    game_started = 0;
    memset(players, 0, sizeof(players));
    for (int i = 0; i < 2; i++) {
        clients[i].active = 0;
        memset(clients[i].username, 0, 50);
        clients[i].rating = 0;
    }
}

void make_move(int player_idx, int col, int dice_value) {
    if (player_idx != current_player) {
        send_to_client(player_idx, "ERROR Not your turn");
        return;
    }
    
    if (col < 0 || col > 2) {
        send_to_client(player_idx, "ERROR Invalid column");
        return;
    }
    
    if (!can_place(&players[player_idx], col)) {
        send_to_client(player_idx, "ERROR Column full");
        return;
    }
    
    destroy_opposite(&players[player_idx], &players[1 - player_idx], col, dice_value);
    place_die(&players[player_idx], col, dice_value);
    apply_gravity(players[player_idx].board);
    
    players[player_idx].score = calculate_score(players[player_idx].board);
    players[1 - player_idx].score = calculate_score(players[1 - player_idx].board);
    
    current_player = 1 - current_player;
    
    send_state();
    
    int full0 = 1, full1 = 1;
    for (int c = 0; c < 3; c++) {
        if (can_place(&players[0], c)) full0 = 0;
        if (can_place(&players[1], c)) full1 = 0;
    }
    if (full0 || full1) {
        game_over();
    }
}

void handle_dice_roll(int player_idx) {
    if (player_idx != current_player) {
        send_to_client(player_idx, "ERROR Not your turn");
        return;
    }
    int dice = (rand() % 6) + 1;
    char msg[MAX_BUFFER];
    sprintf(msg, "DICE %d", dice);
    send_to_client(player_idx, msg);
}

void parse_command(int client_idx, char *cmd) {
    char cmd_copy[MAX_BUFFER];
    strcpy(cmd_copy, cmd);
    char *token = strtok(cmd_copy, " \n\r");
    if (!token) return;
    
    if (strcmp(token, "ROLL") == 0) {
        handle_dice_roll(client_idx);
    }
    else if (strcmp(token, "PLACE") == 0) {
        int col, value;
        token = strtok(NULL, " ");
        if (token) col = atoi(token);
        token = strtok(NULL, " ");
        if (token) value = atoi(token);
        make_move(client_idx, col, value);
    }
}

void start_game() {
    if (clients[0].active && clients[1].active && !game_started && 
        strlen(clients[0].username) > 0 && strlen(clients[1].username) > 0) {
        game_started = 1;
        current_player = 0;
        memset(players, 0, sizeof(players));
        players[0].score = 0;
        players[1].score = 0;
        
        char start_msg[MAX_BUFFER];
        sprintf(start_msg, "START %s %d %s %d", 
                clients[0].username, clients[0].rating,
                clients[1].username, clients[1].rating);
        broadcast(start_msg, -1);
        send_state();
        
        printf("=== GAME STARTED ===\n");
        printf("%s (Rating: %d) vs %s (Rating: %d)\n", 
               clients[0].username, clients[0].rating,
               clients[1].username, clients[1].rating);
    }
}

int main() {
    srand(time(NULL));
    load_users();
    
    server_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_socket < 0) die("socket");
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
        die("bind");
    
    socklen_t len = sizeof(server_addr);
    getsockname(server_socket, (struct sockaddr*)&server_addr, &len);
    game_port = ntohs(server_addr.sin_port);
    printf("\n========================================\n");
    printf("Server started on UDP port %d\n", game_port);
    printf("Waiting for players...\n");
    printf("========================================\n\n");
    
    fd_set readfds;
    struct timeval tv;
    char buffer[MAX_BUFFER];
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    while (1) {
        FD_ZERO(&readfds);
        FD_SET(server_socket, &readfds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        if (select(server_socket + 1, &readfds, NULL, NULL, &tv) > 0) {
            memset(buffer, 0, MAX_BUFFER);
            int n = recvfrom(server_socket, buffer, MAX_BUFFER - 1, 0, 
                           (struct sockaddr*)&client_addr, &addr_len);
            if (n > 0) {
                buffer[n] = '\0';
                buffer[strcspn(buffer, "\n")] = 0;
                
                int idx = -1;
                for (int i = 0; i < 2; i++) {
                    if (clients[i].active && 
                        clients[i].addr.sin_addr.s_addr == client_addr.sin_addr.s_addr &&
                        clients[i].addr.sin_port == client_addr.sin_port) {
                        idx = i;
                        break;
                    }
                }

                if (idx == -1 && !game_started && strncmp(buffer, "LOGIN", 5) == 0) {
                    char username[50], password[50];
                    sscanf(buffer, "LOGIN %s %s", username, password);
                    
                    User *u = find_user(username);
                    if (u && strcmp(u->password, password) == 0) {
                        for (int i = 0; i < 2; i++) {
                            if (!clients[i].active) {
                                clients[i].addr = client_addr;
                                clients[i].active = 1;
                                strcpy(clients[i].username, username);
                                clients[i].rating = u->rating;
                                printf("Player %d logged in as %s (rating: %d)\n", i+1, username, u->rating);
                                send_to_client(i, "LOGIN_OK");
                                
                                if (game_started) {
                                    char start_msg[MAX_BUFFER];
                                    sprintf(start_msg, "START %s %d %s %d", 
                                            clients[0].username, clients[0].rating,
                                            clients[1].username, clients[1].rating);
                                    send_to_client(i, start_msg);
                                    send_state_to_client(i);
                                }
                                break;
                            }
                        }
                    } else if (!u) {
                        add_user(username, password);
                        for (int i = 0; i < 2; i++) {
                            if (!clients[i].active) {
                                clients[i].addr = client_addr;
                                clients[i].active = 1;
                                strcpy(clients[i].username, username);
                                clients[i].rating = 1000;
                                printf("Player %d registered as %s (rating: 1000)\n", i+1, username);
                                send_to_client(i, "LOGIN_OK");
                                
                                if (game_started) {
                                    char start_msg[MAX_BUFFER];
                                    sprintf(start_msg, "START %s %d %s %d", 
                                            clients[0].username, clients[0].rating,
                                            clients[1].username, clients[1].rating);
                                    send_to_client(i, start_msg);
                                    send_state_to_client(i);
                                }
                                break;
                            }
                        }
                    } else {
                        sendto(server_socket, "LOGIN_FAIL", 10, 0,
                               (struct sockaddr*)&client_addr, addr_len);
                    }
                }

                if (idx != -1 && strncmp(buffer, "RATING", 6) == 0) {
                    send_rating_list(idx);
                } else {
                    start_game();
                    if (idx != -1 && game_started) {
                        parse_command(idx, buffer);
                    }
                }
            }
        } else {
            start_game();
        }
    }
    
    close(server_socket);
    return 0;
}
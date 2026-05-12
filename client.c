#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <termios.h>

#define MAX_BUFFER 4096

int sock;
struct sockaddr_in server_addr;
char my_username[50];
int my_rating = 0;
char opponent_username[50];
int opponent_rating = 0;
int my_player_id = -1;

void print_board(int board[3][3], int score, const char* title) {
    printf("\n=== %s (Score: %d) ===\n", title, score);
    for (int row = 0; row < 3; row++) {
        printf("  ");
        for (int col = 0; col < 3; col++) {
            if (board[col][row] == 0) printf(". ");
            else printf("%d ", board[col][row]);
        }
        printf("\n");
    }
    if (strcmp(title, my_username) == 0) {
        printf("  Columns: 0 1 2\n");
    }
}

void show_help() {
    printf("\n\033[36m=== COMMANDS ===\033[0m\n");
    printf("  \033[32mENTER\033[0m      - Roll dice (when it's your turn)\n");
    printf("  \033[32mrating\033[0m     - Show global rating table\n");
    printf("  \033[32mhelp\033[0m       - Show this help\n");
    printf("  \033[32mquit\033[0m       - Exit game\n");
    printf("\n");
}

void get_password(char *password, int max_len) {
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    
    fgets(password, max_len, stdin);
    password[strcspn(password, "\n")] = 0;
    
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <server_ip> <port>\n", argv[0]);
        return 1;
    }
    
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[2]));
    inet_pton(AF_INET, argv[1], &server_addr.sin_addr);
    
    char buffer[MAX_BUFFER];
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    
    printf("=== DICE GAME ===\n\n");
    
    char username[50], password[50];
    printf("Username: ");
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = 0;
    
    printf("Password: ");
    get_password(password, sizeof(password));
    printf("\n");
    
    sprintf(buffer, "LOGIN %s %s", username, password);
    sendto(sock, buffer, strlen(buffer), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
    
    int n = recvfrom(sock, buffer, MAX_BUFFER - 1, 0, (struct sockaddr*)&from, &fromlen);
    buffer[n] = '\0';
    
    if (strcmp(buffer, "LOGIN_OK") != 0) {
        printf("Login failed!\n");
        return 1;
    }
    
    strcpy(my_username, username);
    printf("Login successful! Welcome, %s!\n", username);
    printf("Waiting for opponent...\n");
    show_help();
    
    int game_started = 0;
    int my_board[3][3] = {0};
    int opp_board[3][3] = {0};
    int my_score = 0, opp_score = 0;
    int current_turn = 0;
    int last_dice = 0;
    int waiting_for_roll = 0;
    int waiting_for_column = 0;
    int waiting_for_response = 0;
    
    struct timeval tv;
    fd_set readfds;
    
    printf("\nPress ENTER to roll dice when it's your turn!\n");
    fflush(stdout);
    
    while (1) {
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        FD_SET(STDIN_FILENO, &readfds);
        
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100 мс
        
        int maxfd = (sock > STDIN_FILENO) ? sock : STDIN_FILENO;
        
        if (select(maxfd + 1, &readfds, NULL, NULL, &tv) > 0) {
            if (FD_ISSET(sock, &readfds)) {
                n = recvfrom(sock, buffer, MAX_BUFFER - 1, 0, 
                           (struct sockaddr*)&from, &fromlen);
                if (n < 0) {
                    perror("recvfrom");
                    break;
                }
                buffer[n] = '\0';

                if (strncmp(buffer, "START", 5) == 0) {
                    char p1_name[50], p2_name[50];
                    int p1_rating, p2_rating;
                    sscanf(buffer, "START %s %d %s %d", 
                           p1_name, &p1_rating, p2_name, &p2_rating);
                    
                    if (strcmp(my_username, p1_name) == 0) {
                        my_player_id = 0;
                        strcpy(opponent_username, p2_name);
                        opponent_rating = p2_rating;
                        my_rating = p1_rating;
                    } else {
                        my_player_id = 1;
                        strcpy(opponent_username, p1_name);
                        opponent_rating = p1_rating;
                        my_rating = p2_rating;
                    }
                    
                    game_started = 1;
                    printf("\n=== GAME STARTED ===\n");
                    printf("%s (Rating: %d) vs %s (Rating: %d)\n\n", 
                           my_username, my_rating, opponent_username, opponent_rating);
                    fflush(stdout);
                }
                else if (strncmp(buffer, "STATE", 5) == 0) {
                    int p1_board[3][3], p2_board[3][3];
                    int p1_score, p2_score;
                    char p1_name[50], p2_name[50];
                    int p1_rating, p2_rating;
                    
                    sscanf(buffer, "STATE %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %s %d %s %d",
                           &p1_board[0][0], &p1_board[0][1], &p1_board[0][2],
                           &p1_board[1][0], &p1_board[1][1], &p1_board[1][2],
                           &p1_board[2][0], &p1_board[2][1], &p1_board[2][2],
                           &p2_board[0][0], &p2_board[0][1], &p2_board[0][2],
                           &p2_board[1][0], &p2_board[1][1], &p2_board[1][2],
                           &p2_board[2][0], &p2_board[2][1], &p2_board[2][2],
                           &p1_score, &p2_score, &current_turn,
                           p1_name, &p1_rating, p2_name, &p2_rating);
                    
                    if (my_player_id == 0) {
                        memcpy(my_board, p1_board, sizeof(my_board));
                        memcpy(opp_board, p2_board, sizeof(opp_board));
                        my_score = p1_score;
                        opp_score = p2_score;
                    } else {
                        memcpy(my_board, p2_board, sizeof(my_board));
                        memcpy(opp_board, p1_board, sizeof(opp_board));
                        my_score = p2_score;
                        opp_score = p1_score;
                    }
                    
                    printf("\033[2J\033[H"); // Очистка экрана
                    printf("\n\033[33m%s (Rating: %d) vs %s (Rating: %d)\033[0m\n\n", 
                           opponent_username, opponent_rating, my_username, my_rating);
                    print_board(opp_board, opp_score, opponent_username);
                    print_board(my_board, my_score, my_username);
                    
                    if (game_started) {
                        if (current_turn == my_player_id) {
                            printf("\n\033[32m>>> YOUR TURN! Press ENTER to roll dice <<<\033[0m\n");
                            waiting_for_roll = 1;
                            waiting_for_column = 0;
                        } else {
                            printf("\n\033[33m>>> Waiting for opponent... <<<\033[0m\n");
                            waiting_for_roll = 0;
                            waiting_for_column = 0;
                        }
                    }
                    fflush(stdout);
                }
                else if (strncmp(buffer, "DICE", 4) == 0) {
                    sscanf(buffer, "DICE %d", &last_dice);
                    printf("\n\033[32mYou rolled: %d\033[0m\n", last_dice);
                    printf("Enter column (0-2): ");
                    fflush(stdout);
                    waiting_for_roll = 0;
                    waiting_for_column = 1;
                }
                else if (strncmp(buffer, "RATING_LIST", 11) == 0) {
                    int count;
                    sscanf(buffer, "RATING_LIST %d", &count);
                    printf("\n\033[36m========== GLOBAL RATING TABLE ==========\033[0m\n");
                    printf("\033[36mRank  Username              Rating  Wins Losses Draws\033[0m\n");
                    printf("\033[36m----  --------------------  ------  ---- ------ -----\033[0m\n");
                    fflush(stdout);

                else if (strncmp(buffer, "RATING_ENTRY", 12) == 0) {
                    int rank, rating, wins, losses, draws;
                    char uname[50];
                    sscanf(buffer, "RATING_ENTRY %d %s %d %d %d %d", 
                           &rank, uname, &rating, &wins, &losses, &draws);
                    printf("%-4d  %-20s %-6d %-4d %-6d %-5d\n", 
                           rank, uname, rating, wins, losses, draws);
                    fflush(stdout);
                }
                else if (strcmp(buffer, "RATING_END") == 0) {
                    printf("\033[36m==========================================\033[0m\n\n");
                    waiting_for_response = 0;
                    if (game_started && current_turn == my_player_id) {
                        printf("\033[32m>>> YOUR TURN! Press ENTER to roll dice <<<\033[0m\n");
                        waiting_for_roll = 1;
                    } else if (game_started) {
                        printf("\033[33m>>> Waiting for opponent... <<<\033[0m\n");
                    } else {
                        printf("Press ENTER to roll dice when it's your turn!\n");
                    }
                    fflush(stdout);
                }
                else if (strncmp(buffer, "GAME_OVER", 9) == 0) {
                    int s1, s2;
                    char winner[50], draw[10];
                    sscanf(buffer, "GAME_OVER %d %d %s %s", &s1, &s2, winner, draw);
                    
                    printf("\n\033[33m========== GAME OVER ==========\033[0m\n");
                    
                    if (my_player_id == 0) {
                        printf("Final Score: %d - %d\n", s1, s2);
                    } else {
                        printf("Final Score: %d - %d\n", s2, s1);
                    }
                    
                    if (strcmp(draw, "draw") == 0) {
                        printf("\033[33m>>> DRAW! <<<\033[0m\n");
                    } else if (strcmp(winner, my_username) == 0) {
                        printf("\033[32m>>> YOU WIN! +25 rating <<<\033[0m\n");
                    } else {
                        printf("\033[31m>>> YOU LOSE! -25 rating <<<\033[0m\n");
                    }
                    printf("\nPress Ctrl+C to exit\n");
                    game_started = 0;
                    waiting_for_roll = 0;
                    waiting_for_column = 0;
                    fflush(stdout);
                }
                else if (strncmp(buffer, "ERROR", 5) == 0) {
                    printf("\n\033[31m%s\033[0m\n", buffer);
                    if (strstr(buffer, "Column full") && last_dice > 0) {
                        printf("\033[33mChoose another column (0-2) for your %d: \033[0m", last_dice);
                        fflush(stdout);
                        waiting_for_column = 1;
                    }
                }
            }

            if (FD_ISSET(STDIN_FILENO, &readfds)) {
                char input[100];
                if (fgets(input, sizeof(input), stdin)) {
                    input[strcspn(input, "\n")] = 0;
                    

                    if (strcmp(input, "rating") == 0 || strcmp(input, "RATING") == 0) {
                        sendto(sock, "RATING", 6, 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
                        printf("Requesting rating table...\n");
                        fflush(stdout);
                        waiting_for_response = 1;
                    }

                    else if (strcmp(input, "help") == 0 || strcmp(input, "HELP") == 0) {
                        show_help();
                    }

                    else if (strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0 || strcmp(input, "q") == 0) {
                        printf("Goodbye!\n");
                        break;
                    }

                    else if (strlen(input) == 0) {
                        if (waiting_for_roll && game_started && current_turn == my_player_id) {
                            sendto(sock, "ROLL", 4, 0, 
                                   (struct sockaddr*)&server_addr, sizeof(server_addr));
                            printf("Rolling dice...\n");
                            waiting_for_roll = 0;
                            fflush(stdout);
                        } else if (waiting_for_roll && game_started && current_turn != my_player_id) {
                            printf("\033[31mNot your turn! Waiting for opponent...\033[0m\n");
                            fflush(stdout);
                        }
                    }

                    else if (waiting_for_column && strlen(input) == 1 && input[0] >= '0' && input[0] <= '2') {
                        int col = atoi(input);
                        if (last_dice > 0) {
                            char place_cmd[50];
                            sprintf(place_cmd, "PLACE %d %d", col, last_dice);
                            sendto(sock, place_cmd, strlen(place_cmd), 0,
                                   (struct sockaddr*)&server_addr, sizeof(server_addr));
                            printf("Placing %d in column %d\n", last_dice, col);
                            waiting_for_column = 0;
                            last_dice = 0;
                            fflush(stdout);
                        }
                    }
                }
            }
        }
    }
    
    close(sock);
    return 0;
}
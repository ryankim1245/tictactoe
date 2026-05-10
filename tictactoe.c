#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <mosquitto.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#define BOARD_SIZE 9
#define MQTT_HOST "localhost"
#define MQTT_PORT 1883
#define KEEPALIVE 60

#define TOPIC_X_MOVE "tictactoe/player/x/move"
#define TOPIC_O_MOVE "tictactoe/player/o/move"

#define TOPIC_MODE "tictactoe/control/mode"

#define TOPIC_STATUS "tictactoe/game/status"
#define TOPIC_BOARD  "tictactoe/game/board"
#define TOPIC_TURN   "tictactoe/game/turn"
#define TOPIC_RESULT "tictactoe/game/result"
#define TOPIC_RESET  "tictactoe/control/reset"

char board[BOARD_SIZE];
char currentPlayer = 'X';
int gameOver = 0;
struct mosquitto *mosq = NULL;
pid_t botPid = -1;

void initializeBoard();
void printBoard();
int isValidMove(int position);
int checkWinner(char player);
int checkDraw();
void switchPlayer();
void publishStatus(const char *message);
void publishTurn();
void publishBoard();
void publishResult(const char *message);
void resetGame();
int extractPosition(const char *payload);
void handleMove(char player, int position);
void onConnect(struct mosquitto *mosq, void *obj, int rc);
void onMessage(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg);
void startOnePlayerMode();
void stopBotIfRunning();

int main() {
    int rc;

    initializeBoard();

    mosquitto_lib_init();

    mosq = mosquitto_new("gcp_tictactoe_game", true, NULL);

    if (mosq == NULL) {
        fprintf(stderr, "Error: Could not create Mosquitto client.\n");
        mosquitto_lib_cleanup();
        return 1;
    }

    mosquitto_connect_callback_set(mosq, onConnect);
    mosquitto_message_callback_set(mosq, onMessage);

    rc = mosquitto_connect(mosq, MQTT_HOST, MQTT_PORT, KEEPALIVE);

    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Error: Could not connect to MQTT broker: %s\n", mosquitto_strerror(rc));
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        return 1;
    }

    printf("=================================\n");
    printf("   MQTT Tic-Tac-Toe Game on GCP\n");
    printf("=================================\n");
    printf("Waiting for MQTT moves...\n\n");

    printBoard();
    publishStatus("Game started");
    publishBoard();
    publishTurn();

    mosquitto_loop_forever(mosq, -1, 1);

    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    return 0;
}

void initializeBoard() {
    for (int i = 0; i < BOARD_SIZE; i++) {
        board[i] = '1' + i;
    }

    currentPlayer = 'X';
    gameOver = 0;
}

void printBoard() {
    printf("\n");
    printf(" %c | %c | %c \n", board[0], board[1], board[2]);
    printf("---+---+---\n");
    printf(" %c | %c | %c \n", board[3], board[4], board[5]);
    printf("---+---+---\n");
    printf(" %c | %c | %c \n", board[6], board[7], board[8]);
    printf("\n");
    fflush(stdout);
}

int isValidMove(int position) {
    if (position < 1 || position > 9) {
        return 0;
    }

    char spot = board[position - 1];

    if (spot == 'X' || spot == 'O') {
        return 0;
    }

    return 1;
}

int checkWinner(char player) {
    int winningCombos[8][3] = {
        {0, 1, 2},
        {3, 4, 5},
        {6, 7, 8},
        {0, 3, 6},
        {1, 4, 7},
        {2, 5, 8},
        {0, 4, 8},
        {2, 4, 6}
    };

    for (int i = 0; i < 8; i++) {
        int a = winningCombos[i][0];
        int b = winningCombos[i][1];
        int c = winningCombos[i][2];

        if (board[a] == player && board[b] == player && board[c] == player) {
            return 1;
        }
    }

    return 0;
}

int checkDraw() {
    for (int i = 0; i < BOARD_SIZE; i++) {
        if (board[i] != 'X' && board[i] != 'O') {
            return 0;
        }
    }

    return 1;
}

void switchPlayer() {
    if (currentPlayer == 'X') {
        currentPlayer = 'O';
    } else {
        currentPlayer = 'X';
    }
}

void publishStatus(const char *message) {
    mosquitto_publish(mosq, NULL, TOPIC_STATUS, strlen(message), message, 0, false);
}

void publishTurn() {
    char message[32];
    snprintf(message, sizeof(message), "Player %c", currentPlayer);

    mosquitto_publish(mosq, NULL, TOPIC_TURN, strlen(message), message, 0, false);
}

void publishBoard() {
    char message[64];

    snprintf(
        message,
        sizeof(message),
        "%c %c %c %c %c %c %c %c %c",
        board[0], board[1], board[2],
        board[3], board[4], board[5],
        board[6], board[7], board[8]
    );

    mosquitto_publish(mosq, NULL, TOPIC_BOARD, strlen(message), message, 0, false);
}

void publishResult(const char *message) {
    mosquitto_publish(mosq, NULL, TOPIC_RESULT, strlen(message), message, 0, false);
}

void resetGame() {
    initializeBoard();

    printf("Game reset.\n");
    printBoard();

    publishStatus("Game reset");
    publishBoard();
    publishTurn();
    publishResult("Game in progress");
}

int extractPosition(const char *payload) {
    int position = -1;

    /*
        This supports either:
        5
        or:
        {"position":5}
    */

    const char *positionText = strstr(payload, "position");

    if (positionText != NULL) {
        while (*positionText != '\0') {
            if (isdigit((unsigned char)*positionText)) {
                position = *positionText - '0';
                break;
            }

            positionText++;
        }
    } else {
        sscanf(payload, "%d", &position);
    }

    return position;
}

void handleMove(char player, int position) {
    char statusMessage[128];
    char resultMessage[64];

    if (gameOver) {
        publishStatus("Game is over. Send reset to start again.");
        return;
    }

    if (player != currentPlayer) {
        snprintf(
            statusMessage,
            sizeof(statusMessage),
            "Not Player %c turn",
            player
        );

        printf("%s\n", statusMessage);
        publishStatus(statusMessage);
        return;
    }

    if (!isValidMove(position)) {
        snprintf(
            statusMessage,
            sizeof(statusMessage),
            "Invalid move %d by Player %c",
            position,
            player
        );

        printf("%s\n", statusMessage);
        publishStatus(statusMessage);
        return;
    }

    board[position - 1] = player;

    snprintf(
        statusMessage,
        sizeof(statusMessage),
        "Player %c moved to %d",
        player,
        position
    );

    printf("%s\n", statusMessage);
    printBoard();

    publishStatus(statusMessage);
    publishBoard();

    if (checkWinner(player)) {
        snprintf(resultMessage, sizeof(resultMessage), "Player %c wins", player);

        printf("%s\n", resultMessage);

        publishResult(resultMessage);
        publishStatus(resultMessage);

        gameOver = 1;
        return;
    }

    if (checkDraw()) {
        printf("Game ended in a draw.\n");

        publishResult("Draw");
        publishStatus("Game ended in a draw");

        gameOver = 1;
        return;
    }

    switchPlayer();
    publishTurn();
}

void onConnect(struct mosquitto *mosq, void *obj, int rc) {
    if (rc == 0) {
        printf("Connected to MQTT broker.\n");

        mosquitto_subscribe(mosq, NULL, TOPIC_X_MOVE, 0);
        mosquitto_subscribe(mosq, NULL, TOPIC_O_MOVE, 0);
        mosquitto_subscribe(mosq, NULL, TOPIC_RESET, 0);
        mosquitto_subscribe(mosq, NULL, TOPIC_MODE, 0);

        printf("Subscribed to topics:\n");
        printf("  %s\n", TOPIC_X_MOVE);
        printf("  %s\n", TOPIC_O_MOVE);
        printf("  %s\n", TOPIC_RESET);
    } else {
        printf("Failed to connect to MQTT broker. Return code: %d\n", rc);
    }
}

void onMessage(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg) {
    char payload[128];

    if (msg->payloadlen >= (int)sizeof(payload)) {
        printf("Payload too large. Ignoring message.\n");
        return;
    }

    memcpy(payload, msg->payload, msg->payloadlen);
    payload[msg->payloadlen] = '\0';

    printf("MQTT message received [%s]: %s\n", msg->topic, payload);

    if (strcmp(msg->topic, TOPIC_RESET) == 0) {
        resetGame();
        return;
    }

    if (strcmp(msg->topic, TOPIC_MODE) == 0) {
        if (strcmp(payload, "oneplayer") == 0) {
            resetGame();
            startOnePlayerMode();
            publishStatus("One player mode selected. Bot is Player O.");
        } 
        else if (strcmp(payload, "twoplayer") == 0) {
            stopBotIfRunning();
            resetGame();
            publishStatus("Two player mode selected.");
        } 
        else {
            publishStatus("Unknown game mode.");
        }

        return;
    }

    int position = extractPosition(payload);

    if (strcmp(msg->topic, TOPIC_X_MOVE) == 0) {
        handleMove('X', position);
    } else if (strcmp(msg->topic, TOPIC_O_MOVE) == 0) {
        handleMove('O', position);
    }
}

void startOnePlayerMode() {
    if (botPid > 0) {
        int status;
        pid_t result = waitpid(botPid, &status, WNOHANG);

        if (result == 0) {
            printf("Bot is already running with PID %d.\n", botPid);
            publishStatus("Bot is already running.");
            return;
        }

        botPid = -1;
    }

    botPid = fork();

    if (botPid < 0) {
        perror("fork failed");
        publishStatus("Failed to start bot.");
        return;
    }

    if (botPid == 0) {
        /*
            Child process.
            Update this path if your script has a different name/location.
        */
        execl(
            "/bin/bash",
            "bash",
            "/home/kimpixel70/tictactoe/oneplayer.sh",
            (char *)NULL
        );

        perror("execl failed");
        exit(1);
    }

    printf("Started one player bot with PID %d.\n", botPid);
    publishStatus("One player bot started.");
}

void stopBotIfRunning() {
    if (botPid > 0) {
        int status;
        pid_t result = waitpid(botPid, &status, WNOHANG);

        if (result == 0) {
            printf("Stopping bot with PID %d.\n", botPid);
            kill(botPid, SIGTERM);
            waitpid(botPid, &status, 0);
        }

        botPid = -1;
    }
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <mosquitto.h>
#include <raylib.h>

#define MQTT_PORT 1883
#define KEEPALIVE 60

#define WINDOW_WIDTH 600
#define WINDOW_HEIGHT 700

#define BOARD_X 75
#define BOARD_Y 100
#define CELL_SIZE 150
#define BOARD_SIZE 450

#define IP_BOX_X 100
#define IP_BOX_Y 280
#define IP_BOX_WIDTH 400
#define IP_BOX_HEIGHT 50
#define MAX_IP_LENGTH 63

#define TOPIC_X_MOVE "tictactoe/player/x/move"
#define TOPIC_O_MOVE "tictactoe/player/o/move"

#define TOPIC_MODE "tictactoe/control/mode"

#define TOPIC_STATUS "tictactoe/game/status"
#define TOPIC_BOARD  "tictactoe/game/board"
#define TOPIC_TURN   "tictactoe/game/turn"
#define TOPIC_RESULT "tictactoe/game/result"
#define TOPIC_RESET  "tictactoe/control/reset"

struct mosquitto *mosq = NULL;

char brokerIp[MAX_IP_LENGTH + 1] = "";
char board[9] = {'1','2','3','4','5','6','7','8','9'};
char latestTurn = 'X';
char statusMessage[128] = "Enter GCP IP address";
char resultMessage[128] = "Game in progress";

bool gameOver = false;
bool mqttConnected = false;
bool connectAttempted = false;
bool inputScreen = true;

void onConnect(struct mosquitto *mosq, void *obj, int rc);
void onMessage(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg);

bool connectToMQTT(const char *mqttHost);
void disconnectMQTT();

void parseBoardMessage(const char *message);
char extractTurn(const char *message);
void publishMove(int position);
void publishReset();

void drawInputScreen();
void handleIpInput();
void drawGame();
void drawBoard();
void drawMarks();
void drawStatus();

void publishGameMode(const char *mode);
void drawModeButtons();
bool onePlayerButtonClicked(Vector2 mousePosition);
bool twoPlayerButtonClicked(Vector2 mousePosition);

int getClickedCell(Vector2 mousePosition);
bool resetButtonClicked(Vector2 mousePosition);

int main(void) {
    mosquitto_lib_init();

    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "MQTT Tic-Tac-Toe - Raylib Client");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        if (inputScreen) {
            handleIpInput();

            BeginDrawing();
            ClearBackground(RAYWHITE);
            drawInputScreen();
            EndDrawing();
        } else {
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                Vector2 mouse = GetMousePosition();

                if (onePlayerButtonClicked(mouse)) {
                    publishGameMode("oneplayer");
                }
                else if (twoPlayerButtonClicked(mouse)) {
                    publishGameMode("twoplayer");
                }
                else if (resetButtonClicked(mouse)) {
                    publishReset();
                } 
                else if (!gameOver) {
                    int cell = getClickedCell(mouse);

                    if (cell >= 1 && cell <= 9) {
                        publishMove(cell);
                    }
                }
            }

            BeginDrawing();
            ClearBackground(RAYWHITE);
            drawGame();
            EndDrawing();
        }
    }

    disconnectMQTT();
    mosquitto_lib_cleanup();

    CloseWindow();

    return 0;
}

bool connectToMQTT(const char *mqttHost) {
    if (mosq != NULL) {
        disconnectMQTT();
    }

    mosq = mosquitto_new("raylib_laptop_client", true, NULL);

    if (mosq == NULL) {
        snprintf(statusMessage, sizeof(statusMessage), "Could not create MQTT client");
        mqttConnected = false;
        return false;
    }

    mosquitto_connect_callback_set(mosq, onConnect);
    mosquitto_message_callback_set(mosq, onMessage);

    int rc = mosquitto_connect(mosq, mqttHost, MQTT_PORT, KEEPALIVE);

    if (rc != MOSQ_ERR_SUCCESS) {
        snprintf(
            statusMessage,
            sizeof(statusMessage),
            "MQTT connect failed: %s",
            mosquitto_strerror(rc)
        );

        mosquitto_destroy(mosq);
        mosq = NULL;
        mqttConnected = false;
        return false;
    }

    rc = mosquitto_loop_start(mosq);

    if (rc != MOSQ_ERR_SUCCESS) {
        snprintf(
            statusMessage,
            sizeof(statusMessage),
            "MQTT loop failed: %s",
            mosquitto_strerror(rc)
        );

        mosquitto_destroy(mosq);
        mosq = NULL;
        mqttConnected = false;
        return false;
    }

    connectAttempted = true;
    snprintf(statusMessage, sizeof(statusMessage), "Connecting to %s...", mqttHost);

    return true;
}

void disconnectMQTT() {
    if (mosq != NULL) {
        mosquitto_loop_stop(mosq, true);
        mosquitto_disconnect(mosq);
        mosquitto_destroy(mosq);
        mosq = NULL;
    }

    mqttConnected = false;
}

void onConnect(struct mosquitto *mosq, void *obj, int rc) {
    if (rc == 0) {
        mqttConnected = true;

        mosquitto_subscribe(mosq, NULL, TOPIC_STATUS, 0);
        mosquitto_subscribe(mosq, NULL, TOPIC_BOARD, 0);
        mosquitto_subscribe(mosq, NULL, TOPIC_TURN, 0);
        mosquitto_subscribe(mosq, NULL, TOPIC_RESULT, 0);

        snprintf(statusMessage, sizeof(statusMessage), "Connected to MQTT broker");
    } else {
        mqttConnected = false;
        snprintf(statusMessage, sizeof(statusMessage), "MQTT connection failed: %d", rc);
    }
}

void onMessage(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg) {
    char payload[128];

    if (msg->payloadlen >= (int)sizeof(payload)) {
        return;
    }

    memcpy(payload, msg->payload, msg->payloadlen);
    payload[msg->payloadlen] = '\0';

    printf("MQTT [%s]: %s\n", msg->topic, payload);

    if (strcmp(msg->topic, TOPIC_BOARD) == 0) {
        parseBoardMessage(payload);
    }
    else if (strcmp(msg->topic, TOPIC_TURN) == 0) {
        latestTurn = extractTurn(payload);
    }
    else if (strcmp(msg->topic, TOPIC_STATUS) == 0) {
        snprintf(statusMessage, sizeof(statusMessage), "%s", payload);
    }
    else if (strcmp(msg->topic, TOPIC_RESULT) == 0) {
        snprintf(resultMessage, sizeof(resultMessage), "%s", payload);

        if (strstr(payload, "wins") != NULL || strstr(payload, "Draw") != NULL) {
            gameOver = true;
        } else {
            gameOver = false;
        }
    }
}

void handleIpInput() {
    int key = GetCharPressed();

    while (key > 0) {
        if (
            ((key >= '0' && key <= '9') ||
             key == '.' ||
             key == '-' ||
             key == '_' ||
             (key >= 'A' && key <= 'Z') ||
             (key >= 'a' && key <= 'z')) &&
            strlen(brokerIp) < MAX_IP_LENGTH
        ) {
            int len = strlen(brokerIp);
            brokerIp[len] = (char)key;
            brokerIp[len + 1] = '\0';
        }

        key = GetCharPressed();
    }

    if (IsKeyPressed(KEY_BACKSPACE)) {
        int len = strlen(brokerIp);

        if (len > 0) {
            brokerIp[len - 1] = '\0';
        }
    }

    if (IsKeyPressed(KEY_ENTER)) {
        if (strlen(brokerIp) == 0) {
            snprintf(statusMessage, sizeof(statusMessage), "Please enter an IP address");
            return;
        }

        bool started = connectToMQTT(brokerIp);

        if (started) {
            inputScreen = false;
        }
    }
}

void drawInputScreen() {
    DrawText("tic-tac-toe", 145, 120, 36, BLACK);
    DrawText("enter ip:", 125, 230, 22, DARKGRAY);

    Rectangle inputBox = {IP_BOX_X, IP_BOX_Y, IP_BOX_WIDTH, IP_BOX_HEIGHT};

    DrawRectangleRec(inputBox, LIGHTGRAY);
    DrawRectangleLinesEx(inputBox, 2, BLACK);

    DrawText(brokerIp, IP_BOX_X + 12, IP_BOX_Y + 14, 24, BLACK);

    if (((int)(GetTime() * 2) % 2) == 0) {
        int textWidth = MeasureText(brokerIp, 24);
        DrawText("|", IP_BOX_X + 14 + textWidth, IP_BOX_Y + 12, 28, BLACK);
    }
}

void parseBoardMessage(const char *message) {
    int count = 0;

    for (int i = 0; message[i] != '\0' && count < 9; i++) {
        if (message[i] != ' ') {
            board[count] = message[i];
            count++;
        }
    }
}

char extractTurn(const char *message) {
    if (strchr(message, 'X') != NULL) {
        return 'X';
    }

    if (strchr(message, 'O') != NULL) {
        return 'O';
    }

    return latestTurn;
}

void publishMove(int position) {
    char payload[64];
    const char *topic;

    if (mosq == NULL || !mqttConnected) {
        snprintf(statusMessage, sizeof(statusMessage), "MQTT not connected");
        return;
    }

    if (position < 1 || position > 9) {
        return;
    }

    if (gameOver) {
        snprintf(statusMessage, sizeof(statusMessage), "Game over. Press reset.");
        return;
    }

    if (latestTurn == 'X') {
        topic = TOPIC_X_MOVE;
    } else {
        topic = TOPIC_O_MOVE;
    }

    snprintf(
        payload,
        sizeof(payload),
        "{\"position\":%d,\"turn\":\"%c\"}",
        position,
        latestTurn
    );

    mosquitto_publish(mosq, NULL, topic, strlen(payload), payload, 0, false);

    snprintf(statusMessage, sizeof(statusMessage), "Sent Player %c move: %d", latestTurn, position);

    printf("Published to %s: %s\n", topic, payload);
}

void publishReset() {
    const char *message = "reset";

    if (mosq == NULL || !mqttConnected) {
        snprintf(statusMessage, sizeof(statusMessage), "MQTT not connected");
        return;
    }

    mosquitto_publish(mosq, NULL, TOPIC_RESET, strlen(message), message, 0, false);

    latestTurn = 'X';
    gameOver = false;

    snprintf(resultMessage, sizeof(resultMessage), "Game in progress");
    snprintf(statusMessage, sizeof(statusMessage), "Reset sent");

    printf("Published reset command.\n");
}

void drawGame() {
    DrawText("tic-tac-toe", 145, 25, 36, BLACK);

    drawStatus();
    drawBoard();
    drawMarks();
    drawModeButtons();
}

void drawStatus() {
    char turnText[64];
    snprintf(turnText, sizeof(turnText), "Current Turn: Player %c", latestTurn);

    DrawText(turnText, 75, 65, 24, DARKBLUE);

    DrawText(statusMessage, 75, 550, 20, DARKGRAY);
    DrawText(resultMessage, 75, 575, 22, MAROON);
}

void drawBoard() {
    DrawRectangleLines(BOARD_X, BOARD_Y, BOARD_SIZE, BOARD_SIZE, BLACK);

    DrawLine(BOARD_X + CELL_SIZE, BOARD_Y, BOARD_X + CELL_SIZE, BOARD_Y + BOARD_SIZE, BLACK);
    DrawLine(BOARD_X + CELL_SIZE * 2, BOARD_Y, BOARD_X + CELL_SIZE * 2, BOARD_Y + BOARD_SIZE, BLACK);

    DrawLine(BOARD_X, BOARD_Y + CELL_SIZE, BOARD_X + BOARD_SIZE, BOARD_Y + CELL_SIZE, BLACK);
    DrawLine(BOARD_X, BOARD_Y + CELL_SIZE * 2, BOARD_X + BOARD_SIZE, BOARD_Y + CELL_SIZE * 2, BLACK);
}

void drawMarks() {
    for (int i = 0; i < 9; i++) {
        int row = i / 3;
        int col = i % 3;

        int cellX = BOARD_X + col * CELL_SIZE;
        int cellY = BOARD_Y + row * CELL_SIZE;

        int centerX = cellX + CELL_SIZE / 2;
        int centerY = cellY + CELL_SIZE / 2;

        if (board[i] == 'X') {
            DrawText("X", centerX - 35, centerY - 50, 90, BLUE);
        }
        else if (board[i] == 'O') {
            DrawText("O", centerX - 35, centerY - 50, 90, RED);
        }
        else {
            char numberText[2];
            numberText[0] = board[i];
            numberText[1] = '\0';

            DrawText(numberText, centerX - 12, centerY - 18, 36, GRAY);
        }
    }
}

int getClickedCell(Vector2 mousePosition) {
    if (
        mousePosition.x < BOARD_X ||
        mousePosition.x > BOARD_X + BOARD_SIZE ||
        mousePosition.y < BOARD_Y ||
        mousePosition.y > BOARD_Y + BOARD_SIZE
    ) {
        return -1;
    }

    int col = (int)((mousePosition.x - BOARD_X) / CELL_SIZE);
    int row = (int)((mousePosition.y - BOARD_Y) / CELL_SIZE);

    return row * 3 + col + 1;
}

bool resetButtonClicked(Vector2 mousePosition) {
    Rectangle resetButton = {210, 640, 180, 40};
    return CheckCollisionPointRec(mousePosition, resetButton);
}

void publishGameMode(const char *mode) {
    if (mosq == NULL || !mqttConnected) {
        snprintf(statusMessage, sizeof(statusMessage), "MQTT not connected");
        return;
    }

    mosquitto_publish(
        mosq,
        NULL,
        TOPIC_MODE,
        strlen(mode),
        mode,
        0,
        false
    );

    if (strcmp(mode, "oneplayer") == 0) {
        snprintf(statusMessage, sizeof(statusMessage), "One player mode selected");
    } else if (strcmp(mode, "twoplayer") == 0) {
        snprintf(statusMessage, sizeof(statusMessage), "Two player mode selected");
    }

    printf("Published game mode: %s\n", mode);
}

void drawModeButtons() {
    Rectangle onePlayerButton = {75, 615, 200, 30};
    Rectangle twoPlayerButton = {325, 615, 200, 30};

    DrawRectangleRec(onePlayerButton, LIGHTGRAY);
    DrawRectangleLinesEx(onePlayerButton, 2, DARKGRAY);
    DrawText("One Player", 120, 622, 18, BLACK);

    DrawRectangleRec(twoPlayerButton, LIGHTGRAY);
    DrawRectangleLinesEx(twoPlayerButton, 2, DARKGRAY);
    DrawText("Two Player", 375, 622, 18, BLACK);
}

bool onePlayerButtonClicked(Vector2 mousePosition) {
    Rectangle onePlayerButton = {75, 615, 200, 30};
    return CheckCollisionPointRec(mousePosition, onePlayerButton);
}

bool twoPlayerButtonClicked(Vector2 mousePosition) {
    Rectangle twoPlayerButton = {325, 615, 200, 30};
    return CheckCollisionPointRec(mousePosition, twoPlayerButton);
}

#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>

#define SDA 0 //Define SDA pins
#define SCL 2 //Define SCL pins

String latestBoard = "123|456|789";
char latestTurn = 'X';

// define the symbols on the buttons of the keypad
char keys[4][4] = {
  {'1', '2', '3', '0'},
  {'4', '5', '6', '0'},
  {'7', '8', '9', '0'},
  {'r', '0', '0', '0'}
};

byte rowPins[4] = {14, 27, 26, 25}; // connect to the row pinouts of the keypad
byte colPins[4] = {13, 21, 22, 23};   // connect to the column pinouts of the keypad

// initialize an instance of class NewKeypad
Keypad myKeypad = Keypad(makeKeymap(keys), rowPins, colPins, 4, 4);

// ============================
// Wi-Fi settings
// ============================
const char* WIFI_SSID = "katphone";
const char* WIFI_PASSWORD = "password";

// ============================
// MQTT broker settings
// Replace this with your GCP external IP address
// ============================
const char* MQTT_SERVER = "schoolgcp.duckdns.org";
const int MQTT_PORT = 1883;

// Optional client name
const char* MQTT_CLIENT_ID = "esp32_tictactoe_player";

// ============================
// MQTT topics
// These should match your GCP C game
// ============================
const char* TOPIC_PLAYER_X_MOVE = "tictactoe/player/x/move";
const char* TOPIC_PLAYER_O_MOVE = "tictactoe/player/o/move";
const char* TOPIC_GAME_STATUS   = "tictactoe/game/status";
const char* TOPIC_GAME_BOARD    = "tictactoe/game/board";
const char* TOPIC_GAME_TURN     = "tictactoe/game/turn";
const char* TOPIC_GAME_RESULT   = "tictactoe/game/result";
const char* TOPIC_CONTROL_RESET = "tictactoe/control/reset";

// ============================
// LCD settings
// Common I2C addresses: 0x27 or 0x3F
// Change 0x27 to 0x3F if your LCD does not show text.
// ============================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ============================
// MQTT setup
// ============================
WiFiClient espClient;
PubSubClient client(espClient);

// ============================
// Helper function declarations
// ============================
void connectToWiFi();
void connectToMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void publishMove(int move);
void displayMessage(String line1, String line2);
void handleSerialInput();

void setup() {
  Serial.begin(115200);

  Wire.begin(SDA, SCL); // attach the IIC pin
  if (!i2CAddrTest(0x27)) {
    lcd = LiquidCrystal_I2C(0x3F, 16, 2);
  }
  lcd.init();
  lcd.backlight();

  displayMessage("TicTacToe ESP32", "Starting...");

  connectToWiFi();

  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(mqttCallback);

  connectToMQTT();

  displayMessage("TicTacToe Ready", "Enter move 1-9");
}

void loop() {
  if (!client.connected()) {
    connectToMQTT();
  }

  client.loop();

  handleSerialInput();
}

void connectToWiFi() {
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(WIFI_SSID);

  displayMessage("Connecting WiFi", WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("Wi-Fi connected.");
  Serial.print("ESP32 IP address: ");
  Serial.println(WiFi.localIP());

  displayMessage("WiFi connected", WiFi.localIP().toString());
  delay(1500);
}

void connectToMQTT() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT broker at ");
    Serial.println(MQTT_SERVER);

    displayMessage("Connecting MQTT", MQTT_SERVER);

    if (client.connect(MQTT_CLIENT_ID)) {
      Serial.println("MQTT connected.");

      client.subscribe(TOPIC_GAME_STATUS);
      client.subscribe(TOPIC_GAME_BOARD);
      client.subscribe(TOPIC_GAME_TURN);
      client.subscribe(TOPIC_GAME_RESULT);

      displayMessage("MQTT connected", "Waiting game...");
      delay(1000);

      // Reset game on connection
      client.publish(TOPIC_CONTROL_RESET, "reset");

    } else {
      Serial.print("MQTT failed, rc=");
      Serial.print(client.state());
      Serial.println(" retrying in 3 seconds.");

      displayMessage("MQTT failed", "Retrying...");
      delay(3000);
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String topicName = String(topic);
  String message = "";

  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("MQTT message received [");
  Serial.print(topicName);
  Serial.print("]: ");
  Serial.println(message);

  if (topicName == TOPIC_GAME_BOARD) {
    latestBoard = formatBoardForLCD(message);
    displayMessage(latestBoard, String("Turn: ") + latestTurn);
  } 
  else if (topicName == TOPIC_GAME_TURN) {
    if (message.indexOf('X') >= 0) {
      latestTurn = 'X';
    } 
    else if (message.indexOf('O') >= 0) {
      latestTurn = 'O';
    }

    displayMessage(latestBoard, String("Turn: ") + latestTurn);
  } 
  else if (topicName == TOPIC_GAME_STATUS) {
    if (message == "Game in progress" || message == "Game started" || message == "Game reset") {
      displayMessage(latestBoard, String("Turn: ") + latestTurn);
    } else {
      displayMessage(latestBoard, message);
    }
  } 
  else if (topicName == TOPIC_GAME_RESULT) {
    if (message == "Game in progress") {
      displayMessage(latestBoard, String("Turn: ") + latestTurn);
    } else {
      displayMessage(latestBoard, message);
    }
  }
}

void publishMove(int move) {
  if (move < 1 || move > 9) {
    Serial.println("Invalid move. Enter a number from 1 to 9.");
    displayMessage("Invalid move", "Use 1 to 9");
    return;
  }

  char payload[32];
  snprintf(payload, sizeof(payload), "%d", move);

  if (latestTurn == 'X') {
    client.publish(TOPIC_PLAYER_X_MOVE, payload);
  } else {
    client.publish(TOPIC_PLAYER_O_MOVE, payload);
  }
  

  Serial.print("Published move: ");
  Serial.println(payload);

  displayMessage("Move sent", String("Position ") + move);
}

void handleSerialInput() {
  char keyPressed = myKeypad.getKey();

  if (keyPressed > 0) {
    String input = {keyPressed, '\0'};

    if (input.length() == 0) {
      return;
    }

    if (input == "r" || input == "R") {
      client.publish(TOPIC_CONTROL_RESET, "reset");
      Serial.println("Reset command sent.");
      displayMessage("Reset sent", "");
      return;
    }

    int move = input.toInt();

    if (move >= 1 && move <= 9) {
      publishMove(move);
    } else {
      displayMessage(latestBoard, "Use 1-9 or *");
    }
  }
}

void displayMessage(String line1, String line2) {
  lcd.clear();

  if (line1.length() > 16) {
    line1 = line1.substring(0, 16);
  }

  if (line2.length() > 16) {
    line2 = line2.substring(0, 16);
  }

  lcd.setCursor(0, 0);
  lcd.print(line1);

  lcd.setCursor(0, 1);
  lcd.print(line2);
}

bool i2CAddrTest(uint8_t addr) {
  Wire.begin();
  Wire.beginTransmission(addr);
  if (Wire.endTransmission() == 0) {
    return true;
  }
  return false;
}

String formatBoardForLCD(String message) {

  String compactBoard = "";

  for (unsigned int i = 0; i < message.length(); i++) {
    if (message.charAt(i) != ' ') {
      compactBoard += message.charAt(i);
    }
  }

  if (compactBoard.length() < 9) {
    return latestBoard;
  }

  String lcdBoard = "";
  lcdBoard += compactBoard.charAt(0);
  lcdBoard += compactBoard.charAt(1);
  lcdBoard += compactBoard.charAt(2);
  lcdBoard += "|";
  lcdBoard += compactBoard.charAt(3);
  lcdBoard += compactBoard.charAt(4);
  lcdBoard += compactBoard.charAt(5);
  lcdBoard += "|";
  lcdBoard += compactBoard.charAt(6);
  lcdBoard += compactBoard.charAt(7);
  lcdBoard += compactBoard.charAt(8);

  return lcdBoard;
}
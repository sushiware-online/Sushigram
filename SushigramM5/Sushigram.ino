#include <M5Cardputer.h>
#include <SD.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <vector>

// --- Configuration ---
const int JSON_DOC_CAPACITY = 8192; // Increased capacity for dialogs list
const int MAX_DIALOGS = 20;       // Fetch up to 20 dialogs

// --- Global Variables ---
String ssid;
String password;
String serverIp;
String userToken;

// --- Data Structures ---
struct Dialog {
    long id;
    String title;
    String last_message;
    int unread_count;
};

std::vector<Dialog> dialogs;

// --- Application State ---
enum AppState {
    STATE_LOADING,
    STATE_CHAT_LIST,
    STATE_CHAT_VIEW,
    STATE_ERROR
};
AppState currentState = STATE_LOADING;

int selectedDialogIndex = 0;
int topVisibleIndex = 0; // For scrolling

// --- Functions for Loading Config ---
bool loadData() {
    if (!SD.exists("/sushigram.json")) {
        M5Cardputer.Display.println("sushigram.json not found!");
        M5Cardputer.Display.println("Please run the Python script");
        M5Cardputer.Display.println("and place the file on the SD card.");
        return false;
    }
    File file = SD.open("/sushigram.json");
    if (!file) return false;

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        M5Cardputer.Display.println("Failed to parse config!");
        return false;
    }
    ssid = doc["ssid"].as<String>();
    password = doc["password"].as<String>();
    serverIp = doc["server_ip"].as<String>();
    userToken = doc["user_token"].as<String>();

    if (userToken.length() == 0) {
        M5Cardputer.Display.println("No user_token in config!");
        M5Cardputer.Display.println("Please log in using the");
        M5Cardputer.Display.println("Python script first.");
        return false;
    }
    return true;
}

// --- WiFi Connection ---
void connectWiFi() {
    M5Cardputer.Display.print("Connecting to WiFi...");
    WiFi.begin(ssid.c_str(), password.c_str());
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        M5Cardputer.Display.print(".");
        attempts++;
    }
    if (WiFi.status() != WL_CONNECTED) {
        M5Cardputer.Display.println("\nFailed to connect.");
        currentState = STATE_ERROR;
    } else {
        M5Cardputer.Display.println("\nConnected!");
    }
}

// --- API Communication ---
String makeApiRequest(String method, String queryParams) {
    HTTPClient http;
    String url = "https://" + serverIp + "/api.php?method=" + method + "&v=10&user=" + userToken + "&" + queryParams;

    http.begin(url);
    int httpCode = http.POST("");
    String payload = "{}";

    if (httpCode > 0 && httpCode == HTTP_CODE_OK) {
        payload = http.getString();
    } else {
        payload = "{\"error\":\"HTTP " + String(httpCode) + "\"}";
    }
    http.end();
    return payload;
}

void fetchDialogs() {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setCursor(10, 10);
    M5Cardputer.Display.println("Fetching chats...");

    String response = makeApiRequest("getDialogs", "limit=" + String(MAX_DIALOGS));

    DynamicJsonDocument doc(JSON_DOC_CAPACITY);
    DeserializationError error = deserializeJson(doc, response);

    if (error || !doc["response"].is<JsonArray>()) {
        M5Cardputer.Display.println("Failed to parse dialogs!");
        M5Cardputer.Display.println(response);
        currentState = STATE_ERROR;
        return;
    }

    dialogs.clear();
    for (JsonObject item : doc["response"].as<JsonArray>()) {
        Dialog d;
        d.id = item["peer"]["id"];
        d.title = item["peer"]["title"].as<String>();
        d.last_message = item["message"].as<String>();
        d.unread_count = item["unread_count"];
        dialogs.push_back(d);
    }
    currentState = STATE_CHAT_LIST;
    drawChatList();
}

// --- UI Drawing Functions ---
void drawChatList() {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(0, 0);

    int y = 5;
    int maxLines = M5Cardputer.Display.height() / 20;

    for (int i = 0; i < maxLines; ++i) {
        int dialogIndex = topVisibleIndex + i;
        if (dialogIndex >= dialogs.size()) break;

        if (dialogIndex == selectedDialogIndex) {
            M5Cardputer.Display.setTextColor(TFT_BLACK, TFT_WHITE);
            M5Cardputer.Display.printf("> ");
        } else {
            M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
            M5Cardputer.Display.printf("  ");
        }

        String title = dialogs[dialogIndex].title;
        if (dialogs[dialogIndex].unread_count > 0) {
            title += " (+)";
        }
        M5Cardputer.Display.setCursor(12, y);
        M5Cardputer.Display.println(title.substring(0, 35));

        M5Cardputer.Display.setCursor(12, y + 9);
        M5Cardputer.Display.setTextColor(TFT_SILVER, TFT_BLACK);
        String lastMsg = dialogs[dialogIndex].last_message;
        lastMsg.replace("\n", " ");
        M5Cardputer.Display.println(lastMsg.substring(0, 38));

        y += 20;
    }
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
}

void drawChatView() {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextSize(1);

    if (selectedDialogIndex < dialogs.size()) {
        M5Cardputer.Display.fillRect(0, 0, 240, 12, TFT_DARKGREY);
        M5Cardputer.Display.setTextColor(TFT_WHITE);
        M5Cardputer.Display.setCursor(5, 2);
        M5Cardputer.Display.println(dialogs[selectedDialogIndex].title);

        M5Cardputer.Display.setCursor(10, 30);
        M5Cardputer.Display.println("Message history will be here.");
        M5Cardputer.Display.setCursor(10, 50);
        M5Cardputer.Display.println("Press DEL to go back.");
    }
}

// --- Main Application ---
void setup() {
    M5Cardputer.begin();
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(TFT_WHITE);
    M5Cardputer.Display.setCursor(10, 10);

    if (!SD.begin(12)) {
        M5Cardputer.Display.println("SD Card Error!");
        currentState = STATE_ERROR;
        return;
    }

    if (!loadData()) {
        currentState = STATE_ERROR;
        return;
    }

    connectWiFi();
    if (currentState != STATE_ERROR) {
        fetchDialogs();
    }
}

void loop() {
    M5Cardputer.update();

    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

        switch (currentState) {
            case STATE_CHAT_LIST: {
                bool needsRedraw = false;

                // Handle character-based keys (like arrows) by iterating status.word
                for (auto k : status.word) {
                    if (k == '/') { // Down Arrow
                        if (selectedDialogIndex < dialogs.size() - 1) {
                            selectedDialogIndex++;
                            needsRedraw = true;
                        }
                    } else if (k == '.') { // Up Arrow
                        if (selectedDialogIndex > 0) {
                            selectedDialogIndex--;
                            needsRedraw = true;
                        }
                    }
                }

                // Handle special keys (like Enter) by checking boolean flags
                if (status.enter) {
                    currentState = STATE_CHAT_VIEW;
                    drawChatView();
                    return; // Exit to avoid redrawing list
                }
                
                // Update scrolling if needed
                if (needsRedraw) {
                    int maxLines = M5Cardputer.Display.height() / 20;
                    if (selectedDialogIndex < topVisibleIndex) {
                        topVisibleIndex = selectedDialogIndex;
                    }
                    if (selectedDialogIndex >= topVisibleIndex + maxLines) {
                        topVisibleIndex = selectedDialogIndex - maxLines + 1;
                    }
                    drawChatList();
                }
                break;
            }

            case STATE_CHAT_VIEW: {
                if (status.del) { // del is the backspace key
                    currentState = STATE_CHAT_LIST;
                    drawChatList();
                }
                break;
            }

            case STATE_LOADING:
            case STATE_ERROR:
                // Do nothing
                break;
        }
    }

    delay(50);
}

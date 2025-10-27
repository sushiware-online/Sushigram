#include <M5Cardputer.h>
#include <SD.h>
#include <JPEGDecoder.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <EasyNTPClient.h>
#include <WiFiUdp.h>

WiFiClientSecure client;
WiFiUDP udp;

EasyNTPClient ntpClient(udp, "pool.ntp.org", 0);

String ssid;
String password;
String ip;
String number;
String sessionCookie;

String getInputAt(int x, int y) {
  String data = "> ";  // prompt
  M5Cardputer.Display.setCursor(x, y);
  M5Cardputer.Display.print(data);

  while (true) {
    M5Cardputer.update();

    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
      auto status = M5Cardputer.Keyboard.keysState();

      for (auto c : status.word) data += c;

      if (status.del && data.length() > 2) data.remove(data.length() - 1);

      M5Cardputer.Display.fillRect(x, y, 200, 16, TFT_BLACK);
      M5Cardputer.Display.setCursor(x, y);
      M5Cardputer.Display.print(data);

      if (status.enter) {
        String input = data;
        input.remove(0, 2);
        input.trim();
        return input;
      }
    }  
  
    delay(10);
  }
}

bool isRPressed() {
  M5Cardputer.update();
  auto status = M5Cardputer.Keyboard.keysState();

  for (char c : status.word) {
    if (c == 'r' || c == 'R') return true;
  }

  return false;
}

void saveData() {
  File file = SD.open("/sushigram.json", FILE_WRITE);
  if (!file) {
    M5Cardputer.Display.println("Failed to open file for writing!");
    return;
  }

  StaticJsonDocument<512> doc;
  doc["ssid"] = ssid;
  doc["password"] = password;
  doc["ip"] = ip;
  doc["number"] = number;
  doc["session"] = sessionCookie;

  serializeJson(doc, file);
  file.close();
}

bool loadData() {
  if (!SD.exists("/sushigram.json")) return false;

  File file = SD.open("/sushigram.json");
  if (!file) {
    M5Cardputer.Display.println("Failed to open file for reading!");
    return false;
  }

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    M5Cardputer.Display.println("Failed to parse JSON!");
    return false;
  }

  ssid = doc["ssid"].as<String>();
  password = doc["password"].as<String>();
  ip = doc["ip"].as<String>();
  number = doc["number"].as<String>();
  sessionCookie = doc["session"].as<String>();

  return true;
}

void connectWiFi() {
  M5Cardputer.Display.setCursor(10, 40);
  M5Cardputer.Display.print("Connecting to WiFi...");
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    M5Cardputer.Display.print(".");
  }
}

void firstLaunch() {
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Display.setCursor(10, 10);
  M5Cardputer.Display.println("Enter SSID:");
  ssid = getInputAt(10, 20);
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Display.setCursor(10, 10);
  M5Cardputer.Display.println("Enter password:");
  password = getInputAt(10, 20);

  M5Cardputer.Display.fillScreen(TFT_BLACK);
  connectWiFi();
  M5Cardputer.Display.fillScreen(TFT_BLACK);

  M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Display.setCursor(10, 10);
  M5Cardputer.Display.println("Input the proxy IP:");
  ip = getInputAt(10, 20);
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Display.setCursor(10, 10);
  M5Cardputer.Display.println("Input your phone number:");
  number = getInputAt(10, 20);

  retrieveSession();

  unsigned long unixTime = ntpClient.getUnixTime();

  fetchCaptcha(unixTime);

  //DO NOT CLEAR SCREEN HERE
  String captchaCode = getInputAt(10, 55);
  login(captchaCode);
}

void drawJPEG(int x, int y) {
  int mcu_w = JpegDec.MCUWidth;
  int mcu_h = JpegDec.MCUHeight;
  int mcu_x = 0;
  int mcu_y = 0;

  while (JpegDec.read()) {
    uint16_t* pImage = JpegDec.pImage;
    int mcu_pixels = mcu_w * mcu_h;
    for (int i = 0; i < mcu_pixels; i++) {
      pImage[i] = (pImage[i] << 8) | (pImage[i] >> 8); // swap RGB bytes if needed
    }

    int block_w = (mcu_x + mcu_w <= JpegDec.width) ? mcu_w : (JpegDec.width - mcu_x);
    int block_h = (mcu_y + mcu_h <= JpegDec.height) ? mcu_h : (JpegDec.height - mcu_y);

    M5Cardputer.Display.pushImage(x + mcu_x, y + mcu_y, block_w, block_h, pImage);

    mcu_x += mcu_w;
    if (mcu_x >= JpegDec.width) {
      mcu_x = 0;
      mcu_y += mcu_h;
    }
  }
}

void fetchCaptcha(unsigned long unixTime) {
  client.setInsecure(); // ignore TLS for HTTPS

  if (!client.connect(ip.c_str(), 443)) {
    Serial.println("Connection failed!");
    return;
  }

  String url = "/captcha.php?r=" + String(unixTime);
  String req = "GET " + url + " HTTP/1.1\r\n";
  req += "Host: " + ip + "\r\n";
  req += "User-Agent: Mozilla/5.0 (ESP32) M5Cardputer\r\n";
  req += "Cookie: " + sessionCookie + "\r\n";
  req += "Connection: close\r\n\r\n";

  client.print(req);

  // Skip HTTP headers
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") break;
  }

  // Read image into buffer
  static uint8_t jpgBuf[32 * 1024]; // adjust size if needed
  size_t jpgSize = 0;

  while (client.available() && jpgSize < sizeof(jpgBuf)) {
    jpgBuf[jpgSize++] = client.read();
  }
  client.stop();

  if (jpgSize == 0) {
    Serial.println("Failed to read captcha image");
    return;
  }

  // Decode and display
  if (JpegDec.decodeArray(jpgBuf, jpgSize) == 1) {
    drawJPEG(0, 0);
    Serial.println("Captcha displayed");
  } else {
    Serial.println("JPEG decode failed");
  }
}

void retrieveSession() {
  client.setInsecure(); // for HTTPS

  if (!client.connect(ip.c_str(), 443)) {
    M5Cardputer.Display.println("Failed to connect to proxy!");
    return;
  }

  // Send GET /login.php to get PHPSESSID
  String req = "GET /login.php HTTP/1.1\r\n";
  req += "Host: " + ip + "\r\n";
  req += "User-Agent: Mozilla/5.0 (ESP32) M5Cardputer\r\n";
  req += "Accept: text/html\r\n";
  req += "Connection: close\r\n\r\n";
  client.print(req);

  // Read response headers and extract Set-Cookie
  sessionCookie = "";
  while (client.connected() || client.available()) {
    String line = client.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) break; // headers done

    if (line.startsWith("Set-Cookie:")) {
      int idx = line.indexOf(':');
      String cookieLine = line.substring(idx + 1);
      cookieLine.trim();
      int sc = cookieLine.indexOf(';');
      if (sc > 0) cookieLine = cookieLine.substring(0, sc);
      if (cookieLine.startsWith("PHPSESSID")) {
        sessionCookie = cookieLine; // store PHPSESSID
        break;
      }
    }
  }
  client.stop();

  if (sessionCookie.length()) {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setCursor(10, 10);
    M5Cardputer.Display.println("Session retrieved:");
    M5Cardputer.Display.println(sessionCookie);
    saveData();
  } else {
    M5Cardputer.Display.println("Failed to get session!");
  }
}

void login(String captchaCode) {
  client.setInsecure();

  if (!client.connect(ip.c_str(), 443)) {
    M5Cardputer.Display.println("Failed to connect to proxy!");
    return;
  }

  String postData = "phone=" + number + "&c=" + captchaCode;
  String req = "POST /login.php HTTP/1.1\r\n";
  req += "Host: " + ip + "\r\n";
  req += "User-Agent: Mozilla/5.0 (ESP32) M5Cardputer\r\n";
  req += "Cookie: " + sessionCookie + "\r\n";
  req += "Connection: close\r\n";
  req += "Content-Type: application/x-www-form-urlencoded\r\n";
  req += "Content-Length: " + String(postData.length()) + "\r\n\r\n";
  req += postData;

  client.print(req);

  while (client.connected() || client.available()) {
    String line = client.readStringUntil('\n');
    Serial.println(line);
  }
  client.stop();
}

void setup() {
  M5Cardputer.begin();
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(TFT_WHITE);
  M5Cardputer.Display.setCursor(10, 10);

  if (!SD.begin(12)) {
    M5Cardputer.Display.println("Please insert an SD card!");
    while (1) {}
  }

  M5Cardputer.Display.println("Hold r to reset!");
  sleep(1.5);
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  bool loaded = loadData();
  if (!loaded || isRPressed()) {
    M5Cardputer.Display.setCursor(10, 10);
    M5Cardputer.Display.println("Starting first config");
    sleep(1.5);
    firstLaunch();
    saveData();
  } else {
    M5Cardputer.Display.println("Loaded configuration from SD card.");
    connectWiFi();
  }
}

void loop() {

}

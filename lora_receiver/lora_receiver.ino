/*-----------------------------------------------------
Board : LoRa Receiver Heltec WiFi LoRa 32(V2) 
Developed For : Firebase & Telegram with encrypted data
-------------------------------------------------------*/

// Libraries
#include <heltec.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <HTTPClient.h>
#include <time.h>


#define WIFI_SSID "PASTE_YOUR_SSID_HERE"
#define WIFI_PASS "PASTE_YOUR_PASSWORD_HERE"

#define FIREBASE_URL "PASTE_YOUR_URL_HERE"

#define BOT_TOKEN "PASTE_YOUR_TELEGRAM_BOT_TOKEN_HERE"
#define CHAT_ID "PASTE_YOUR_TELEGRAM_ID_HERE"

WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

#define SECRET_KEY "DEFINE_YOUR_SECRET_KEY_FOR_DECRYPTION"
#define BAND 868E6  // 868E6 for EU, 915E6 for US

#define TEMP_THRESHOLD 40.0
#define HUM_THRESHOLD 80.0

bool tempAlertSent = false;
bool humAlertSent = false;

String lastID = "--";
float lastTemp = 0, lastHum = 0, lastLat = 0, lastLng = 0;
int lastBat = 0, lastRssi = 0;
unsigned long lastReceiveTime = 0;
unsigned long timer;
unsigned long wifiReconnectTimer = 0;

String xorCipher(String data) {
  String out = "";
  int klen = strlen(SECRET_KEY);
  for (int i = 0; i < data.length(); i++)
    out += char(data[i] ^ SECRET_KEY[i % klen]);
  return out;
}

void oledDisplay(String l1, String l2 = "", String l3 = "", String l4 = "") {
  Heltec.display->clear();
  Heltec.display->setTextAlignment(TEXT_ALIGN_LEFT);
  Heltec.display->setFont(ArialMT_Plain_10);
  Heltec.display->drawString(0, 0, l1);
  Heltec.display->drawString(0, 12, l2);
  Heltec.display->drawString(0, 24, l3);
  Heltec.display->drawString(0, 36, l4);
  Heltec.display->display();
}

void sendTelegram(String msg) {
  if (strlen(CHAT_ID) > 0 && CHAT_ID[0] != '\0') {
    Serial.println("Sending Telegram: " + msg);
    
    if (bot.sendMessage(CHAT_ID, msg, "")) {
      Serial.println("Telegram sent successfully");
    } else {
      Serial.println("Failed to send Telegram");
    }
  } else {
    Serial.println("Telegram CHAT_ID not set!");
  }
}

bool sendToFirebase(String path, String jsonData) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, skipping Firebase");
    return false;
  }
  
  HTTPClient http;
  String url = String(FIREBASE_URL) + path + ".json";
  
  Serial.println("Firebase URL: " + url);
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  int httpCode = http.PUT(jsonData);
  String response = http.getString();
  
  Serial.print("Firebase HTTP Code: ");
  Serial.println(httpCode);
  
  http.end();
  
  return (httpCode == 200 || httpCode == 204);
}

void processLoRaPacket(int packetSize) {
  String encrypted = "";
  for (int i = 0; i < packetSize; i++) {
    encrypted += (char)LoRa.read();
  }
  
  Serial.print("Received encrypted: ");
  Serial.println(encrypted);
  
  String plain = xorCipher(encrypted);
  Serial.print("Decrypted: ");
  Serial.println(plain);
  
  // Parse: ID;TEMP;HUM;LAT;LNG;BAT
  int p[5];
  p[0] = plain.indexOf(';');
  if (p[0] == -1) {
    Serial.println("Parsing error: No semicolon found");
    return;
  }
  
  for (int i = 1; i < 5; i++) {
    p[i] = plain.indexOf(';', p[i-1] + 1);
    if (p[i] == -1) {
      Serial.println("Parsing error: Not enough fields");
      return;
    }
  }
  
  lastID = plain.substring(0, p[0]);
  lastTemp = plain.substring(p[0] + 1, p[1]).toFloat();
  lastHum = plain.substring(p[1] + 1, p[2]).toFloat();
  lastLat = plain.substring(p[2] + 1, p[3]).toFloat();
  lastLng = plain.substring(p[3] + 1, p[4]).toFloat();
  lastBat = plain.substring(p[4] + 1).toInt();
  lastRssi = LoRa.packetRssi();
  lastReceiveTime = millis();
  
  Serial.print("Parsed - ID: ");
  Serial.print(lastID);
  Serial.print(" Temp: ");
  Serial.print(lastTemp);
  Serial.print(" Hum: ");
  Serial.print(lastHum);
  Serial.print(" RSSI: ");
  Serial.println(lastRssi);
  
  // OLED Display
  String wifiStatus = WiFi.status() == WL_CONNECTED ? "WiFi:ON" : "WiFi:OFF";
  oledDisplay(
    "Colis " + lastID,
    "T:" + String(lastTemp, 1) + "C H:" + String(lastHum, 1) + "%",
    "Bat:" + String(lastBat) + "% " + wifiStatus,
    "RSSI:" + String(lastRssi) + "dBm"
  );
  
  // Send to Firebase if connected
  if (WiFi.status() == WL_CONNECTED) {
    String base = "/tracker/colis_" + lastID + "/live";
    String jsonData = "{";
    jsonData += "\"temperature\":" + String(lastTemp, 2) + ",";
    jsonData += "\"humidite\":" + String(lastHum, 2) + ",";
    jsonData += "\"lat\":" + String(lastLat, 6) + ",";
    jsonData += "\"lng\":" + String(lastLng, 6) + ",";
    jsonData += "\"battery\":" + String(lastBat) + ",";
    jsonData += "\"rssi\":" + String(lastRssi) + ",";
    jsonData += "\"timestamp\":" + String(millis() / 1000);
    jsonData += "}";
    
    if (sendToFirebase(base, jsonData)) {
      Serial.println("Firebase update successful");
      oledDisplay("Colis " + lastID, "Data Sent", "Firebase: OK", "RSSI:" + String(lastRssi) + "dBm");
      delay(500);
    }
  }
  
  // Telegram Alerts
  if (WiFi.status() == WL_CONNECTED) {
    if (lastTemp > TEMP_THRESHOLD && !tempAlertSent) {
      String alertMsg = "🔥 HIGH TEMPERATURE ALERT!\n";
      alertMsg += "📦 Package: Colis " + lastID + "\n";
      alertMsg += "🌡️ Temperature: " + String(lastTemp, 1) + "°C\n";
      alertMsg += "⚠️ Threshold: " + String(TEMP_THRESHOLD) + "°C\n";
      alertMsg += "📶 Signal: " + String(lastRssi) + " dBm";
      
      sendTelegram(alertMsg);
      tempAlertSent = true;
    }
    
    if (lastTemp <= (TEMP_THRESHOLD - 3.0)) {
      tempAlertSent = false;
    }
    
    if (lastHum > HUM_THRESHOLD && !humAlertSent) {
      String alertMsg = "💧 HIGH HUMIDITY ALERT!\n";
      alertMsg += "📦 Package: Colis " + lastID + "\n";
      alertMsg += "💦 Humidity: " + String(lastHum, 1) + "%\n";
      alertMsg += "⚠️ Threshold: " + String(HUM_THRESHOLD) + "%\n";
      alertMsg += "📶 Signal: " + String(lastRssi) + " dBm";
      
      sendTelegram(alertMsg);
      humAlertSent = true;
    }
    
    if (lastHum <= (HUM_THRESHOLD - 10.0)) {
      humAlertSent = false;
    }
  }
}

void setup() {
  // Initialize Heltec Board
  Heltec.begin(true /*DisplayEnable*/, true /*LoRaEnable*/, true /*SerialEnable*/, 
               BAND, true /*PABOOST*/);
  
  // Initialize Serial
  Serial.begin(115200);
  
  // Configure LoRa
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setSyncWord(0x12);
  LoRa.enableCrc();
  LoRa.receive();
  
  // OLED Initial Display
  Heltec.display->flipScreenVertically();
  Heltec.display->setFont(ArialMT_Plain_10);
  Heltec.display->clear();
  Heltec.display->drawString(0, 24, "Starting LoRa Receiver...");
  Heltec.display->display();
  delay(2000);
  
  // WiFi Connection
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  oledDisplay("Connecting WiFi", "", "SSID: " + String(WIFI_SSID), "");
  
  Serial.print("Connecting to WiFi");
  int wifiTimeout = 0;
  while (WiFi.status() != WL_CONNECTED && wifiTimeout < 20) {
    delay(500);
    Serial.print(".");
    wifiTimeout++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.println("IP: " + WiFi.localIP().toString());
    
    // Configure NTP time
    configTzTime("UTC", "pool.ntp.org");
    
    // Telegram SSL setup
    client.setInsecure();
    
    // Send startup message
    String startupMsg = "🚚 LoRa Tracker Started\n";
    startupMsg += "Board: Heltec LoRa32 V2\n";
    startupMsg += "IP: " + WiFi.localIP().toString() + "\n";
    startupMsg += "Receiver is online";
    
    sendTelegram(startupMsg);
    
    oledDisplay("Receiver Ready", WiFi.localIP().toString(), "Telegram: OK", "Freq: " + String(BAND/1E6) + "MHz");
  } else {
    Serial.println("\nWiFi connection failed!");
    oledDisplay("WiFi Failed", "Check credentials", "Working offline", "");
  }
  
  delay(2000);
  timer = millis();
}

void loop() {
  // Check WiFi connection periodically
  if (millis() - wifiReconnectTimer > 30000) {
    wifiReconnectTimer = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected, reconnecting...");
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
  }
  
  // Process LoRa packets
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    processLoRaPacket(packetSize);
  }
  
  // Show waiting message if no data for 30 seconds
  if (millis() - lastReceiveTime > 30000) {
    String statusLine = WiFi.status() == WL_CONNECTED ? "WiFi: Connected" : "WiFi: Disconnected";
    oledDisplay("LoRa Receiver", "Waiting for data...", statusLine, "Freq: " + String(BAND/1E6) + "MHz");
  }
  
  delay(10);
}
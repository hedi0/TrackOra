/*---------------------------------------
Board: LoRa Sender Heltec WiFi LoRa 32(V2) 
Developed For: Encrypted data transmission
-----------------------------------------*/

// Libraries
#include <heltec.h>
#include <DHT.h>

#define DHTPIN 13
#define DHTTYPE DHT22

#define SECRET_KEY "DEFINE_YOUR_SECRET_KEY_FOR_ENCRYPTION"

#define COLIS_ID "01"

#define BAND 868E6  // 868E6 for EU, 915E6 for US

DHT dht(DHTPIN, DHTTYPE);

String xorCipher(String data) {
  String result = "";
  int keyLen = strlen(SECRET_KEY);
  for (int i = 0; i < data.length(); i++) {
    result += char(data[i] ^ SECRET_KEY[i % keyLen]);
  }
  return result;
}

void setup() {
  // Initialize Heltec Board
  Heltec.begin(true /*DisplayEnable*/, true /*LoRaEnable*/, true /*SerialEnable*/, 
               BAND, true /*PABOOST*/);
  
  // Initialize Serial
  Serial.begin(115200);
  
  // Initialize DHT sensor
  dht.begin();
  
  // Configure LoRa
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setSyncWord(0x12);
  LoRa.enableCrc();
  LoRa.setTxPower(14, PA_OUTPUT_PA_BOOST_PIN);
  
  // OLED Initial Display
  Heltec.display->flipScreenVertically();
  Heltec.display->setFont(ArialMT_Plain_10);
  Heltec.display->clear();
  Heltec.display->drawString(0, 24, "Starting LoRa Sender...");
  Heltec.display->display();
  delay(2000);
  
  oledDisplay("Transmitter Ready", "ID: " + String(COLIS_ID), "Freq: " + String(BAND/1E6) + "MHz", "Encryption: ON");
  
  Serial.println("LoRa Sender Initialized");
  Serial.println("Device ID: " + String(COLIS_ID));
  Serial.println("Frequency: " + String(BAND/1E6) + " MHz");
  delay(1000);
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

void loop() {
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();
  
  if (isnan(temp) || isnan(hum)) {
    oledDisplay("DHT Sensor Error", "Check connection", "", "");
    Serial.println("Failed to read from DHT sensor!");
    delay(2000);
    return;
  }
  
  // Fixed GPS coordinates (example: Paris)
  float lat = 48.8566;
  float lng = 2.3522;
  int battery = 82;  // Simulated battery level
  
  String plain = 
    String(COLIS_ID) + ";" +
    String(temp, 1) + ";" +
    String(hum, 1) + ";" +
    String(lat, 6) + ";" +
    String(lng, 6) + ";" +
    String(battery);

  String encrypted = xorCipher(plain);
  
  LoRa.beginPacket();
  LoRa.print(encrypted);
  LoRa.endPacket();
  
  int rssi = LoRa.packetRssi();
  float snr = LoRa.packetSnr();
  
  oledDisplay(
    "ID: " + String(COLIS_ID),
    "T:" + String(temp, 1) + "C H:" + String(hum, 1) + "%",
    "Bat:" + String(battery) + "% Sent",
    "RSSI:" + String(rssi) + "dBm"
  );
  
  // Serial output for debugging
  Serial.println("--- Transmission ---");
  Serial.println("Plain Text: " + plain);
  Serial.println("Encrypted : " + encrypted);
  Serial.println("RSSI: " + String(rssi) + " dBm");
  Serial.println("SNR: " + String(snr) + " dB");
  Serial.println("--------------------");
  
  // Blink LED to indicate transmission
  digitalWrite(LED, HIGH);
  delay(100);
  digitalWrite(LED, LOW);
  
  delay(10000);
}
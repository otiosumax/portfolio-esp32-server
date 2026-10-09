// Прошивка portfolio-сервера: Wi-Fi + запуск API/SSE.
// Вся HTTP-логика живёт в lib/PortfolioServer (ApiServer), чтобы её же
// использовали интеграционные тесты (pio test).

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>

#include "ApiServer.h"
#include "wifi_credentials.h"

ApiServer api;

void setup() {
  Serial.begin(115200);
  delay(500);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("[wifi] connecting to '%s'", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("[wifi] connected, IP: ");
  Serial.println(WiFi.localIP());

  if (!MDNS.begin(HOSTNAME)) {
    Serial.println("[mdns] failed to start");
  }

  api.begin();

  Serial.printf("[server] ready at http://%s.local/projects\n", HOSTNAME);
  Serial.printf("[sse] events stream at http://%s.local/events\n", HOSTNAME);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(500);
  }
  api.loop();
  delay(2);
}

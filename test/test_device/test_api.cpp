// Интеграционные тесты HTTP API и SSE на устройстве: поднимается тот же
// ApiServer, что и в прошивке, а тест обращается к нему по сети через
// AsyncClient (на собственный IP).
//
// Требования к окружению:
//   * плата подключена к Wi-Fi (учётные данные в wifi_credentials.h);
//   * точка доступа не должна включать «client isolation» (устройство
//     подключается к самому себе).
//
// Запуск: pio test -e esp32s3

#include <Arduino.h>
#include <WiFi.h>
#include <unity.h>
#include <AsyncTCP.h>

#include <ArduinoJson.h>

#include "ApiServer.h"
#include "ProjectJson.h"
#include "wifi_credentials.h"

ApiServer api;

// --- Минимальный синхронный HTTP-клиент поверх AsyncClient -----------------

struct HttpResponse {
  int status = 0;
  String body;
};

static void deleteAllProjects() {
  while (!api.store.all().empty()) {
    api.store.remove(api.store.all().front().id);
  }
}

void setUp(void) {
  deleteAllProjects();
}

void tearDown(void) {
  deleteAllProjects();
}

HttpResponse httpRequest(const String& method, const String& path,
                         const String& body = String(),
                         bool withContentType = true) {
  HttpResponse result;

  String raw = method + " " + path + " HTTP/1.1\r\n"
               "Host: test\r\n"
               "Connection: close\r\n";
  if (body.length() > 0 && withContentType) {
    raw += "Content-Type: application/json\r\n";
  }
  if (body.length() > 0) {
    raw += "Content-Length: " + String(body.length()) + "\r\n";
  }
  raw += "\r\n";
  raw += body;

  bool done = false;
  String collected;
  AsyncClient* client = new AsyncClient();

  client->onConnect([&](void* arg, AsyncClient* c) {
    (void)arg;
    c->write(raw.c_str(), raw.length());
  });
  client->onData(
      [&](void* arg, AsyncClient* c, void* data, size_t len) {
        (void)arg;
        (void)c;
        collected.concat(reinterpret_cast<const char*>(data),
                         static_cast<unsigned int>(len));
      },
      nullptr);
  client->onDisconnect([&](void* arg, AsyncClient* c) {
    (void)arg;
    (void)c;
    done = true;
  });
  client->onError([&](void* arg, AsyncClient* c, int8_t err) {
    (void)arg;
    (void)c;
    (void)err;
    done = true;
  });

  TEST_ASSERT_TRUE_MESSAGE(client->connect(WiFi.localIP(), 80),
                           "TCP connect to own IP failed");

  const uint32_t start = millis();
  while (!done && millis() - start < 15000) {
    delay(5);
  }
  client->close(true);
  delete client;

  if (!done) {
    return result;  // статус 0 — тест упадёт на проверке статуса
  }

  const int headerEnd = collected.indexOf("\r\n\r\n");
  String head = collected;
  if (headerEnd >= 0) {
    result.body = collected.substring(headerEnd + 4);
    head = collected.substring(0, headerEnd);
  }
  const int sp1 = head.indexOf(' ');
  if (sp1 >= 0) {
    const int sp2 = head.indexOf(' ', sp1 + 1);
    result.status = head.substring(sp1 + 1, sp2).toInt();
  }
  return result;
}

// --- Тесты ------------------------------------------------------------------

void test_http_crud_cyrillic(void) {
  // Создание с кириллицей.
  const char* payload =
      "{\"title\":\"Умный дом\",\"tags\":[\"esp32\",\"iot\"],"
      "\"description\":\"Описание на русском.\","
      "\"githubLink\":\"https://github.com/me/x\","
      "\"imageURL\":\"https://example.com/x.png\"}";
  HttpResponse created = httpRequest("PUT", "/projects", payload);
  TEST_ASSERT_EQUAL(201, created.status);

  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, created.body));
  String id = doc["id"] | "";
  TEST_ASSERT_FALSE(id.isEmpty());
  TEST_ASSERT_EQUAL_STRING("Умный дом", doc["title"] | "");
  TEST_ASSERT_EQUAL_STRING("Описание на русском.", doc["description"] | "");

  // Список содержит проект.
  HttpResponse list = httpRequest("GET", "/projects");
  TEST_ASSERT_EQUAL(200, list.status);
  TEST_ASSERT_TRUE(list.body.indexOf("Умный дом") >= 0);

  // Получение по id.
  HttpResponse one = httpRequest("GET", "/projects/" + id);
  TEST_ASSERT_EQUAL(200, one.status);
  TEST_ASSERT_TRUE(one.body.indexOf(id) >= 0);

  // Частичное обновление: меняется только title, description сохраняется.
  HttpResponse updated =
      httpRequest("PUT", "/projects/" + id, "{\"title\":\"Обновлённый\"}");
  TEST_ASSERT_EQUAL(200, updated.status);
  JsonDocument upd;
  TEST_ASSERT_FALSE(deserializeJson(upd, updated.body));
  TEST_ASSERT_EQUAL_STRING("Обновлённый", upd["title"] | "");
  TEST_ASSERT_EQUAL_STRING("Описание на русском.", upd["description"] | "");

  // Удаление.
  HttpResponse deleted = httpRequest("DELETE", "/projects/" + id);
  TEST_ASSERT_EQUAL(204, deleted.status);
  HttpResponse after = httpRequest("GET", "/projects/" + id);
  TEST_ASSERT_EQUAL(404, after.status);
}

void test_http_errors(void) {
  // Недопустимый id в пути.
  HttpResponse badId = httpRequest("GET", "/projects/абв");
  TEST_ASSERT_EQUAL(400, badId.status);

  // Корректный, но несуществующий id.
  HttpResponse missing = httpRequest("GET", "/projects/zz999");
  TEST_ASSERT_EQUAL(404, missing.status);

  // Недопустимый id в теле.
  HttpResponse badBodyId =
      httpRequest("PUT", "/projects", "{\"id\":\"bad_id\",\"title\":\"T\"}");
  TEST_ASSERT_EQUAL(400, badBodyId.status);

  // Пустое тело.
  HttpResponse empty = httpRequest("PUT", "/projects", "");
  TEST_ASSERT_EQUAL(400, empty.status);

  // Несуществующий маршрут.
  HttpResponse unknown = httpRequest("GET", "/nope");
  TEST_ASSERT_EQUAL(404, unknown.status);

  // Слишком большое тело (лимит 16 КБ).
  String big = "{\"title\":\"";
  for (int i = 0; i < 20000; ++i) {
    big += 'a';
  }
  big += "\"}";
  HttpResponse tooBig = httpRequest("PUT", "/projects", big);
  TEST_ASSERT_EQUAL(413, tooBig.status);
}

void test_sse_stream(void) {
  // Открываем SSE-поток.
  AsyncClient* es = new AsyncClient();
  String collected;
  es->onConnect([&](void* arg, AsyncClient* c) {
    (void)arg;
    c->write("GET /events HTTP/1.1\r\nHost: test\r\n"
             "Accept: text/event-stream\r\n\r\n");
  });
  es->onData(
      [&](void* arg, AsyncClient* c, void* data, size_t len) {
        (void)arg;
        (void)c;
        collected.concat(reinterpret_cast<const char*>(data),
                         static_cast<unsigned int>(len));
      },
      nullptr);
  TEST_ASSERT_TRUE(es->connect(WiFi.localIP(), 80));

  // Ждём заголовки потока.
  uint32_t start = millis();
  while (collected.indexOf("event-stream") < 0 && millis() - start < 10000) {
    delay(10);
  }
  TEST_ASSERT_TRUE(collected.indexOf("200") >= 0);
  TEST_ASSERT_TRUE(collected.indexOf("event-stream") >= 0);

  // Создание проекта должно породить событие project-created.
  const HttpResponse created =
      httpRequest("PUT", "/projects", "{\"title\":\"SSE Тест\"}");
  TEST_ASSERT_EQUAL(201, created.status);

  start = millis();
  while (collected.indexOf("event: project-created") < 0 &&
         millis() - start < 10000) {
    delay(10);
  }
  TEST_ASSERT_TRUE(collected.indexOf("event: project-created") >= 0);
  TEST_ASSERT_TRUE(collected.indexOf("SSE Тест") >= 0);

  es->close(true);
  delete es;
}

void setup() {
  delay(2000);

  Serial.printf("[test] connecting to Wi-Fi '%s'...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(250);
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[test] Wi-Fi connect failed — HTTP-тесты упадут");
  }

  api.begin();
  delay(200);

  UNITY_BEGIN();
  RUN_TEST(test_http_crud_cyrillic);
  RUN_TEST(test_http_errors);
  RUN_TEST(test_sse_stream);
  UNITY_END();
}

void loop() {
  api.loop();
  delay(2);
}

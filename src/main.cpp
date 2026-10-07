// REST API сервер портфолио программных проектов.
//
// Маршруты:
//   GET    /projects        -> [ {id, title, description, githubLink, imageURL}, ... ]
//   GET    /projects/{id}   -> один проект (404, если нет)
//   PUT    /projects        -> создать проект (тело без id, сервер выдаёт id) -> 201
//   PUT    /projects/{id}   -> обновить проект (можно передать только часть полей)
//   DELETE /projects/{id}   -> удалить проект -> 204
//
// Данные хранятся в файле /projects.json в LittleFS (флеш-память),
// поэтому переживают перезагрузку и отключение питания.

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <LittleFS.h>

#include <ArduinoJson.h>

#include "ProjectStore.h"
#include "wifi_credentials.h"

WebServer server(80);
ProjectStore store;

// --- Вспомогательные функции ---------------------------------------------

// CORS-заголовки, чтобы к API мог обращаться фронтенд с другого origin.
void addCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods",
                    "GET, PUT, DELETE, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void sendJson(int code, const String& body) {
  addCorsHeaders();
  server.send(code, "application/json", body);
}

void sendNoContent() {
  addCorsHeaders();
  server.send(204);
}

String errorJson(const char* message) {
  JsonDocument doc;
  doc["error"] = message;
  String out;
  serializeJson(doc, out);
  return out;
}

String serializeProject(const Project& p) {
  JsonDocument doc;
  doc["id"] = p.id;
  doc["title"] = p.title;
  doc["description"] = p.description;
  doc["githubLink"] = p.githubLink;
  doc["imageURL"] = p.imageURL;
  String out;
  serializeJson(doc, out);
  return out;
}

String serializeProjects() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const Project& p : store.all()) {
    JsonObject obj = arr.add<JsonObject>();
    obj["id"] = p.id;
    obj["title"] = p.title;
    obj["description"] = p.description;
    obj["githubLink"] = p.githubLink;
    obj["imageURL"] = p.imageURL;
  }
  String out;
  serializeJson(doc, out);
  return out;
}

// Извлекает числовой id из URI вида /projects/123.
// Возвращает false, если формат не подходит.
bool parseId(const String& uri, uint32_t& id) {
  const char* prefix = "/projects/";
  if (!uri.startsWith(prefix)) {
    return false;
  }

  String tail = uri.substring(strlen(prefix));
  if (tail.isEmpty() || tail.indexOf('/') >= 0) {
    return false;
  }
  for (char c : tail) {
    if (!isdigit(static_cast<unsigned char>(c))) {
      return false;
    }
  }

  id = strtoul(tail.c_str(), nullptr, 10);
  return true;
}

// Разбирает тело запроса в проект. Поле id игнорируется — его выдаёт
// сервер. Пропущенные строковые поля становятся пустыми строками.
bool parseProjectBody(const String& body, Project& out) {
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    return false;
  }
  if (!doc.is<JsonObject>()) {
    return false;
  }

  out.title = doc["title"] | "";
  out.description = doc["description"] | "";
  out.githubLink = doc["githubLink"] | "";
  out.imageURL = doc["imageURL"] | "";
  out.title.trim();
  return true;
}

// Применяет к существующему проекту только те поля, которые реально
// переданы в теле запроса (null-поля не трогают текущее значение).
bool applyProjectUpdate(const Project& base, const String& body,
                        Project& out) {
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    return false;
  }
  if (!doc.is<JsonObject>()) {
    return false;
  }

  out = base;
  if (!doc["title"].isNull()) out.title = doc["title"] | "";
  if (!doc["description"].isNull()) out.description = doc["description"] | "";
  if (!doc["githubLink"].isNull()) out.githubLink = doc["githubLink"] | "";
  if (!doc["imageURL"].isNull()) out.imageURL = doc["imageURL"] | "";
  out.title.trim();
  return !out.title.isEmpty();
}

// --- Обработчики маршрутов -------------------------------------------------

void handleGetProjects() {
  sendJson(200, serializeProjects());
}

void handleCreateProject() {
  Project p;
  if (!parseProjectBody(server.arg("plain"), p) || p.title.isEmpty()) {
    sendJson(400, errorJson(
        "Invalid body: expected JSON object with fields "
        "title, description, githubLink, imageURL"));
    return;
  }

  Project created =
      store.create(p.title, p.description, p.githubLink, p.imageURL);
  Serial.printf("[API] created project #%u: %s\n", created.id,
                created.title.c_str());
  sendJson(201, serializeProject(created));
}

void handleProjectById(uint32_t id) {
  switch (server.method()) {
    case HTTP_GET: {
      const Project* p = store.find(id);
      if (!p) {
        sendJson(404, errorJson("Project not found"));
        return;
      }
      sendJson(200, serializeProject(*p));
      return;
    }

    case HTTP_PUT: {
      const Project* existing = store.find(id);
      if (!existing) {
        sendJson(404, errorJson("Project not found"));
        return;
      }

      Project updated;
      if (!applyProjectUpdate(*existing, server.arg("plain"), updated)) {
        sendJson(400, errorJson(
            "Invalid body: expected JSON object with fields "
            "title, description, githubLink, imageURL"));
        return;
      }

      store.update(id, updated);
      Serial.printf("[API] updated project #%u\n", id);
      sendJson(200, serializeProject(updated));
      return;
    }

    case HTTP_DELETE: {
      if (!store.remove(id)) {
        sendJson(404, errorJson("Project not found"));
        return;
      }
      Serial.printf("[API] deleted project #%u\n", id);
      sendNoContent();
      return;
    }

    default:
      sendJson(405, errorJson("Method not allowed"));
      return;
  }
}

void handleNotFound() {
  // Ответ на CORS preflight (OPTIONS) для любого пути.
  if (server.method() == HTTP_OPTIONS) {
    sendNoContent();
    return;
  }

  // Динамические маршруты /projects/{id}.
  uint32_t id;
  if (parseId(server.uri(), id)) {
    handleProjectById(id);
    return;
  }

  sendJson(404, errorJson("Not found"));
}

// --- Запуск ------------------------------------------------------------------

void setup() {
  Serial.begin(115200);

  if (!store.begin()) {
    Serial.println("[store] LittleFS mount failed");
  } else {
    Serial.printf("[store] loaded %u projects\n",
                  static_cast<unsigned>(store.all().size()));
  }

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

  server.on("/projects", HTTP_GET, handleGetProjects);
  server.on("/projects", HTTP_PUT, handleCreateProject);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.printf("[server] ready at http://%s.local/projects\n", HOSTNAME);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(500);
  }
  server.handleClient();
  delay(2);
}

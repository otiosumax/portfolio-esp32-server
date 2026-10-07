// REST API сервер портфолио программных проектов.
//
// Маршруты:
//   GET    /projects        -> [ {id, title, tags, description, githubLink,
//   imageURL}, ... ] GET    /projects/{id}   -> один проект (404, если нет) PUT
//   /projects        -> создать проект (id опционален: свой или
//   сгенерированный) -> 201 PUT    /projects/{id}   -> обновить проект (можно
//   передать только часть полей) DELETE /projects/{id}   -> удалить проект ->
//   204 GET    /storage         -> загруженность флеш-памяти и кучи
//
// id проекта допускает только латиницу и цифры (a-z, A-Z, 0-9).
// Текстовые поля — произвольный UTF-8 (в т.ч. кириллица); ответы отдаются
// с Content-Type: application/json; charset=utf-8.
//
// Защита ресурсов:
//   * Тело запроса читается порциями и не превышает kMaxRequestBodyBytes —
//     иначе 413, но RAM под большой payload не выделяется целиком.
//   * При нехватке места на флеше изменение откатывается -> 507.
//
// Данные хранятся в файле /projects.json в LittleFS (флеш-память),
// поэтому переживают перезагрузку и отключение питания.

#include <Arduino.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>

#include <ArduinoJson.h>

#include "ProjectJson.h"
#include "ProjectStore.h"
#include "wifi_credentials.h"

// Максимальный размер тела PUT-запроса. Тело тянется порциями через
// raw-механизм WebServer, поэтому даже огромный Content-Length не приведёт
// к аллокации всей памяти: лишнее отбрасывается, клиент получает 413.
static const size_t kMaxRequestBodyBytes = 16 * 1024;

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

void sendJson(int code, const String &body) {
  addCorsHeaders();
  // charset обязателен: без него клиенты декодируют ответ в системной
  // кодировке и кириллица превращается в «кракозябры».
  server.send(code, "application/json; charset=utf-8", body);
}

void sendNoContent() {
  addCorsHeaders();
  server.send(204);
}

String errorJson(const char *message) {
  JsonDocument doc;
  doc["error"] = message;
  String out;
  serializeJson(doc, out);
  return out;
}

// Преобразует результат операции хранилища в HTTP-ответ.
void sendStoreError(StoreResult result) {
  switch (result) {
  case StoreResult::NotFound:
    sendJson(404, errorJson("Project not found"));
    return;
  case StoreResult::DuplicateId:
    sendJson(409, errorJson("Project with this id already exists"));
    return;
  case StoreResult::StorageFull:
    sendJson(507, errorJson("Insufficient storage: not enough free space"));
    return;
  default:
    sendJson(500, errorJson("Unexpected storage error"));
    return;
  }
}

void sendInvalidId() {
  sendJson(400, errorJson("Invalid id: only latin letters and digits are "
                          "allowed"));
}

String serializeProject(const Project &p) {
  JsonDocument doc;
  projectToJson(doc.to<JsonObject>(), p);
  String out;
  serializeJson(doc, out);
  return out;
}

String serializeProjects() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const Project &p : store.all()) {
    projectToJson(arr.add<JsonObject>(), p);
  }
  String out;
  serializeJson(doc, out);
  return out;
}

// Результат разбора URI вида /projects/{id}.
enum class IdParse {
  NotProjectId, // URI не похож на /projects/{id}
  Invalid,      // id есть, но содержит недопустимые символы
  Ok,           // id корректен (см. isValidId)
};

// Извлекает id (только латиница и цифры) из URI вида /projects/{id}.
IdParse parseProjectId(const String &uri, String &id) {
  const char *prefix = "/projects/";
  if (!uri.startsWith(prefix)) {
    return IdParse::NotProjectId;
  }

  String tail = uri.substring(strlen(prefix));
  if (tail.isEmpty() || tail.indexOf('/') >= 0) {
    return IdParse::NotProjectId;
  }
  if (!isValidId(tail)) {
    return IdParse::Invalid;
  }

  id = tail;
  return IdParse::Ok;
}

// Разбирает тело запроса в проект целиком (включая id).
bool parseProjectBody(const String &body, Project &out) {
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    return false;
  }
  if (!doc.is<JsonObject>()) {
    return false;
  }

  projectFromJson(doc.as<JsonObjectConst>(), out);
  return true;
}

// Применяет к существующему проекту только те поля, которые реально
// переданы в теле запроса (отсутствующие поля сохраняют текущее значение).
// id телом запроса не меняется — он берётся из URL.
bool applyProjectUpdate(const Project &base, const String &body, Project &out) {
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    return false;
  }
  if (!doc.is<JsonObject>()) {
    return false;
  }

  JsonObjectConst obj = doc.as<JsonObjectConst>();
  out = base;

  if (!obj["title"].isNull()) {
    out.title = obj["title"] | "";
    trimAscii(out.title);
  }
  if (!obj["tags"].isNull()) {
    out.tags.clear();
    readStringArray(obj["tags"], out.tags);
  }
  if (!obj["description"].isNull()) {
    out.description.clear();
    trimAscii(out.description);
  }
  if (!obj["githubLink"].isNull())
    out.githubLink = obj["githubLink"] | "";
  if (!obj["imageURL"].isNull())
    out.imageURL = obj["imageURL"] | "";

  return !out.title.isEmpty();
}

// --- Ограниченный приём тела запроса --------------------------------------

// Вызывается, когда тело запроса полностью прочитано (или превысило лимит).
using BodyRequestCallback = void (*)(HTTPMethod method, const String &uri,
                                     const String &body, bool overflow);

void handleBodyRequest(HTTPMethod method, const String &uri, const String &body,
                       bool overflow);

// Обработчик методов с телом (PUT/POST/PATCH/DELETE) на путях /projects... .
// В отличие от стандартного FunctionRequestHandler, тело не буферизуется
// целиком: WebServer отдаёт его порциями в raw(), а мы храним не больше
// kMaxRequestBodyBytes. При превышении лимита тело отбрасывается, а запрос
// получает 413 — без риска исчерпать RAM.
class BoundedBodyHandler : public RequestHandler {
public:
  BoundedBodyHandler(const char *prefix, size_t maxBytes,
                     BodyRequestCallback callback)
      : prefix_(prefix), maxBytes_(maxBytes), callback_(callback) {}

  bool canHandle(HTTPMethod method, String uri) override {
    if (!matches(uri)) {
      return false;
    }
    return method == HTTP_POST || method == HTTP_PUT || method == HTTP_PATCH ||
           method == HTTP_DELETE;
  }

  // Просим WebServer стримить тело вместо буферизации.
  bool canRaw(String uri) override { return matches(uri); }

  void raw(WebServer &server, String requestUri, HTTPRaw &raw) override {
    switch (raw.status) {
    case RAW_START:
      body_ = "";
      overflow_ = false;
      responded_ = false;
      // Заранее известный объём больше лимита — даже не копим.
      if (server.clientContentLength() > 0 &&
          static_cast<size_t>(server.clientContentLength()) > maxBytes_) {
        overflow_ = true;
      }
      break;

    case RAW_WRITE:
      if (overflow_) {
        break; // лишнее просто отбрасываем
      }
      if (body_.length() + raw.currentSize <= maxBytes_) {
        body_.concat(reinterpret_cast<const char *>(raw.buf),
                     static_cast<unsigned int>(raw.currentSize));
      } else {
        overflow_ = true;
        body_ = ""; // Content-Length соврал — не храним мусор
      }
      break;

    case RAW_ABORTED:
      // Тело не догрузилось: handle() уже не вызовется, отвечаем здесь.
      if (!responded_) {
        sendJson(overflow_ ? 413 : 400,
                 errorJson(overflow_ ? "Request body too large"
                                     : "Incomplete request body"));
        responded_ = true;
      }
      break;

    default:
      break;
    }
  }

  bool handle(WebServer &server, HTTPMethod method,
              String requestUri) override {
    if (responded_) {
      return true;
    }
    callback_(method, requestUri, body_, overflow_);
    return true;
  }

private:
  bool matches(const String &uri) const {
    if (!uri.startsWith(prefix_)) {
      return false;
    }
    if (uri.length() == prefix_.length()) {
      return true;
    }
    return uri[prefix_.length()] == '/';
  }

  String prefix_;
  size_t maxBytes_;
  BodyRequestCallback callback_;
  String body_;
  bool overflow_ = false;
  bool responded_ = false;
};

// --- Обработчики маршрутов -------------------------------------------------

void handleGetProjects() { sendJson(200, serializeProjects()); }

void handleGetStorage() {
  const StorageStats s = store.stats();

  JsonDocument doc;
  doc["totalBytes"] = s.totalBytes;
  doc["usedBytes"] = s.usedBytes;
  doc["freeBytes"] = s.freeBytes;
  doc["usedPercent"] =
      s.totalBytes ? static_cast<uint32_t>(100ULL * s.usedBytes / s.totalBytes)
                   : 0;
  doc["projectCount"] = s.projectCount;
  doc["dataFile"] = store.dataPath();
  doc["dataFileBytes"] = s.dataFileBytes;

  // Состояние оперативной памяти (ESP32-S3).
  doc["freeHeapBytes"] = ESP.getFreeHeap();
  doc["minFreeHeapBytes"] = ESP.getMinFreeHeap();
  doc["maxAllocHeapBytes"] = ESP.getMaxAllocHeap();
  doc["requestBodyLimitBytes"] = kMaxRequestBodyBytes;

  String out;
  serializeJson(doc, out);
  sendJson(200, out);
}

void handleGetProject(const String &id) {
  const Project *p = store.find(id);
  if (!p) {
    sendJson(404, errorJson("Project not found"));
    return;
  }
  sendJson(200, serializeProject(*p));
}

// Диспетчер для методов с телом (см. BoundedBodyHandler).
void handleBodyRequest(HTTPMethod method, const String &uri, const String &body,
                       bool overflow) {
  if (overflow) {
    sendJson(413, errorJson("Request body too large"));
    return;
  }

  switch (method) {
  case HTTP_PUT: {
    // PUT /projects -> создание.
    if (uri == "/projects") {
      Project p;
      if (!parseProjectBody(body, p) || p.title.isEmpty()) {
        sendJson(400,
                 errorJson("Invalid body: expected JSON object with fields "
                           "id (optional), title, tags, description, "
                           "githubLink, imageURL"));
        return;
      }

      // Если id задан клиентом — он должен быть из латиницы и цифр.
      if (!p.id.isEmpty() && !isValidId(p.id)) {
        sendInvalidId();
        return;
      }

      const StoreResult result = store.create(p);
      if (result != StoreResult::Ok) {
        sendStoreError(result);
        return;
      }

      Serial.printf("[API] created project %s: %s\n", p.id.c_str(),
                    p.title.c_str());
      sendJson(201, serializeProject(p));
      return;
    }

    // PUT /projects/{id} -> обновление.
    String id;
    switch (parseProjectId(uri, id)) {
    case IdParse::Ok:
      break;
    case IdParse::Invalid:
      sendInvalidId();
      return;
    default:
      sendJson(404, errorJson("Not found"));
      return;
    }

    const Project *existing = store.find(id);
    if (!existing) {
      sendJson(404, errorJson("Project not found"));
      return;
    }

    Project updated;
    if (!applyProjectUpdate(*existing, body, updated)) {
      sendJson(400,
               errorJson("Invalid body: expected JSON object with fields "
                         "title, tags, description, githubLink, imageURL"));
      return;
    }

    const StoreResult result = store.update(id, updated);
    if (result != StoreResult::Ok) {
      sendStoreError(result);
      return;
    }

    Serial.printf("[API] updated project %s\n", id.c_str());
    sendJson(200, serializeProject(updated));
    return;
  }

  case HTTP_DELETE: {
    String id;
    switch (parseProjectId(uri, id)) {
    case IdParse::Ok:
      break;
    case IdParse::Invalid:
      sendInvalidId();
      return;
    default:
      sendJson(405, errorJson("Method not allowed"));
      return;
    }

    const StoreResult result = store.remove(id);
    if (result != StoreResult::Ok) {
      sendStoreError(result);
      return;
    }

    Serial.printf("[API] deleted project %s\n", id.c_str());
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

  // Динамические маршруты /projects/{id} (GET).
  String id;
  switch (parseProjectId(server.uri(), id)) {
  case IdParse::Ok:
    handleGetProject(id);
    return;
  case IdParse::Invalid:
    sendInvalidId();
    return;
  default:
    break;
  }

  sendJson(404, errorJson("Not found"));
}

// --- Запуск ------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(500);

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
  server.on("/storage", HTTP_GET, handleGetStorage);
  // PUT/DELETE обрабатываются ограниченным по памяти обработчиком.
  server.addHandler(new BoundedBodyHandler("/projects", kMaxRequestBodyBytes,
                                           handleBodyRequest));
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

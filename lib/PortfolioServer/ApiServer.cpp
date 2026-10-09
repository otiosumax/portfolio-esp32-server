// HTTP-слой API и SSE. Используется и прошивкой, и интеграционными тестами.

#include "ApiServer.h"

#include <ArduinoJson.h>

#include "ProjectJson.h"

namespace {

// --- Вспомогательные функции ---------------------------------------------

void sendJson(AsyncWebServerRequest* request, int code, const String& body) {
  // charset обязателен: без него клиенты декодируют ответ в системной
  // кодировке и кириллица превращается в «кракозябры».
  request->send(code, "application/json; charset=utf-8", body);
}

void sendNoContent(AsyncWebServerRequest* request) {
  request->send(204);
}

String errorJson(const char* message) {
  JsonDocument doc;
  doc["error"] = message;
  String out;
  serializeJson(doc, out);
  return out;
}

void sendStoreError(AsyncWebServerRequest* request, StoreResult result) {
  switch (result) {
    case StoreResult::NotFound:
      sendJson(request, 404, errorJson("Project not found"));
      return;
    case StoreResult::DuplicateId:
      sendJson(request, 409, errorJson("Project with this id already exists"));
      return;
    case StoreResult::StorageFull:
      sendJson(request, 507,
               errorJson("Insufficient storage: not enough free space"));
      return;
    default:
      sendJson(request, 500, errorJson("Unexpected storage error"));
      return;
  }
}

void sendInvalidId(AsyncWebServerRequest* request) {
  sendJson(request, 400,
           errorJson("Invalid id: only latin letters and digits are allowed"));
}

String serializeProject(const Project& p) {
  JsonDocument doc;
  projectToJson(doc.to<JsonObject>(), p);
  String out;
  serializeJson(doc, out);
  return out;
}

// Результат разбора URI вида /projects/{id}.
enum class IdParse {
  NotProjectId,  // URI не похож на /projects/{id}
  Invalid,       // id есть, но содержит недопустимые символы
  Ok,            // id корректен (см. isValidId)
};

// Извлекает id (только латиница и цифры) из URI вида /projects/{id}.
IdParse parseProjectId(const String& uri, String& id) {
  const char* prefix = "/projects/";
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
bool parseProjectBody(const String& body, Project& out) {
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
bool applyProjectUpdate(const Project& base, const String& body,
                        Project& out) {
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
    readDescription(obj["description"], out.description);
  }
  if (!obj["githubLink"].isNull()) out.githubLink = obj["githubLink"] | "";
  if (!obj["imageURL"].isNull()) out.imageURL = obj["imageURL"] | "";

  return !out.title.isEmpty();
}

// --- Ограниченный приём тела запроса --------------------------------------

// Обработчик методов с телом на путях /projects... . Тело собирается порциями
// в буфер, привязанный к конкретному запросу (request->_tempObject), поэтому
// параллельные запросы не мешают друг другу, а объём не превышает
// maxBytes — лишнее не копируется, клиент получает 413.
class ProjectBodyHandler : public AsyncWebHandler {
 public:
  using BodyCallback =
      std::function<void(AsyncWebServerRequest*, const String&, bool)>;

  ProjectBodyHandler(const String& prefix, size_t maxBytes,
                     BodyCallback callback)
      : prefix_(prefix), maxBytes_(maxBytes), callback_(std::move(callback)) {}

  bool canHandle(AsyncWebServerRequest* request) override {
    if (!(request->method() & (HTTP_POST | HTTP_PUT | HTTP_PATCH |
                               HTTP_DELETE))) {
      return false;
    }
    const String& url = request->url();
    return url == prefix_ || url.startsWith(prefix_ + "/");
  }

  // Просим ядро отдавать нам порции тела, а не отбрасывать их.
  bool isRequestHandlerTrivial() override { return false; }

  void handleBody(AsyncWebServerRequest* request, uint8_t* data, size_t len,
                  size_t index, size_t total) override {
    if (total == 0 || total > maxBytes_) {
      // total == 0 — нет Content-Length (напр. chunked): не копим, клиент
      // получит 400. total > лимита — не копим, клиент получит 413.
      return;
    }
    if (request->_tempObject == nullptr) {
      request->_tempObject = malloc(total + 1);
      if (request->_tempObject == nullptr) {
        return;  // не хватило кучи — тело останется пустым, клиент получит 400
      }
    }
    memcpy(static_cast<uint8_t*>(request->_tempObject) + index, data, len);
  }

  void handleRequest(AsyncWebServerRequest* request) override {
    const size_t total = request->contentLength();
    const bool overflow = total > maxBytes_;

    String body;
    if (request->_tempObject != nullptr) {
      if (!overflow) {
        uint8_t* buf = static_cast<uint8_t*>(request->_tempObject);
        buf[total] = 0;
        body = reinterpret_cast<const char*>(buf);
      }
      free(request->_tempObject);
      request->_tempObject = nullptr;
    }

    callback_(request, body, overflow);
  }

 private:
  String prefix_;
  size_t maxBytes_;
  BodyCallback callback_;
};

}  // namespace

// --- ApiServer --------------------------------------------------------------

void ApiServer::begin() {
  if (!store.begin()) {
    Serial.println("[store] LittleFS mount failed");
  } else {
    Serial.printf("[store] loaded %u projects\n",
                  static_cast<unsigned>(store.all().size()));
  }

  // CORS для всех ответов (в т.ч. SSE-потока и preflight).
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods",
                                       "GET, PUT, DELETE, OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers",
                                       "Content-Type");

  server.on("/projects", HTTP_GET,
            [this](AsyncWebServerRequest* r) { handleGetProjects(r); });
  server.on("/storage", HTTP_GET,
            [this](AsyncWebServerRequest* r) { handleGetStorage(r); });
  server.addHandler(new ProjectBodyHandler(
      "/projects", kMaxRequestBodyBytes,
      [this](AsyncWebServerRequest* r, const String& body, bool overflow) {
        handleBodyRequest(r, body, overflow);
      }));
  server.addHandler(&events);
  server.onNotFound([this](AsyncWebServerRequest* r) { handleNotFound(r); });

  events.onConnect(
      [](AsyncEventSource* source, AsyncEventSourceClient* client) {
        (void)client;
        Serial.printf("[sse] client connected (total %u)\n",
                      static_cast<unsigned>(source->count()));
      });
  events.onDisconnect(
      [](AsyncEventSource* source, AsyncEventSourceClient* client) {
        (void)client;
        Serial.printf("[sse] client disconnected (total %u)\n",
                      static_cast<unsigned>(source->count()));
      });

  server.begin();

  Serial.println("[server] API ready at /projects, SSE at /events");
}

void ApiServer::loop() {
  // SSE heartbeat: держит соединение и NAT-таблицы живыми.
  const uint32_t now = millis();
  if (now - lastHeartbeatMs_ >= kSseHeartbeatMs) {
    lastHeartbeatMs_ = now;
    events.send("", "ping", now);
  }
}

void ApiServer::handleGetProjects(AsyncWebServerRequest* request) {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const Project& p : store.all()) {
    projectToJson(arr.add<JsonObject>(), p);
  }
  String out;
  serializeJson(doc, out);
  sendJson(request, 200, out);
}

void ApiServer::handleGetStorage(AsyncWebServerRequest* request) {
  const StorageStats s = store.stats();

  JsonDocument doc;
  doc["totalBytes"] = s.totalBytes;
  doc["usedBytes"] = s.usedBytes;
  doc["freeBytes"] = s.freeBytes;
  doc["usedPercent"] = s.totalBytes ? static_cast<uint32_t>(
                                          100ULL * s.usedBytes / s.totalBytes)
                                    : 0;
  doc["projectCount"] = s.projectCount;
  doc["dataFile"] = store.dataPath();
  doc["dataFileBytes"] = s.dataFileBytes;

  // Состояние оперативной памяти (ESP32-S3).
  doc["freeHeapBytes"] = ESP.getFreeHeap();
  doc["minFreeHeapBytes"] = ESP.getMinFreeHeap();
  doc["maxAllocHeapBytes"] = ESP.getMaxAllocHeap();
  doc["requestBodyLimitBytes"] = kMaxRequestBodyBytes;
  doc["sseClients"] = events.count();

  String out;
  serializeJson(doc, out);
  sendJson(request, 200, out);
}

void ApiServer::handleGetProject(AsyncWebServerRequest* request,
                                 const String& id) {
  const Project* p = store.find(id);
  if (!p) {
    sendJson(request, 404, errorJson("Project not found"));
    return;
  }
  sendJson(request, 200, serializeProject(*p));
}

// Диспетчер для методов с телом (см. ProjectBodyHandler).
void ApiServer::handleBodyRequest(AsyncWebServerRequest* request,
                                  const String& body, bool overflow) {
  if (overflow) {
    sendJson(request, 413, errorJson("Request body too large"));
    return;
  }

  if (request->method() == HTTP_PUT) {
    const String& url = request->url();

    // PUT /projects -> создание.
    if (url == "/projects") {
      Project p;
      if (!parseProjectBody(body, p) || p.title.isEmpty()) {
        sendJson(request, 400,
                 errorJson("Invalid body: expected JSON object with fields "
                           "id (optional), title, tags, description, "
                           "githubLink, imageURL"));
        return;
      }

      // Если id задан клиентом — он должен быть из латиницы и цифр.
      if (!p.id.isEmpty() && !isValidId(p.id)) {
        sendInvalidId(request);
        return;
      }

      const StoreResult result = store.create(p);
      if (result != StoreResult::Ok) {
        sendStoreError(request, result);
        return;
      }

      Serial.printf("[API] created project %s: %s\n", p.id.c_str(),
                    p.title.c_str());
      const String payload = serializeProject(p);
      events.send(payload.c_str(), "project-created", millis());
      sendJson(request, 201, payload);
      return;
    }

    // PUT /projects/{id} -> обновление.
    String id;
    switch (parseProjectId(url, id)) {
      case IdParse::Ok:
        break;
      case IdParse::Invalid:
        sendInvalidId(request);
        return;
      default:
        sendJson(request, 404, errorJson("Not found"));
        return;
    }

    const Project* existing = store.find(id);
    if (!existing) {
      sendJson(request, 404, errorJson("Project not found"));
      return;
    }

    Project updated;
    if (!applyProjectUpdate(*existing, body, updated)) {
      sendJson(request, 400,
               errorJson("Invalid body: expected JSON object with fields "
                         "title, tags, description, githubLink, imageURL"));
      return;
    }

    const StoreResult result = store.update(id, updated);
    if (result != StoreResult::Ok) {
      sendStoreError(request, result);
      return;
    }

    Serial.printf("[API] updated project %s\n", id.c_str());
    const String payload = serializeProject(updated);
    events.send(payload.c_str(), "project-updated", millis());
    sendJson(request, 200, payload);
    return;
  }

  if (request->method() == HTTP_DELETE) {
    String id;
    switch (parseProjectId(request->url(), id)) {
      case IdParse::Ok:
        break;
      case IdParse::Invalid:
        sendInvalidId(request);
        return;
      default:
        sendJson(request, 405, errorJson("Method not allowed"));
        return;
    }

    const StoreResult result = store.remove(id);
    if (result != StoreResult::Ok) {
      sendStoreError(request, result);
      return;
    }

    Serial.printf("[API] deleted project %s\n", id.c_str());
    JsonDocument doc;
    doc["id"] = id;
    String payload;
    serializeJson(doc, payload);
    events.send(payload.c_str(), "project-deleted", millis());
    sendNoContent(request);
    return;
  }

  sendJson(request, 405, errorJson("Method not allowed"));
}

void ApiServer::handleNotFound(AsyncWebServerRequest* request) {
  // Ответ на CORS preflight (OPTIONS) для любого пути.
  if (request->method() == HTTP_OPTIONS) {
    sendNoContent(request);
    return;
  }

  // Динамические маршруты /projects/{id} (GET).
  String id;
  switch (parseProjectId(request->url(), id)) {
    case IdParse::Ok:
      handleGetProject(request, id);
      return;
    case IdParse::Invalid:
      sendInvalidId(request);
      return;
    default:
      break;
  }

  sendJson(request, 404, errorJson("Not found"));
}

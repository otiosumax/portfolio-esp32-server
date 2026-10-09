#pragma once

#include <ESPAsyncWebServer.h>

#include "ProjectStore.h"

// HTTP API и SSE-поток портфолио. Вынесены из main.cpp, чтобы
// интеграционные тесты на устройстве поднимали ровно тот же сервер,
// что и прошивка.
class ApiServer {
 public:
  // Максимальный размер тела запроса (байт).
  static constexpr size_t kMaxRequestBodyBytes = 16 * 1024;

  // Интервал SSE-heartbeat.
  static constexpr uint32_t kSseHeartbeatMs = 15000;

  AsyncWebServer server{80};
  AsyncEventSource events{"/events"};
  ProjectStore store;

  // Монтирует LittleFS, загружает проекты и поднимает сервер.
  void begin();

  // Периодический вызов: SSE-heartbeat.
  void loop();

 private:
  void handleGetProjects(AsyncWebServerRequest* request);
  void handleGetStorage(AsyncWebServerRequest* request);
  void handleGetProject(AsyncWebServerRequest* request, const String& id);
  void handleBodyRequest(AsyncWebServerRequest* request, const String& body,
                         bool overflow);
  void handleNotFound(AsyncWebServerRequest* request);

  uint32_t lastHeartbeatMs_ = 0;
};

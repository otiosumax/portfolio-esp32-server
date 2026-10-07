#include "ProjectStore.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <esp_system.h>

#include "ProjectJson.h"

bool ProjectStore::begin(const char* path) {
  path_ = path;

  // true — отформатировать раздел, если он ещё не размечан.
  if (!LittleFS.begin(true)) {
    return false;
  }

  return load();
}

const Project* ProjectStore::find(const String& id) const {
  for (const Project& p : projects_) {
    if (p.id == id) {
      return &p;
    }
  }
  return nullptr;
}

Project* ProjectStore::find(const String& id) {
  for (Project& p : projects_) {
    if (p.id == id) {
      return &p;
    }
  }
  return nullptr;
}

String ProjectStore::generateId() const {
  // 64-битный случайный id в hex; при коллизии пробуем ещё раз.
  for (int attempt = 0; attempt < 10; ++attempt) {
    char buf[17];
    snprintf(buf, sizeof(buf), "%08x%08x", esp_random(), esp_random());
    if (!find(String(buf))) {
      return String(buf);
    }
  }
  return String();
}

StoreResult ProjectStore::create(Project& project) {
  if (!project.id.isEmpty() && find(project.id)) {
    return StoreResult::DuplicateId;
  }

  Project created = project;
  if (created.id.isEmpty()) {
    created.id = generateId();
  }
  if (created.id.isEmpty()) {
    // Практически недостижимо: не удалось подобрать свободный id.
    return StoreResult::StorageFull;
  }

  projects_.push_back(created);
  if (!save()) {
    projects_.pop_back();  // откат: память и файл остаются согласованными
    return StoreResult::StorageFull;
  }

  project = created;  // отдаём наружу назначенный id
  return StoreResult::Ok;
}

StoreResult ProjectStore::update(const String& id, const Project& updated) {
  Project* p = find(id);
  if (!p) {
    return StoreResult::NotFound;
  }

  const Project backup = *p;
  p->title = updated.title;
  p->tags = updated.tags;
  p->description = updated.description;
  p->githubLink = updated.githubLink;
  p->imageURL = updated.imageURL;

  if (!save()) {
    *p = backup;  // откат
    return StoreResult::StorageFull;
  }
  return StoreResult::Ok;
}

StoreResult ProjectStore::remove(const String& id) {
  for (auto it = projects_.begin(); it != projects_.end(); ++it) {
    if (it->id != id) {
      continue;
    }

    const size_t index = it - projects_.begin();
    const Project backup = *it;
    projects_.erase(it);

    if (!save()) {
      projects_.insert(projects_.begin() + index, backup);  // откат
      return StoreResult::StorageFull;
    }
    return StoreResult::Ok;
  }
  return StoreResult::NotFound;
}

StorageStats ProjectStore::stats() const {
  StorageStats s;
  s.totalBytes = LittleFS.totalBytes();
  s.usedBytes = LittleFS.usedBytes();
  s.freeBytes = s.totalBytes > s.usedBytes ? s.totalBytes - s.usedBytes : 0;
  s.dataFileBytes = fileSize(path_);
  s.projectCount = projects_.size();
  return s;
}

size_t ProjectStore::fileSize(const String& path) const {
  File file = LittleFS.open(path, "r");
  if (!file) {
    return 0;
  }
  const size_t size = file.size();
  file.close();
  return size;
}

bool ProjectStore::load() {
  // Файла ещё нет — начнём с пустого хранилища.
  if (!LittleFS.exists(path_)) {
    return true;
  }

  File file = LittleFS.open(path_, "r");
  if (!file) {
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    return false;
  }

  projects_.clear();
  for (JsonObjectConst obj : doc["projects"].as<JsonArrayConst>()) {
    Project p;
    projectFromJson(obj, p);
    if (!p.id.isEmpty()) {
      projects_.push_back(p);
    }
  }

  return true;
}

bool ProjectStore::save() const {
  JsonDocument doc;

  JsonArray arr = doc["projects"].to<JsonArray>();
  for (const Project& p : projects_) {
    projectToJson(arr.add<JsonObject>(), p);
  }

  // Предварительная проверка места: считаем нужный объём, не собирая строку.
  // Временный файл пишется, пока старый ещё существует, поэтому нужен
  // объём needed именно в свободном месте (старый освобождается только
  // при переименовании).
  const size_t needed = measureJson(doc);
  const size_t freeBytes = LittleFS.totalBytes() - LittleFS.usedBytes();
  if (needed > freeBytes) {
    Serial.printf("[store] save aborted: need %u bytes, free %u\n",
                  static_cast<unsigned>(needed),
                  static_cast<unsigned>(freeBytes));
    return false;
  }

  // Пишем во временный файл, затем атомарно переименовываем,
  // чтобы при сбое питания не остаться с обрезанным JSON.
  const String tmpPath = path_ + ".tmp";
  File file = LittleFS.open(tmpPath, "w");
  if (!file) {
    Serial.println("[store] save failed: cannot open temp file");
    return false;
  }
  const size_t written = serializeJson(doc, file);
  file.close();
  if (written != needed) {
    Serial.printf("[store] save failed: wrote %u of %u bytes\n",
                  static_cast<unsigned>(written),
                  static_cast<unsigned>(needed));
    LittleFS.remove(tmpPath);
    return false;
  }

  LittleFS.remove(path_);
  if (!LittleFS.rename(tmpPath, path_)) {
    Serial.println("[store] save failed: rename");
    LittleFS.remove(tmpPath);
    return false;
  }
  return true;
}

#pragma once

#include <Arduino.h>
#include <vector>

// Программный проект — единица хранения.
struct Project {
  String id;
  String title;
  std::vector<String> tags;
  String description;
  String githubLink;
  String imageURL;
};

// Результат операции, изменяющей хранилище.
enum class StoreResult {
  Ok,
  NotFound,       // нет проекта с таким id
  DuplicateId,    // заданный id уже занят
  StorageFull,    // не хватило места во флеш-памяти (изменение откатано)
};

// Загруженность файловой системы.
struct StorageStats {
  size_t totalBytes = 0;
  size_t usedBytes = 0;
  size_t freeBytes = 0;
  size_t dataFileBytes = 0;
  size_t projectCount = 0;
};

// Хранилище проектов. Данные живут в оперативной памяти,
// а после каждого изменения сериализуются в JSON-файл в LittleFS,
// поэтому переживают перезагрузку устройства.
class ProjectStore {
 public:
  // Монтирует файловую систему и загружает данные из файла.
  // Возвращает false, если LittleFS не смогла смонтироваться.
  bool begin(const char* path = "/projects.json");

  // Все проекты (в порядке добавления).
  const std::vector<Project>& all() const { return projects_; }

  // Путь к файлу данных.
  const String& dataPath() const { return path_; }

  // Загруженность флеш-памяти и размер файла данных.
  StorageStats stats() const;

  const Project* find(const String& id) const;
  Project* find(const String& id);

  // Добавляет проект и сохраняет хранилище.
  // Если у проекта пустой id — генерируется случайный
  // (он записывается обратно в project).
  // DuplicateId — id уже занят; StorageFull — не хватило места.
  StoreResult create(Project& project);

  // Заменяет поля существующего проекта и сохраняет хранилище.
  // NotFound — нет такого id; StorageFull — не хватило места.
  StoreResult update(const String& id, const Project& updated);

  // Удаляет проект по id и сохраняет хранилище.
  // NotFound — нет такого id; StorageFull — не хватило места.
  StoreResult remove(const String& id);

 private:
  bool load();

  // Сериализует проекты в файл. При нехватке места возвращает false,
  // не трогая текущий файл данных.
  bool save() const;

  // Случайный 64-битный id в hex, гарантированно свободный.
  String generateId() const;

  // Размер файла или 0, если файла нет.
  size_t fileSize(const String& path) const;

  std::vector<Project> projects_;
  String path_;
};

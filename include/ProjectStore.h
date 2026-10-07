#pragma once

#include <Arduino.h>
#include <vector>

// Программный проект — единица хранения.
struct Project {
  String id;
  String title;
  std::vector<String> tags;
  std::vector<String> description;
  String githubLink;
  String imageURL;
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

  const Project* find(const String& id) const;
  Project* find(const String& id);

  // Добавляет проект и сохраняет хранилище.
  // Если у проекта пустой id — генерируется случайный.
  // Возвращает false, если заданный id уже занят.
  bool create(Project& project);

  // Заменяет поля существующего проекта и сохраняет хранилище.
  bool update(const String& id, const Project& updated);

  // Удаляет проект по id и сохраняет хранилище.
  bool remove(const String& id);

 private:
  bool load();
  bool save() const;

  // Случайный 64-битный id в hex, гарантированно свободный.
  String generateId() const;

  std::vector<Project> projects_;
  String path_;
};

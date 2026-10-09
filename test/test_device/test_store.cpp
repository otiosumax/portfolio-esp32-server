// Тесты хранилища на устройстве: CRUD, дубликаты, откаты, персистентность
// через реальную LittleFS (в т.ч. кириллица и миграция description-массива).
// Запуск: pio test -e esp32s3

#include <Arduino.h>
#include <LittleFS.h>
#include <unity.h>

#include "ProjectJson.h"
#include "ProjectStore.h"

static const char* kTestPath = "/test_projects.json";

void setUp(void) {
  LittleFS.begin(true);
  LittleFS.remove(kTestPath);
}

void tearDown(void) {
  LittleFS.remove(kTestPath);
}

void test_crud_and_persistence(void) {
  ProjectStore store;
  TEST_ASSERT_TRUE(store.begin(kTestPath));
  TEST_ASSERT_TRUE(store.all().empty());

  Project first;
  first.title = "Умный дом";
  first.tags = {String("esp32"), String("iot")};
  first.description = "Описание на русском.";
  first.githubLink = "https://github.com/me/smart-home";
  first.imageURL = "https://example.com/p.png";
  TEST_ASSERT_EQUAL(StoreResult::Ok, store.create(first));
  TEST_ASSERT_FALSE(first.id.isEmpty());

  Project second;
  second.id = "custom42";
  second.title = "Второй";
  TEST_ASSERT_EQUAL(StoreResult::Ok, store.create(second));
  TEST_ASSERT_EQUAL(2, static_cast<int>(store.all().size()));

  // Дубликат id.
  Project dup = first;
  TEST_ASSERT_EQUAL(StoreResult::DuplicateId, store.create(dup));
  TEST_ASSERT_EQUAL(2, static_cast<int>(store.all().size()));

  // Несуществующий id.
  TEST_ASSERT_EQUAL(StoreResult::NotFound, store.update("nope", first));
  TEST_ASSERT_EQUAL(StoreResult::NotFound, store.remove("nope"));

  // Обновление и удаление.
  first.title = "Обновлённый";
  TEST_ASSERT_EQUAL(StoreResult::Ok, store.update(first.id, first));
  TEST_ASSERT_EQUAL(StoreResult::Ok, store.remove(second.id));
  TEST_ASSERT_EQUAL(1, static_cast<int>(store.all().size()));

  // «Перезагрузка»: другой экземпляр читает тот же файл из LittleFS.
  ProjectStore reloaded;
  TEST_ASSERT_TRUE(reloaded.begin(kTestPath));
  TEST_ASSERT_EQUAL(1, static_cast<int>(reloaded.all().size()));

  const Project* p = &reloaded.all()[0];
  TEST_ASSERT_EQUAL_STRING(first.id.c_str(), p->id.c_str());
  TEST_ASSERT_EQUAL_STRING("Обновлённый", p->title.c_str());
  TEST_ASSERT_EQUAL_STRING("Описание на русском.", p->description.c_str());
  TEST_ASSERT_EQUAL(2, static_cast<int>(p->tags.size()));
  TEST_ASSERT_EQUAL_STRING("esp32", p->tags[0].c_str());
  TEST_ASSERT_EQUAL_STRING("iot", p->tags[1].c_str());
  TEST_ASSERT_EQUAL_STRING("https://github.com/me/smart-home",
                           p->githubLink.c_str());
  TEST_ASSERT_EQUAL_STRING("https://example.com/p.png", p->imageURL.c_str());
}

void test_legacy_description_array(void) {
  // Файл в старом формате: description — массив строк.
  File f = LittleFS.open(kTestPath, "w");
  TEST_ASSERT_TRUE(f);
  f.print("{\"projects\":[{\"id\":\"abc\",\"title\":\"Т\",\"tags\":[],"
          "\"description\":[\"строка 1\",\"строка 2\"],"
          "\"githubLink\":\"\",\"imageURL\":\"\"}]}");
  f.close();

  ProjectStore store;
  TEST_ASSERT_TRUE(store.begin(kTestPath));
  TEST_ASSERT_EQUAL(1, static_cast<int>(store.all().size()));
  TEST_ASSERT_EQUAL_STRING("строка 1\nстрока 2",
                           store.all()[0].description.c_str());
}

void test_corrupted_file_is_ignored(void) {
  File f = LittleFS.open(kTestPath, "w");
  TEST_ASSERT_TRUE(f);
  f.print("{невалидный json");
  f.close();

  ProjectStore store;
  TEST_ASSERT_FALSE(store.begin(kTestPath));
}

void test_id_validation(void) {
  TEST_ASSERT_TRUE(isValidId(String("abc123")));
  TEST_ASSERT_FALSE(isValidId(String("абв")));
  TEST_ASSERT_FALSE(isValidId(String("abc_def")));
}

void setup() {
  delay(2000);

  UNITY_BEGIN();
  RUN_TEST(test_crud_and_persistence);
  RUN_TEST(test_legacy_description_array);
  RUN_TEST(test_corrupted_file_is_ignored);
  RUN_TEST(test_id_validation);
  UNITY_END();
}

void loop() {
  delay(10);
}

// Нативные (host) тесты логики данных: JSON-преобразования, валидация id,
// UTF-8-безопасная обрезка, миграция description из массива в строку.
// Запуск: pio test -e native

#include <unity.h>

#include "ProjectJson.h"
#include "ProjectStore.h"

// --- isValidId -------------------------------------------------------------

void test_isValidId(void) {
  TEST_ASSERT_TRUE(isValidId(String("abc123")));
  TEST_ASSERT_TRUE(isValidId(String("ABC")));
  TEST_ASSERT_TRUE(isValidId(String("aB0c")));

  TEST_ASSERT_FALSE(isValidId(String("")));
  TEST_ASSERT_FALSE(isValidId(String("abc_def")));
  TEST_ASSERT_FALSE(isValidId(String("abc-def")));
  TEST_ASSERT_FALSE(isValidId(String("abc def")));
  TEST_ASSERT_FALSE(isValidId(String("абв")));
  TEST_ASSERT_FALSE(isValidId(String("abc/def")));
}

// --- trimAscii -------------------------------------------------------------

void test_trimAscii(void) {
  String s("  Привет  \n");
  trimAscii(s);
  TEST_ASSERT_EQUAL_STRING("Привет", s.c_str());

  String t("\t Тест \r\n");
  trimAscii(t);
  TEST_ASSERT_EQUAL_STRING("Тест", t.c_str());

  // Многобайтовые символы по краям трогать нельзя.
  String u("Привет");
  trimAscii(u);
  TEST_ASSERT_EQUAL_STRING("Привет", u.c_str());

  String v("   \t ");
  trimAscii(v);
  TEST_ASSERT_TRUE(v.isEmpty());

  // Кириллица не является «пробелом» — обрезка останавливается на ней.
  String w(" \tЁж\n ");
  trimAscii(w);
  TEST_ASSERT_EQUAL_STRING("Ёж", w.c_str());
}

// --- readStringArray (tags) ------------------------------------------------

void test_readStringArray(void) {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  arr.add("  esp32 ");
  arr.add("");
  arr.add("кириллица");

  std::vector<String> out;
  readStringArray(arr.as<JsonVariantConst>(), out);

  TEST_ASSERT_EQUAL(2, static_cast<int>(out.size()));
  TEST_ASSERT_EQUAL_STRING("esp32", out[0].c_str());
  TEST_ASSERT_EQUAL_STRING("кириллица", out[1].c_str());
}

// --- readDescription: строка и устаревший массив ----------------------------

void test_readDescription_string(void) {
  JsonDocument doc;
  doc["description"] = "  Описание на русском. \n";

  String out;
  readDescription(doc["description"], out);
  TEST_ASSERT_EQUAL_STRING("Описание на русском.", out.c_str());
}

void test_readDescription_legacy_array(void) {
  JsonDocument doc;
  JsonArray arr = doc["description"].to<JsonArray>();
  arr.add("строка 1");
  arr.add("строка 2");

  String out;
  readDescription(doc["description"], out);
  TEST_ASSERT_EQUAL_STRING("строка 1\nстрока 2", out.c_str());
}

// --- Полный round-trip Project -> JSON -> Project ----------------------------

void test_project_roundtrip_cyrillic(void) {
  Project p;
  p.id = "abc123";
  p.title = "Умный дом";
  p.tags = {String("esp32"), String("iot")};
  p.description = "Описание на русском.";
  p.githubLink = "https://github.com/me/x";
  p.imageURL = "https://example.com/p.png";

  JsonDocument doc;
  projectToJson(doc.to<JsonObject>(), p);

  // Сериализованный текст — валидный UTF-8 JSON.
  std::string text;
  serializeJson(doc, text);
  TEST_ASSERT_TRUE(text.find("Умный дом") != std::string::npos);

  Project q;
  projectFromJson(doc.as<JsonObjectConst>(), q);

  TEST_ASSERT_EQUAL_STRING(p.id.c_str(), q.id.c_str());
  TEST_ASSERT_EQUAL_STRING(p.title.c_str(), q.title.c_str());
  TEST_ASSERT_EQUAL(2, static_cast<int>(q.tags.size()));
  TEST_ASSERT_EQUAL_STRING("esp32", q.tags[0].c_str());
  TEST_ASSERT_EQUAL_STRING("iot", q.tags[1].c_str());
  TEST_ASSERT_EQUAL_STRING("Описание на русском.", q.description.c_str());
  TEST_ASSERT_EQUAL_STRING(p.githubLink.c_str(), q.githubLink.c_str());
  TEST_ASSERT_EQUAL_STRING(p.imageURL.c_str(), q.imageURL.c_str());
}

// --- Разбор JSON-текста (как тело HTTP-запроса) -----------------------------

void test_project_parse_from_body(void) {
  const char* body =
      "{\"id\":\"p1\",\"title\":\"Привет\",\"tags\":[\"a\",\"б\"],"
      "\"description\":\"Тест\",\"githubLink\":\"g\",\"imageURL\":\"i\"}";

  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, body));

  Project p;
  projectFromJson(doc.as<JsonObjectConst>(), p);

  TEST_ASSERT_EQUAL_STRING("p1", p.id.c_str());
  TEST_ASSERT_EQUAL_STRING("Привет", p.title.c_str());
  TEST_ASSERT_EQUAL(2, static_cast<int>(p.tags.size()));
  TEST_ASSERT_EQUAL_STRING("б", p.tags[1].c_str());
  TEST_ASSERT_EQUAL_STRING("Тест", p.description.c_str());
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_isValidId);
  RUN_TEST(test_trimAscii);
  RUN_TEST(test_readStringArray);
  RUN_TEST(test_readDescription_string);
  RUN_TEST(test_readDescription_legacy_array);
  RUN_TEST(test_project_roundtrip_cyrillic);
  RUN_TEST(test_project_parse_from_body);
  return UNITY_END();
}

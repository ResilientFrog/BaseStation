#include <unity.h>
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "logger/Logger.h"
#include "wi-fi/wiFiConnectionController.h"

// Fresh logger instance for each test (avoid global `logger` side effects)
static Logger testLogger;

void setUp(void) {
  // Runs before each test — start with clean state
  LittleFS.begin(true);
  LittleFS.remove("/logs/steps.txt");
  LittleFS.remove("/logs/data.txt");
  testLogger = Logger();
  testLogger.initialize();
  testLogger.clearLogs();
}

void tearDown(void) {
  // Runs after each test
}

// ─── Step logging ───────────────────────────────────────────

void test_log_info_creates_entry(void) {
  testLogger.logInfo("RTK", "Test message");

  String json = testLogger.getStepLogsAsJSON();
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);

  TEST_ASSERT_TRUE(err == DeserializationError::Ok);
  TEST_ASSERT_EQUAL(1, doc.as<JsonArray>().size());
  TEST_ASSERT_EQUAL_STRING("INFO", doc[0]["level"]);
  TEST_ASSERT_EQUAL_STRING("RTK", doc[0]["component"]);
  TEST_ASSERT_EQUAL_STRING("Test message", doc[0]["message"]);
}

void test_log_levels(void) {
  testLogger.logInfo("A", "info msg");
  testLogger.logWarn("B", "warn msg");
  testLogger.logError("C", "error msg");

  String json = testLogger.getStepLogsAsJSON();
  JsonDocument doc;
  deserializeJson(doc, json);

  // clearLogs() itself logs one "Logs cleared" entry, then these 3
  // Find our entries by component
  int found = 0;
  for (JsonObject entry : doc.as<JsonArray>()) {
    String comp = entry["component"].as<String>();
    if (comp == "A") { TEST_ASSERT_EQUAL_STRING("INFO",  entry["level"]); found++; }
    if (comp == "B") { TEST_ASSERT_EQUAL_STRING("WARN",  entry["level"]); found++; }
    if (comp == "C") { TEST_ASSERT_EQUAL_STRING("ERROR", entry["level"]); found++; }
  }
  TEST_ASSERT_EQUAL(3, found);
}

void test_step_log_has_timestamp(void) {
  testLogger.logInfo("WiFi", "ts check");

  String json = testLogger.getStepLogsAsJSON();
  JsonDocument doc;
  deserializeJson(doc, json);

  JsonArray arr = doc.as<JsonArray>();
  // Last entry is our "ts check"
  JsonObject last = arr[arr.size() - 1];
  TEST_ASSERT_TRUE(last["timestamp"].as<unsigned long>() >= 0);
}

// ─── Data logging ───────────────────────────────────────────

void test_log_data_accuracy(void) {
  testLogger.logDataAccuracy("SURVEY_ACCURACY", 49.12345678, 16.12345678, 250.5f, 3, 12, 1.234f);

  String json = testLogger.getDataLogsAsJSON();
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);

  TEST_ASSERT_TRUE(err == DeserializationError::Ok);
  TEST_ASSERT_EQUAL(1, doc.as<JsonArray>().size());

  JsonObject entry = doc[0];
  TEST_ASSERT_EQUAL_STRING("SURVEY_ACCURACY", entry["dataType"]);
  TEST_ASSERT_DOUBLE_WITHIN(0.0001, 49.1234, entry["latitude"].as<double>());
  TEST_ASSERT_DOUBLE_WITHIN(0.0001, 16.1234, entry["longitude"].as<double>());
  TEST_ASSERT_EQUAL(3, entry["fixType"].as<uint8_t>());
  TEST_ASSERT_EQUAL(12, entry["satellites"].as<uint8_t>());
  TEST_ASSERT_FLOAT_WITHIN(0.01, 1.234, entry["accuracyMeters"].as<float>());
}

void test_log_data_nan_becomes_null(void) {
  testLogger.logDataAccuracy("SURVEY_ACCURACY", NAN, NAN, NAN, 0, 0, NAN);

  String json = testLogger.getDataLogsAsJSON();
  JsonDocument doc;
  deserializeJson(doc, json);

  JsonObject entry = doc[0];
  TEST_ASSERT_TRUE(entry["latitude"].isNull());
  TEST_ASSERT_TRUE(entry["accuracyMeters"].isNull());
}

// ─── Rolling window (MAX_ENTRIES_IN_MEMORY = 100) ───────────

void test_max_entries_in_memory_bounded(void) {
  for (int i = 0; i < 120; i++) {
    testLogger.logInfo("Test", String("msg ") + String(i));
  }

  String json = testLogger.getStepLogsAsJSON();
  JsonDocument doc;
  deserializeJson(doc, json);

  // Should be capped at 100 (clearLogs added 1 entry + 120 = 121, trimmed to 100)
  TEST_ASSERT_LESS_OR_EQUAL(100, doc.as<JsonArray>().size());
}

// ─── Clear logs ─────────────────────────────────────────────

void test_clear_logs_empties_data(void) {
  testLogger.logDataAccuracy("RTK_STATUS", 1.0, 2.0, 3.0f, 3, 10, 0.5f);
  testLogger.clearLogs();

  String json = testLogger.getDataLogsAsJSON();
  JsonDocument doc;
  deserializeJson(doc, json);

  // After clear, data logs should be empty (only step logs have the "Logs cleared" entry)
  TEST_ASSERT_EQUAL(0, doc.as<JsonArray>().size());
}

// ─── Full logs ──────────────────────────────────────────────

void test_full_logs_has_both_keys(void) {
  testLogger.logInfo("A", "step");
  testLogger.logDataAccuracy("B", 0, 0, 0, 0, 0, 0);

  String json = testLogger.getFullLogsAsJSON();
  JsonDocument doc;
  deserializeJson(doc, json);

  TEST_ASSERT_TRUE(doc["steps"].is<JsonArray>());
  TEST_ASSERT_TRUE(doc["data"].is<JsonArray>());
  TEST_ASSERT_TRUE(doc["steps"].as<JsonArray>().size() > 0);
  TEST_ASSERT_TRUE(doc["data"].as<JsonArray>().size() > 0);
}

// ─── Statistics ─────────────────────────────────────────────

void test_statistics_counts(void) {
  testLogger.logInfo("X", "one");
  testLogger.logInfo("X", "two");
  testLogger.logDataAccuracy("D", 0, 0, 0, 0, 0, 0);

  String json = testLogger.getStatistics();
  JsonDocument doc;
  deserializeJson(doc, json);

  // stepLogs: clearLogs entry(1) + "one"(2) + "two"(3) = 3
  TEST_ASSERT_EQUAL(3, doc["stepLogsCount"].as<int>());
  TEST_ASSERT_EQUAL(1, doc["dataLogsCount"].as<int>());
}

// ─── File persistence ───────────────────────────────────────

void test_logs_persist_to_file(void) {
  testLogger.logInfo("Persist", "saved to flash");

  // Create a new logger that loads from file
  Logger freshLogger;
  freshLogger.initialize();

  String json = freshLogger.getStepLogsAsJSON();
  JsonDocument doc;
  deserializeJson(doc, json);

  // Should find our "saved to flash" entry loaded from file
  bool found = false;
  for (JsonObject entry : doc.as<JsonArray>()) {
    if (entry["message"].as<String>() == "saved to flash") {
      found = true;
      break;
    }
  }
  TEST_ASSERT_TRUE(found);
}

// ─── BaseConfig struct ──────────────────────────────────────

void test_base_config_defaults(void) {
  BaseConfig config = {};
  config.mode = MODE_INVALID;
  TEST_ASSERT_EQUAL(MODE_INVALID, config.mode);

  config.mode = MODE_SURVEY_IN;
  config.accuracy = 2.0f;
  config.duration = 60;
  TEST_ASSERT_EQUAL(MODE_SURVEY_IN, config.mode);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 2.0, config.accuracy);
  TEST_ASSERT_EQUAL(60, config.duration);
}

void test_base_config_fixed_mode(void) {
  BaseConfig config = {};
  config.mode = MODE_FIXED;
  config.latitude = 49.19506700;
  config.longitude = 16.60836100;
  config.altitude = 290.50;

  TEST_ASSERT_EQUAL(MODE_FIXED, config.mode);
  TEST_ASSERT_DOUBLE_WITHIN(0.00000001, 49.19506700, config.latitude);
  TEST_ASSERT_DOUBLE_WITHIN(0.00000001, 16.60836100, config.longitude);
  TEST_ASSERT_DOUBLE_WITHIN(0.01, 290.50, config.altitude);
}

// ─── Runner ─────────────────────────────────────────────────

void setup() {
  delay(2000);  // Wait for serial monitor
  UNITY_BEGIN();

  // Step logging
  RUN_TEST(test_log_info_creates_entry);
  RUN_TEST(test_log_levels);
  RUN_TEST(test_step_log_has_timestamp);

  // Data logging
  RUN_TEST(test_log_data_accuracy);
  RUN_TEST(test_log_data_nan_becomes_null);

  // Bounds
  RUN_TEST(test_max_entries_in_memory_bounded);

  // Clear
  RUN_TEST(test_clear_logs_empties_data);

  // Full logs & stats
  RUN_TEST(test_full_logs_has_both_keys);
  RUN_TEST(test_statistics_counts);

  // Persistence
  RUN_TEST(test_logs_persist_to_file);

  // BaseConfig struct
  RUN_TEST(test_base_config_defaults);
  RUN_TEST(test_base_config_fixed_mode);

  UNITY_END();
}

void loop() {
  // Nothing — tests run once in setup()
}

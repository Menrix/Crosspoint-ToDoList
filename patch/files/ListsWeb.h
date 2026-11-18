#pragma once
// Web addresses for to-do lists (added by the Crosspoint-ToDoList patch).
// Included once, by CrossPointWebServer.cpp. Only works while File Transfer runs.
//
//   GET  /api/lists        -> {"lists":[{"name":"Einkauf","open":3,"total":5}, ...]}
//   POST /api/lists/add    form fields: list=<name>, text=<one entry per line>
//                          -> {"list":"Einkauf","added":2,"reopened":1,"skipped":0,"message":"..."}
//
// Both only ever touch files in /Lists.

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <WebServer.h>

#include <cstdio>
#include <string>
#include <vector>

#include "../activities/reader/ListStore.h"

namespace todolist {

inline void sendJsonError(WebServer& server, const int code, const char* message) {
  JsonDocument doc;
  doc["error"] = message;
  doc["message"] = message;
  String out;
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

inline void handleListIndex(WebServer& server) {
  JsonDocument doc;
  JsonArray lists = doc["lists"].to<JsonArray>();
  if (Storage.exists(kListsDir)) {
    for (const String& file : Storage.listFiles(kListsDir, 200)) {
      const std::string path = std::string(kListsDir) + "/" + file.c_str();
      if (!isListPath(path)) continue;
      std::vector<Item> items;
      if (!loadList(path, items)) continue;
      JsonObject entry = lists.add<JsonObject>();
      entry["name"] = nameFromPath(path);
      entry["open"] = countOpen(items);
      entry["total"] = static_cast<int>(items.size());
    }
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

inline void handleListAdd(WebServer& server) {
  if (!server.hasArg("list") || !server.hasArg("text")) {
    sendJsonError(server, 400, "Send the fields 'list' and 'text'.");
    return;
  }
  const String& rawText = server.arg("text");
  if (rawText.length() > kMaxAddBytes) {
    sendJsonError(server, 413, "Too much text at once (max 32 KB).");
    return;
  }
  const std::string name = sanitizeName(server.arg("list").c_str());
  if (name.empty()) {
    sendJsonError(server, 400, "Invalid list name.");
    return;
  }

  const std::string path = pathFor(name);
  const bool isNew = !Storage.exists(path.c_str());
  std::vector<Item> items;
  if (!loadList(path, items)) {
    sendJsonError(server, 500, "Could not read the list.");
    return;
  }
  const AddResult result = merge(items, rawText.c_str());
  if (result.added + result.reopened > 0 || isNew) {
    if (!saveList(path, items)) {
      sendJsonError(server, 500, "Could not save the list.");
      return;
    }
    if (isNew) {
      ensureCover();                     // post-it cover for the home screen
      library::markLibraryIndexDirty();  // the new list shows up in the Library too
    }
  }

  char message[160];
  if (result.added + result.reopened == 0) {
    snprintf(message, sizeof(message), "Nothing new for %s", name.c_str());
  } else if (result.reopened == 0) {
    snprintf(message, sizeof(message), "%d added to %s", result.added, name.c_str());
  } else {
    snprintf(message, sizeof(message), "%d added, %d reopened in %s", result.added, result.reopened, name.c_str());
  }
  LOG_INF("TODO", "%s", message);

  JsonDocument doc;
  doc["list"] = name;
  doc["added"] = result.added;
  doc["reopened"] = result.reopened;
  doc["skipped"] = result.skipped;
  doc["message"] = message;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

}  // namespace todolist

#include "history_manager.h"
#include <cstring>
#include <stdint.h>
#include <time.h>

namespace {
constexpr uint32_t kSnapshotMagic = 0x48535431U;
constexpr uint16_t kSnapshotVersion = 1;

struct SnapshotHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t pointSize;
  uint16_t pointCount;
  uint16_t reserved;
};
} // namespace

HistoryManager::HistoryManager() { buffer.resize(MAX_POINTS); }

const HistoryPoint &HistoryManager::pointAt(size_t index) const {
  return buffer[(head + index) % MAX_POINTS];
}

void HistoryManager::appendPoint(const HistoryPoint &point) {
  if (count < MAX_POINTS) {
    buffer[(head + count) % MAX_POINTS] = point;
    ++count;
    return;
  }

  buffer[head] = point;
  head = (head + 1) % MAX_POINTS;
}

void HistoryManager::addPoint(int16_t pit, int16_t meat, int16_t set,
                              uint8_t fan) {
  unsigned long now = millis();

  bool intervalElapsed = (now - lastPointTime) >= SAMPLE_INTERVAL_MS;
  if (!intervalElapsed && count > 0) {
    return;
  }

  uint32_t timestamp = static_cast<uint32_t>(time(nullptr));
  if (timestamp == 0) {
    timestamp = static_cast<uint32_t>(now / 1000UL);
  }

  HistoryPoint point = {timestamp, pit, meat, set, fan};
  appendPoint(point);
  lastStoredPoint = point;
  lastPointTime = now;
  dirty = true;
}

String HistoryManager::getHistoryJSON() const {
  String json = "{\"type\":\"history\",\"data\":[";
  for (size_t i = 0; i < count; i++) {
    if (i > 0) {
      json += ",";
    }
    const HistoryPoint &point = pointAt(i);
    json += "{";
    json += "\"t\":" + String((int)point.timestamp) + ",";
    json += "\"p\":" + String(point.pitTemp) + ",";
    json += "\"m\":" + String(point.meatTemp) + ",";
    json += "\"s\":" + String(point.setpoint) + ",";
    json += "\"f\":" + String(point.fanPercent);
    json += "}";
  }
  json += "]";
  json += "}";
  return json;
}

String HistoryManager::getHistoryChunkJSON(size_t start, size_t maxPoints,
                                           int32_t utcOffsetSeconds) const {
  size_t safeStart = start > count ? count : start;
  size_t remaining = count - safeStart;
  size_t chunkCount = remaining > maxPoints ? maxPoints : remaining;
  size_t nextStart = safeStart + chunkCount;
  bool complete = nextStart >= count;

  String json = "{\"type\":\"historyChunk\",";
  json += "\"start\":" + String((int)safeStart) + ",";
  json += "\"count\":" + String((int)chunkCount) + ",";
  json += "\"total\":" + String((int)count) + ",";
  json += "\"nextStart\":" + String((int)nextStart) + ",";
  json += "\"complete\":" + String(complete ? "true" : "false") + ",";
  json += "\"offset\":" + String((int)utcOffsetSeconds) + ",";
  json += "\"data\":[";

  for (size_t i = 0; i < chunkCount; ++i) {
    if (i > 0) {
      json += ",";
    }
    const HistoryPoint &point = pointAt(safeStart + i);
    json += "{";
    json += "\"t\":" + String((int)point.timestamp) + ",";
    json += "\"p\":" + String(point.pitTemp) + ",";
    json += "\"m\":" + String(point.meatTemp) + ",";
    json += "\"s\":" + String(point.setpoint) + ",";
    json += "\"f\":" + String(point.fanPercent);
    json += "}";
  }

  json += "]}";
  return json;
}

std::string HistoryManager::serializeSnapshot() const {
  SnapshotHeader header = {kSnapshotMagic, kSnapshotVersion,
                           static_cast<uint16_t>(sizeof(HistoryPoint)),
                           static_cast<uint16_t>(count), 0};

  std::string data;
  data.reserve(sizeof(SnapshotHeader) + (count * sizeof(HistoryPoint)));
  data.append(reinterpret_cast<const char *>(&header), sizeof(SnapshotHeader));

  for (size_t i = 0; i < count; ++i) {
    const HistoryPoint &point = pointAt(i);
    data.append(reinterpret_cast<const char *>(&point), sizeof(HistoryPoint));
  }

  return data;
}

bool HistoryManager::restoreSnapshot(const std::string &snapshotData) {
  clear();

  if (snapshotData.size() < sizeof(SnapshotHeader)) {
    return false;
  }

  SnapshotHeader header = {};
  memcpy(&header, snapshotData.data(), sizeof(SnapshotHeader));
  if (header.magic != kSnapshotMagic || header.version != kSnapshotVersion ||
      header.pointSize != sizeof(HistoryPoint) || header.pointCount > MAX_POINTS) {
    return false;
  }

  size_t expectedSize = sizeof(SnapshotHeader) +
                        (static_cast<size_t>(header.pointCount) * sizeof(HistoryPoint));
  if (snapshotData.size() != expectedSize) {
    return false;
  }

  const char *cursor = snapshotData.data() + sizeof(SnapshotHeader);
  for (size_t i = 0; i < header.pointCount; ++i) {
    HistoryPoint point = {};
    memcpy(&point, cursor, sizeof(HistoryPoint));
    appendPoint(point);
    cursor += sizeof(HistoryPoint);
  }

  if (count > 0) {
    lastStoredPoint = pointAt(count - 1);
  }
  lastPointTime = 0;
  dirty = false;
  return true;
}

void HistoryManager::clear() {
  head = 0;
  count = 0;
  lastPointTime = 0;
  lastStoredPoint = {0, -999, -999, -999, 255};
  dirty = false;
}

#pragma once

#include <QDateTime>

// Epoch seconds, not local wall-clock fields. The host's old wxQt formatter
// applies DST even with wxDateTime::UTC on some Android devices.
inline QString XgribAndroidUtcText(qint64 seconds, bool table = false) {
  return QDateTime::fromSecsSinceEpoch(seconds, Qt::UTC).toString(
      table ? "ddd yyyy-MM-dd\nHH:mm" : "yyyy-MM-dd HH:mm:ss");
}

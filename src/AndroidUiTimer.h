#pragma once
#include <QTimer>
#include <functional>

// The host's wx timer backend is not usable by this statically linked plugin.
// Use the same native event-loop timers as the Android generator.
class AndroidUiTimer {
 public:
  void SetCallback(std::function<void()> callback) {
    QObject::connect(&timer_, &QTimer::timeout, &timer_, std::move(callback));
  }
  bool IsRunning() const { return timer_.isActive(); }
  void Start(int milliseconds, bool once = false) {
    timer_.setSingleShot(once);
    timer_.start(milliseconds);
  }
  void Stop() { timer_.stop(); }
 private:
  QTimer timer_;
};

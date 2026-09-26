#pragma once

#ifdef __OCPN__ANDROID__
#include <wx/string.h>
#include <wx/defs.h>
#include <QEventLoop>
#include <QTimer>
#include <QtAndroidExtras/QAndroidJniObject>
#include <QtAndroidExtras/QAndroidJniEnvironment>
#include <future>
#include <chrono>
#include <thread>

// Native Android selection without blocking Qt's GUI/IME callbacks.
// The same standalone adapter is carried by xGRIB and xWeatherRouting.
inline int OCPNAndroidFileSelector(wxWindow*, wxString* file,
    const wxString& caption, const wxString& initDir,
    const wxString& suggestedName, const wxString& wildcard) {
  if (!file) return wxID_CANCEL;
    // Older hosts block inside Java's FileChooserDialog (CountDownLatch).
    // Android's IME may synchronously call Qt while the Java picker gains
    // focus. Keep the Qt GUI event loop running until Java returns, otherwise
    // opening a picker after editing a field deadlocks both UI threads.
    // Only JNI runs on the worker; all wx/Qt widgets stay on the GUI thread.
    const QString directory = QString::fromUtf8(initDir.ToUTF8().data());
    const QString title = QString::fromUtf8(caption.ToUTF8().data());
    const QString suggestion = QString::fromUtf8(suggestedName.ToUTF8().data());
    const QString mask = QString::fromUtf8(wildcard.ToUTF8().data());
    auto selected = std::async(std::launch::async, [directory, title, suggestion, mask]() {
      QAndroidJniEnvironment env;
      auto activity = QAndroidJniObject::callStaticObjectMethod(
          "org/qtproject/qt5/android/QtNative", "activity",
          "()Landroid/app/Activity;");
      if (!activity.isValid()) return QString();
      auto dir = QAndroidJniObject::fromString(directory);
      auto caption = QAndroidJniObject::fromString(title);
      auto name = QAndroidJniObject::fromString(suggestion);
      auto wildcard = QAndroidJniObject::fromString(mask);
      auto result = activity.callObjectMethod("FileChooserDialog",
          "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
          dir.object<jstring>(), caption.object<jstring>(), name.object<jstring>(),
          wildcard.object<jstring>());
      if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return QString();
      }
      QString path = result.toString();
      // Scoped storage and newer hosts return immediately and expose their
      // eventual document-picker result through this polling method.
      const auto started = std::chrono::steady_clock::now();
      while (path == QStringLiteral("OK") || path == QStringLiteral("no")) {
        if (std::chrono::steady_clock::now() - started > std::chrono::minutes(5))
          return QString();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        path = activity.callObjectMethod("isFileChooserFinished",
            "()Ljava/lang/String;").toString();
        if (env->ExceptionCheck()) {
          env->ExceptionClear();
          return QString();
        }
      }
      return path;
    });
    QEventLoop loop;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
      if (selected.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
        loop.quit();
    });
    timer.start(20);
    loop.exec();
    const QString path = selected.get();
    if (!path.startsWith(QStringLiteral("file:"))) return wxID_CANCEL;
    *file = wxString::FromUTF8(path.mid(5).toUtf8().constData());
    return file->IsEmpty() ? wxID_CANCEL : wxID_OK;
}
#endif

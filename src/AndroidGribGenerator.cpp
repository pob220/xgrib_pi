#include "AndroidGribGenerator.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <future>
#include <map>
#include <mutex>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QGuiApplication>
#include <QInputMethod>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScroller>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <wx/fileconf.h>
#include <wx/filename.h>
#include "XgribPaths.h"
#include "environmental_grib/environment.h"
#include "environmental_grib/estimate.h"

namespace eg = environmental_grib;
namespace {
std::string Text(const QString& value) { return value.toUtf8().toStdString(); }
QString Text(const std::string& value) { return QString::fromUtf8(value.c_str()); }
wxString Wx(const QString& value) { return wxString::FromUTF8(Text(value).c_str()); }
QString QtText(const wxString& value) { return QString::fromUtf8(value.ToUTF8()); }

class ResizeFollower : public QObject {
 public:
  ResizeFollower(QWidget* canvas, QWidget* dialog)
      : QObject(dialog), canvas_(canvas), dialog_(dialog) {
    canvas_->installEventFilter(this);
    auto* input = QGuiApplication::inputMethod();
    connect(input, &QInputMethod::keyboardRectangleChanged, this, [this] { Fit(); });
    connect(input, &QInputMethod::visibleChanged, this, [this] { Fit(); });
  }
  void Fit() {
    const auto available = canvas_->size();
    const int width = std::max(240, std::min(1000, available.width() - 24));
    const auto* input = QGuiApplication::inputMethod();
    const int keyboard = input->isVisible() ? int(input->keyboardRectangle().height()) : 0;
    dialog_->setGeometry((available.width() - width) / 2, 12,
                         width, std::max(200, available.height() - keyboard - 24));
  }
 protected:
  bool eventFilter(QObject* object, QEvent* event) override {
    if (object == canvas_ && event->type() == QEvent::Resize) Fit();
    return false;
  }
 private:
  QWidget *canvas_, *dialog_;
};
}

struct AndroidGribGeneratorDialog::Impl {
  AndroidGribGeneratorDialog* owner;
  GribReadyCallback ready;
  QWidget* root;
  QTabWidget* tabs;
  QPushButton *generate, *cancel, *close;
  QLabel *status, *estimate;
  QPlainTextEdit* log;
  QTimer* timer;
  ResizeFollower* resize;
  std::map<std::string, QLineEdit*> fields;
  std::map<std::string, QComboBox*> choices;
  std::map<std::string, QWidget*> rows;
  QCheckBox *extend, *minor, *keep, *open;
  PlugIn_ViewPort viewport{};
  std::future<eg::EnvironmentResult> job;
  std::shared_ptr<std::atomic<bool>> cancelled;
  std::mutex logMutex;
  std::string latestProgress;
  bool running{false};

  QVBoxLayout* Page(const char* title) {
    auto* scroll = new QScrollArea(tabs);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* page = new QWidget(scroll);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);
    layout->setAlignment(Qt::AlignTop);
    scroll->setWidget(page);
    QScroller::grabGesture(scroll->viewport(), QScroller::TouchGesture);
    tabs->addTab(scroll, title);
    return layout;
  }
  void Note(QVBoxLayout* layout, const QString& text) {
    auto* label = new QLabel(text); label->setWordWrap(true); layout->addWidget(label);
  }
  QLineEdit* Field(QVBoxLayout* layout, const char* key, const char* label, const QString& value) {
    auto* row = new QWidget; rows[key] = row;
    auto* rowLayout = new QVBoxLayout(row); rowLayout->setContentsMargins(0, 0, 0, 6);
    Note(rowLayout, label); layout->addWidget(row);
    auto* field = new QLineEdit(value);
    field->setObjectName(QString("xgrib_") + key);
    fields[key] = field; rowLayout->addWidget(field); return field;
  }
  QComboBox* Choice(QVBoxLayout* layout, const char* key, const char* label,
                    std::initializer_list<std::pair<const char*, const char*>> values) {
    auto* row = new QWidget; rows[key] = row;
    auto* rowLayout = new QVBoxLayout(row); rowLayout->setContentsMargins(0, 0, 0, 6);
    Note(rowLayout, label); layout->addWidget(row);
    auto* choice = new QComboBox;
    choice->setObjectName(QString("xgrib_") + key);
    choice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    choice->setMinimumContentsLength(10);
    for (const auto& value : values) choice->addItem(value.second, value.first);
    choices[key] = choice; rowLayout->addWidget(choice); return choice;
  }
  void File(QVBoxLayout* layout, const char* key, const char* label) {
    auto* field = Field(layout, key, label, ""); field->setReadOnly(true);
    auto* button = new QPushButton(QString("Choose ") + label); rows[key]->layout()->addWidget(button);
    QObject::connect(button, &QPushButton::clicked, root, [this, field] {
      wxString path;
      if (PlatformFileSelectorDialog(owner, &path, "Choose data file",
             "/storage/emulated/0/Download", "", "*.*") == wxID_OK)
        field->setText(QtText(path));
    });
  }
  std::string Value(const char* key) const { return Text(fields.at(key)->text().trimmed()); }
  std::string Selected(const char* key) const { return Text(choices.at(key)->currentData().toString()); }
  double Number(const char* key) const {
    bool ok; const double value = fields.at(key)->text().toDouble(&ok);
    if (!ok || !std::isfinite(value)) throw std::runtime_error(std::string("Enter a number for ") + key);
    return value;
  }
  Impl(AndroidGribGeneratorDialog* dialog, GribReadyCallback callback)
      : owner(dialog), ready(std::move(callback)), root(static_cast<QWidget*>(dialog->GetHandle())) {
    root->setStyleSheet("QWidget {font-size: 20px;}"
        "QLineEdit, QComboBox, QPushButton {min-height: 48px;}"
        "QPushButton {padding-left: 10px; padding-right: 10px;}"
        "QTabBar::tab {min-height: 42px; padding: 4px 12px;}"
        "QCheckBox {min-height: 44px;} QCheckBox::indicator {width: 24px; height: 24px;}");
    auto* layout = new QVBoxLayout(root); layout->setContentsMargins(10, 10, 10, 10);
    Note(layout, "xGRIB — Generate forecast");
    tabs = new QTabWidget(root); tabs->setUsesScrollButtons(true); layout->addWidget(tabs, 1);
    auto* area = Page("Area / time");
    Note(area, "Coordinates are decimal degrees. West and south are negative. All times are UTC.");
    auto* useChart = new QPushButton("Use chart area"); area->addWidget(useChart);
    Field(area, "west", "West longitude", "-8"); Field(area, "east", "East longitude", "-2");
    Field(area, "south", "South latitude", "50"); Field(area, "north", "North latitude", "56");
    for (const char* key : {"west", "east", "south", "north"}) fields[key]->setInputMethodHints(Qt::ImhFormattedNumbersOnly);
    QObject::connect(useChart, &QPushButton::clicked, root, [this] {
      fields["west"]->setText(QString::number(viewport.lon_min, 'f', 3));
      fields["east"]->setText(QString::number(viewport.lon_max, 'f', 3));
      fields["south"]->setText(QString::number(viewport.lat_min, 'f', 3));
      fields["north"]->setText(QString::number(viewport.lat_max, 'f', 3));
    });
    Field(area, "start", "Start (UTC, YYYY-MM-DDTHH:MM:SSZ)", QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddTHH:00:00Z"));
    Note(area, "For ordinary forecast downloads, duration starts at the model cycle. Start is used for tidal/current predictions and forecast extension. Check the generated coverage below.");
    Field(area, "hours", "Duration (hours)", "24")->setInputMethodHints(Qt::ImhDigitsOnly);
    Choice(area, "step", "Forecast interval (hours)", {{"1","1"},{"3","3"},{"6","6"},{"12","12"}})->setCurrentIndex(1);
    auto* weather = Page("Weather / waves");
    Choice(weather, "weather", "Weather source", {{"gfs","NOAA GFS"}, {"noaa_hrrr","NOAA HRRR 3 km"},
      {"ukmo_ukv","Met Office UKV 2 km"}, {"metno_nordic","MET Norway Nordic"}, {"dwd_icon_eu","DWD ICON-EU"},
      {"ecmwf_ifs_open","ECMWF IFS Open Data"}, {"ecmwf_aifs_open","ECMWF AIFS Open Data"},
      {"existing-file","Existing weather GRIB"}, {"none","None"}});
    Choice(weather, "preset", "Weather fields", {{"minimal","Wind only"}, {"routing","Routing: wind, pressure, temperature"},
      {"marine","Marine"}, {"all","All supported fields"}})->setCurrentIndex(1);
    Choice(weather, "wave", "Waves", {{"none","None"},{"gfs_wave","NOAA GFS Wave"},{"copernicus_global_waves","Copernicus Global Waves"}});
    File(weather, "weatherFile", "weather GRIB");
    Field(weather, "weatherGrid", "Weather output grid spacing (degrees)", "0.025");
    Note(weather, "Regional sources must cover the area and forecast period. The shared generator validates their actual coverage.");
    auto* currents = Page("Currents");
    Choice(currents, "current", "Current source", {{"none","None"}, {"existing-file","Existing current GRIB"},
      {"offline-tidal","Offline tidal package (.xtd)"}, {"tpxo-cache","TPXO cache"}, {"tpxo","TPXO model"},
      {"marine_ie_irish_sea","Marine.ie Irish Sea"}, {"copernicus_nws","Copernicus NWS"}, {"copernicus_ibi","Copernicus IBI"},
      {"copernicus_mediterranean","Copernicus Mediterranean"}, {"copernicus_global","Copernicus Global"},
      {"noaa_rtofs_global","NOAA RTOFS"}, {"noaa_ofs_s111","NOAA OFS / S-111"}, {"auto","Automatic model provider"}, {"netcdf","Local NetCDF"}});
    File(currents, "currentFile", "current GRIB"); File(currents, "tidalFile", "offline tidal package (.xtd)");
    Choice(currents, "offlineMode", "Offline current calculation", {{"tide-only","Astronomical tide only"},{"tide-expected-seasonal","Tide + seasonal circulation"}});
    File(currents, "cacheFile", "TPXO cache"); File(currents, "netcdfFile", "current NetCDF");
    Field(currents, "modelDir", "TPXO model directory (accessible to OpenCPN)", "");
    Field(currents, "currentGrid", "Current grid spacing (degrees)", "0.05");
    minor = new QCheckBox("Infer minor tidal constituents"); minor->setChecked(true); currents->addWidget(minor);
    auto* options = Page("Options");
    Field(options, "username", "Copernicus username (if required)", "");
    Field(options, "password", "Copernicus password (not saved)", "")->setEchoMode(QLineEdit::Password);
    extend = new QCheckBox("Extend using fallback models"); options->addWidget(extend);
    Choice(options, "fallbackWeather", "Weather fallback", {{"none","None"},{"gfs","NOAA GFS"},{"ecmwf_ifs_open","ECMWF IFS"}});
    Choice(options, "fallbackWave", "Wave fallback", {{"none","None"},{"gfs_wave","NOAA GFS Wave"}});
    Choice(options, "fallbackCurrent", "Current fallback", {{"none","None"},{"offline-tidal","Offline tides"},{"copernicus_global","Copernicus Global"}});
    Field(options, "filename", "Output filename", "xgrib_" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss") + ".grb2");
    keep = new QCheckBox("Keep intermediate downloads"); options->addWidget(keep);
    open = new QCheckBox("Open completed GRIB on chart"); open->setChecked(true); options->addWidget(open);
    estimate = new QLabel; estimate->setWordWrap(true); layout->addWidget(estimate);
    Note(options, "Output is stored in OpenCPN’s xGRIB generated folder. Processing runs on the tablet; online providers need an internet connection.");
    status = new QLabel("Ready. Choose an area, time and sources, then Generate."); status->setWordWrap(true); layout->addWidget(status);
    log = new QPlainTextEdit; log->setReadOnly(true); log->setMaximumBlockCount(150); log->setMaximumHeight(110); log->hide(); layout->addWidget(log);
    auto* actions = new QHBoxLayout;
    generate = new QPushButton("Generate"); cancel = new QPushButton("Cancel job"); close = new QPushButton("Close"); cancel->setEnabled(false);
    actions->addWidget(generate, 1); actions->addWidget(cancel, 1); actions->addWidget(close, 1); layout->addLayout(actions);
    QObject::connect(generate, &QPushButton::clicked, root, [this] { Start(); });
    QObject::connect(cancel, &QPushButton::clicked, root, [this] { Cancel(); });
    QObject::connect(close, &QPushButton::clicked, root, [this] { Close(); });
    timer = new QTimer(root); QObject::connect(timer, &QTimer::timeout, root, [this] { Poll(); });
    resize = new ResizeFollower(static_cast<QWidget*>(owner->GetParent()->GetHandle()), root);
    for (auto& [key, field] : fields) {
      if (key == "password" || key == "start" || key == "filename") continue;
      wxString saved;
      if (auto* config = GetOCPNConfigObject(); config && config->Read("/Settings/xGRIB/AndroidGenerator/" + wxString(key), &saved)) field->setText(QtText(saved));
    }
    for (auto& [key, choice] : choices) {
      wxString saved;
      if (auto* config = GetOCPNConfigObject(); config && config->Read("/Settings/xGRIB/AndroidGenerator/" + wxString(key), &saved)) {
        const int index = choice->findData(QtText(saved)); if (index >= 0) choice->setCurrentIndex(index);
      }
    }
    for (const auto& entry : fields)
      QObject::connect(entry.second, &QLineEdit::textChanged, root, [this] { UpdateOptions(); });
    for (const auto& entry : choices)
      QObject::connect(entry.second, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                       root, [this] { UpdateOptions(); });
    QObject::connect(extend, &QCheckBox::toggled, root, [this] { UpdateOptions(); });
    UpdateOptions();
  }
  ~Impl() { if (cancelled) cancelled->store(true); if (job.valid()) job.wait(); }
  void Cancel() {
    if (cancelled) cancelled->store(true);
    status->setText("Cancelling… waiting for the current operation to stop."); cancel->setEnabled(false);
  }
  void Close() {
    if (running) { Cancel(); return; }
    if (auto* config = GetOCPNConfigObject()) {
      for (const auto& [key, field] : fields)
        if (key != "password" && key != "start" && key != "filename") config->Write("/Settings/xGRIB/AndroidGenerator/" + wxString(key), Wx(field->text()));
      for (const auto& [key, choice] : choices) config->Write("/Settings/xGRIB/AndroidGenerator/" + wxString(key), Wx(choice->currentData().toString()));
    }
    fields["password"]->clear(); owner->EndModal(wxID_CLOSE);
  }
  eg::EnvironmentRequest Request(bool preparing = true) {
    eg::EnvironmentRequest request;
    request.bbox = {Number("west"), Number("south"), Number("east"), Number("north")}; request.bbox.Validate();
    request.start = eg::ParseUtcDateTime(Value("start"));
    const double hours = Number("hours");
    if (hours < 1 || hours > 384 || hours != std::floor(hours)) throw std::runtime_error("Duration must be a whole number from 1 to 384 hours");
    request.hours = static_cast<int>(hours); request.step_hours = std::stoi(Selected("step")); request.wave_step_hours = request.step_hours;
    request.weather_provider = Selected("weather"); request.weather_preset = Selected("preset");
    request.include_waves = Selected("wave") != "none"; request.wave_provider = Selected("wave"); request.current_source = Selected("current");
    request.weather_grid_spacing_deg = Number("weatherGrid"); request.current_grid_spacing_deg = Number("currentGrid");
    if (request.weather_grid_spacing_deg <= 0 || request.current_grid_spacing_deg <= 0) throw std::runtime_error("Grid spacing must be positive");
    auto path = [&](const char* key, std::optional<std::filesystem::path>& target) { const auto value = Value(key); if (!value.empty()) target = value; };
    path("weatherFile", request.weather_file); path("currentFile", request.current_file); path("tidalFile", request.offline_tidal_file);
    path("cacheFile", request.input_cache); path("netcdfFile", request.input_netcdf); path("modelDir", request.tpxo_model_directory);
    request.offline_current_mode = Selected("offlineMode"); request.infer_minor_tides = minor->isChecked();
    request.copernicus_username = Value("username"); request.copernicus_password = Value("password"); request.extend_forecast = extend->isChecked();
    request.fallback_weather_provider = Selected("fallbackWeather"); request.fallback_wave_provider = Selected("fallbackWave"); request.fallback_current_source = Selected("fallbackCurrent");
    request.parallel_components = false; request.keep_intermediate = keep->isChecked();
    const std::string filename = Value("filename");
    if (filename.empty() || filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos || filename == "." || filename == "..") throw std::runtime_error("Enter an output filename without folders");
    const auto directory = std::filesystem::path(GetPluginDataDir("xgrib_pi").ToStdString()) / "generated";
    if (preparing) std::filesystem::create_directories(directory);
    request.output = directory / filename;
    if (preparing && std::filesystem::exists(request.output)) throw std::runtime_error("That output already exists. Choose another filename in Options.");
    request.download_directory = directory / "downloads"; request.execution.cancelled = cancelled;
    request.execution.ca_bundle = (GetXgribDataDirectory() + "android-ca-bundle.pem").ToStdString();
    return request;
  }
  void UpdateOptions() {
    const auto weather = Selected("weather"), current = Selected("current");
    rows["weatherFile"]->setVisible(weather == "existing-file");
    rows["preset"]->setVisible(weather != "none" && weather != "existing-file");
    rows["weatherGrid"]->setVisible(weather == "metno_nordic" || weather == "ukmo_ukv");
    rows["currentFile"]->setVisible(current == "existing-file");
    rows["cacheFile"]->setVisible(current == "tpxo-cache");
    rows["modelDir"]->setVisible(current == "tpxo");
    rows["netcdfFile"]->setVisible(current == "netcdf");
    const bool offline = current == "offline-tidal" || (extend->isChecked() && Selected("fallbackCurrent") == "offline-tidal");
    rows["tidalFile"]->setVisible(offline); rows["offlineMode"]->setVisible(offline);
    minor->setVisible(offline || current == "tpxo" || current == "tpxo-cache");
    rows["currentGrid"]->setVisible(current != "none" && current != "existing-file");
    const bool credentials = current.find("copernicus") == 0 || current == "auto" ||
        Selected("wave").find("copernicus") == 0 ||
        (extend->isChecked() && Selected("fallbackCurrent").find("copernicus") == 0);
    rows["username"]->setVisible(credentials); rows["password"]->setVisible(credentials);
    for (const char* key : {"fallbackWeather", "fallbackWave", "fallbackCurrent"}) rows[key]->setVisible(extend->isChecked());
    if (running) return;
    try {
      const auto size = eg::EstimateEnvironment(Request(false));
      const auto numeric = size.isMember("decodedBytes") ? "Decoded numeric data: " : "Known numeric data (partial): ";
      QString message = QString(numeric) + QString::number(size["knownDecodedBytes"].asUInt64() / 1048576.0, 'f', 1) + " MiB.";
      if (size.isMember("fileUpperBytes")) message += " File planning upper estimate: " + QString::number(size["fileUpperBytes"].asUInt64() / 1048576.0, 'f', 1) + " MiB.";
      message += " Generation and routing need additional RAM.";
      estimate->setText(message);
    } catch (const std::exception&) { estimate->setText("Size estimate available when the area, time and source inputs are valid."); }
  }
  void Start() {
    if (running) return;
    try {
      cancelled = std::make_shared<std::atomic<bool>>(false); auto request = Request();
      latestProgress.clear(); log->clear(); log->show();
      job = std::async(std::launch::async, [this, request = std::move(request)] {
        auto result = eg::GenerateEnvironment(request, {}, std::nullopt,
            [this, flag = request.execution.cancelled](const std::string& stage, const Json::Value&) {
              if (flag->load()) throw std::runtime_error("Generation cancelled");
              std::lock_guard<std::mutex> lock(logMutex); latestProgress = stage;
            });
        if (request.execution.cancelled->load()) throw std::runtime_error("Generation cancelled");
        return result;
      });
      running = true; tabs->setEnabled(false); generate->setEnabled(false); cancel->setEnabled(true); close->setEnabled(false);
      status->setText("Generating forecast…"); timer->start(150);
    } catch (const std::exception& error) { status->setText(Text(std::string(error.what()))); }
  }
  void Poll() {
    if (!running) return;
    { std::lock_guard<std::mutex> lock(logMutex); if (!latestProgress.empty()) { log->appendPlainText(Text(latestProgress)); latestProgress.clear(); } }
    if (job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    timer->stop(); running = false; tabs->setEnabled(true); generate->setEnabled(true); cancel->setEnabled(false); close->setEnabled(true);
    try {
      const auto result = job.get();
      status->setText(QString("Ready: %1 messages, %2 MiB. %3").arg(static_cast<qulonglong>(result.message_count))
          .arg(result.byte_count / 1048576.0, 0, 'f', 2).arg(Text(result.output.filename().string())));
      log->appendPlainText(Text(result.output.string()));
      if (result.inspection.isMember("first_valid_time") &&
          result.inspection.isMember("last_valid_time"))
        log->appendPlainText(Text("File coverage (UTC): " +
            result.inspection["first_valid_time"].asString() + " to " +
            result.inspection["last_valid_time"].asString() +
            ". Individual fields can have shorter coverage."));
      if (open->isChecked() && ready) ready(wxString::FromUTF8(result.output.string().c_str()));
      // Keep repeated jobs convenient without ever overwriting a previous GRIB.
      fields["filename"]->setText("xgrib_" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss") + ".grb2");
    } catch (const std::exception& error) { status->setText(cancelled->load() ? "Generation cancelled." : Text(std::string(error.what()))); }
  }
};

AndroidGribGeneratorDialog::AndroidGribGeneratorDialog(wxWindow* parent, GribReadyCallback ready)
    : wxDialog(parent, wxID_ANY, "xGRIB", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      impl_(std::make_unique<Impl>(this, std::move(ready))) {
  Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { impl_->Close(); });
}
AndroidGribGeneratorDialog::~AndroidGribGeneratorDialog() = default;
void AndroidGribGeneratorDialog::ShowMobile(const PlugIn_ViewPort& viewport) {
  impl_->viewport = viewport; impl_->resize->Fit(); ShowModal();
}

int AndroidChooseForecast(wxWindow* parent, const wxArrayString& times, int selected) {
  if (times.empty()) return wxNOT_FOUND;
  wxDialog dialog(parent, wxID_ANY, "Forecast time", wxDefaultPosition,
      wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  auto* root = static_cast<QWidget*>(dialog.GetHandle());
  root->setStyleSheet("QWidget {font-size: 22px;} QPushButton {min-height: 52px;}");
  auto* layout = new QVBoxLayout(root);
  layout->addWidget(new QLabel("Select forecast time"));
  auto* list = new QListWidget(root);
  for (const auto& time : times) {
    auto* item = new QListWidgetItem(QtText(time), list);
    item->setSizeHint(QSize(0, 56));
  }
  list->setCurrentRow(selected);
  QScroller::grabGesture(list->viewport(), QScroller::TouchGesture);
  layout->addWidget(list, 1);
  auto* actions = new QHBoxLayout;
  auto* use = new QPushButton("Use selected time");
  auto* cancel = new QPushButton("Cancel");
  actions->addWidget(use); actions->addWidget(cancel); layout->addLayout(actions);
  QObject::connect(use, &QPushButton::clicked, root, [&] {
    if (list->currentRow() >= 0) dialog.EndModal(wxID_OK);
  });
  QObject::connect(cancel, &QPushButton::clicked, root, [&] { dialog.EndModal(wxID_CANCEL); });
  dialog.Bind(wxEVT_CLOSE_WINDOW, [&](wxCloseEvent&) { dialog.EndModal(wxID_CANCEL); });
  auto* resize = new ResizeFollower(static_cast<QWidget*>(parent->GetHandle()), root);
  resize->Fit();
  return dialog.ShowModal() == wxID_OK ? list->currentRow() : wxNOT_FOUND;
}

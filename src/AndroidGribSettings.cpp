#include "AndroidGribGenerator.h"
#include "GribSettingsDialog.h"
#include "TimeZoneDisplay.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScroller>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <vector>

namespace {
QVBoxLayout* ScrollPage(QStackedWidget* stack) {
  auto* scroll = new QScrollArea;
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* page = new QWidget;
  auto* layout = new QVBoxLayout(page);
  layout->setAlignment(Qt::AlignTop);
  scroll->setWidget(page);
  QScroller::grabGesture(scroll->viewport(), QScroller::TouchGesture);
  stack->addWidget(scroll);
  return layout;
}
void Note(QVBoxLayout* layout, const QString& text) {
  auto* label = new QLabel(text); label->setWordWrap(true); layout->addWidget(label);
}
void Check(QVBoxLayout* layout, const char* label, bool& value) {
  auto* field = new QCheckBox(label); field->setChecked(value); layout->addWidget(field);
  QObject::connect(field, &QCheckBox::toggled, field, [&value](bool v) { value = v; });
}
template<typename T>
void Number(QVBoxLayout* layout, const char* label, T& value, double low,
            double high, int decimals = 0) {
  Note(layout, label);
  auto* field = new QDoubleSpinBox;
  field->setDecimals(decimals); field->setRange(low, high); field->setValue(value);
  field->setSingleStep(decimals ? 0.1 : 1.0); layout->addWidget(field);
  QObject::connect(field, static_cast<void(QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
                   field, [&value](double v) { value = static_cast<T>(v); });
}
void Choice(QVBoxLayout* layout, const char* label, int& value, const QStringList& names) {
  Note(layout, label);
  auto* field = new QComboBox; field->addItems(names); field->setCurrentIndex(value);
  field->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  field->setMinimumContentsLength(8); layout->addWidget(field);
  QObject::connect(field, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                   field, [&value](int v) { value = v; });
}
}

bool AndroidGribSettings(wxWindow* parent, GribOverlaySettings& settings) {
  // All edits are staged. Cancel/Android Back must leave persisted settings alone.
  auto edited = settings;
  bool accepted = false;
  wxDialog dialog(parent, wxID_ANY, "xGRIB settings", wxDefaultPosition,
                  wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  auto* root = static_cast<QWidget*>(dialog.GetHandle());
  root->setStyleSheet("QWidget {font-size: 20px;}"
      "QPushButton, QComboBox, QDoubleSpinBox, QLineEdit {min-height: 48px;}"
      "QComboBox QAbstractItemView::item {min-height: 48px;}"
      "QCheckBox {min-height: 48px;} QCheckBox::indicator {width: 24px; height: 24px;}");
  auto* layout = new QVBoxLayout(root);
  Note(layout, "xGRIB — Display and playback settings");
  auto* category = new QComboBox;
  category->addItem("General / playback");
  for (int i = 0; i < GribOverlaySettings::SETTINGS_COUNT; ++i)
    category->addItem(QString::fromUtf8(GribOverlaySettings::NameFromIndex(i).ToUTF8()));
  layout->addWidget(category);
  auto* stack = new QStackedWidget; layout->addWidget(stack, 1);
  QObject::connect(category, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                   stack, &QStackedWidget::setCurrentIndex);
  auto* general = ScrollPage(stack);
  Note(general, "Scroll for more controls. Save and close applies changes; Cancel returns to the chart without changing them.");
  Check(general, "Use configured display time zone (otherwise UTC)", edited.m_bUseLocalTimeZone);
  Note(general, "Configured zone: " + QString::fromUtf8(edited.m_sDisplayTimeZone.ToUTF8()));
  Check(general, "Interpolate between forecast times", edited.m_bInterpolate);
  Check(general, "Loop playback", edited.m_bLoopMode);
  Choice(general, "Loop starts at", edited.m_LoopStartPoint, {"First forecast", "Current time"});
  Number(general, "Playback speed (1 slow — 10 fast)", edited.m_UpdatesPerSecond, 1, 10);
  Choice(general, "Interpolation interval", edited.m_SlicesPerUpdate,
         {"2 minutes", "5 minutes", "10 minutes", "20 minutes", "30 minutes", "1 hour", "90 minutes", "3 hours", "6 hours", "12 hours", "24 hours"});
  Number(general, "Overlay opacity (0 transparent — 254 opaque)", edited.m_iOverlayTransparency, 0, 254);
  std::vector<std::pair<QLineEdit*, wxColour*>> colours;
  using S = GribOverlaySettings;
  for (int i = 0; i < S::SETTINGS_COUNT; ++i) {
    auto* page = ScrollPage(stack);
    auto& data = edited.Settings[i];
    QStringList units;
    switch (i) {
      case S::WIND: case S::WIND_GUST: units = QStringList{"knots", "m/s", "mph", "km/h", "Beaufort"}; break;
      case S::CURRENT: units = QStringList{"knots", "m/s", "mph", "km/h"}; break;
      case S::PRESSURE: units = QStringList{"hPa / millibars", "mmHg", "inHg"}; break;
      case S::WAVE: case S::GEO_ALTITUDE: units = QStringList{"metres", "feet"}; break;
      case S::AIR_TEMPERATURE: case S::SEA_TEMPERATURE: units = QStringList{"Celsius", "Fahrenheit"}; break;
      case S::PRECIPITATION: units = QStringList{"millimetres", "inches"}; break;
      case S::CAPE: units = QStringList{"J/kg"}; break;
      case S::COMP_REFL: units = QStringList{"dBZ"}; break;
      default: units = QStringList{"percent"}; break;
    }
    Choice(page, "Units", data.m_Units, units);
    if (i == S::WIND) {
      Check(page, "Wind barbs", data.m_bBarbedArrows);
      Check(page, "Always visible wind barbs", data.m_iBarbedVisibility);
      Choice(page, "Barb colour", data.m_iBarbedColour, {"Default colour", "Controlled colours"});
      Check(page, "Fixed wind-barb spacing", data.m_bBarbArrFixSpac);
      Number(page, "Wind-barb spacing (pixels)", data.m_iBarbArrSpacing, 10, 200);
    }
    if (i == S::WIND || i == S::WIND_GUST || i == S::PRESSURE ||
        i == S::AIR_TEMPERATURE || i == S::SEA_TEMPERATURE || i == S::CAPE || i == S::COMP_REFL) {
      Check(page, "Contour lines / isobars", data.m_bIsoBars);
      Note(page, "Spacing is in the selected units (0.03 inHg steps for pressure in inHg).");
      Number(page, "Contour spacing", data.m_iIsoBarSpacing, 1, 1000);
      Check(page, "Always visible contours", data.m_iIsoBarVisibility);
      if (i == S::PRESSURE) Check(page, "Abbreviated isobar labels", data.m_bAbbrIsoBarsNumbers);
    }
    if (i == S::CURRENT || i == S::WAVE) {
      Check(page, "Direction arrows / wave symbols", data.m_bDirectionArrows);
      QStringList forms{"Single arrow", "Double arrow", "Proportional arrow"};
      if (i == S::WAVE) forms << "Wave crests with travel marker" << "Height circles with direction tick";
      Choice(page, "Symbol style", data.m_iDirectionArrowForm, forms);
      Number(page, "Symbol baseline size (pixels)", data.m_iDirectionArrowSizePixels, i == S::WAVE ? 8 : 6, 40);
      if (i == S::CURRENT)
        Number(page, "Proportional arrow growth (pixels / knot)", data.m_dDirectionArrowGrowthPerKnot, 0, 12, 1);
      Note(page, "Symbol colour (#RRGGBB)");
      auto* colour = new QLineEdit(QString::fromUtf8(data.m_DirectionArrowColour.GetAsString(wxC2S_HTML_SYNTAX).ToUTF8()));
      page->addWidget(colour); colours.emplace_back(colour, &data.m_DirectionArrowColour);
      Check(page, "Fixed symbol spacing", data.m_bDirArrFixSpac);
      Number(page, "Symbol spacing (pixels)", data.m_iDirArrSpacing, 24, 120);
    }
    if (i != S::PRESSURE) {
      Check(page, "Colour overlay", data.m_bOverlayMap);
      Choice(page, "Colour scale", data.m_iOverlayMapColors,
             {"Generic", "Wind", "Air temperature", "Sea temperature", "Rainfall", "Cloud", "Current", "CAPE", "Reflectivity", "Windy"});
    }
    Check(page, "Numeric values on chart", data.m_bNumbers);
    Check(page, "Fixed numeric-value spacing", data.m_bNumFixSpac);
    Number(page, "Number spacing (pixels)", data.m_iNumbersSpacing, 10, 200);
    if (i == S::WIND || i == S::CURRENT) {
      Check(page, "Animated particles", data.m_bParticles);
      Number(page, "Particle density", data.m_dParticleDensity, 0.01, 20, 2);
    }
  }
  auto* error = new QLabel; error->setWordWrap(true); layout->addWidget(error);
  auto* actions = new QHBoxLayout;
  auto* save = new QPushButton("Save and close");
  auto* cancel = new QPushButton("Cancel");
  actions->addWidget(save); actions->addWidget(cancel); layout->addLayout(actions);
  QObject::connect(save, &QPushButton::clicked, root, [&] {
    root->clearFocus();
    for (const auto& entry : colours) {
      wxColour colour(wxString::FromUTF8(entry.first->text().toUtf8().constData()));
      if (!colour.IsOk()) { error->setText("Enter a valid symbol colour, for example #000000."); return; }
      *entry.second = colour;
    }
    accepted = true;
    dialog.EndModal(wxID_OK);
  });
  QObject::connect(cancel, &QPushButton::clicked, root, [&] { dialog.EndModal(wxID_CANCEL); });
  dialog.Bind(wxEVT_CLOSE_WINDOW, [&](wxCloseEvent&) { dialog.EndModal(wxID_CANCEL); });
  AndroidFitDialog(parent, dialog);
  // The bundled wxQt modal return code can report OK for a nonzero cancel
  // code. Trust the explicit action, never the wrapper's translated result.
  dialog.ShowModal();
  if (!accepted) return false;
  settings = edited;
  return true;
}

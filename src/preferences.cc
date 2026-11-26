// vim:ts=4

// Local:
#include "preferences.h"

// Qt:
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSettings>
#include <QVBoxLayout>

// Standard:
#include <cmath>


namespace mawari {

QString
navigation_mode_name (NavigationMode const mode)
{
	switch (mode)
	{
		case NavigationMode::ClickAndDrag:
			return Translations::tr ("Click and drag");

		case NavigationMode::AutoPanning:
			return Translations::tr ("Auto panning");

		case NavigationMode::HoldToPan:
			return Translations::tr ("Hold to pan");
	}

	return {};
}


QKeySequence
navigation_mode_shortcut (NavigationMode const mode)
{
	switch (mode)
	{
		case NavigationMode::ClickAndDrag:
			return QKeySequence (Qt::AltModifier | Qt::Key_D);

		case NavigationMode::AutoPanning:
			return QKeySequence (Qt::AltModifier | Qt::Key_A);

		case NavigationMode::HoldToPan:
			return QKeySequence (Qt::AltModifier | Qt::Key_H);
	}

	return {};
}


Preferences
Preferences::load()
{
	QSettings settings;
	Preferences result;
	bool valid = false;

	if (settings.value ("view/projection").toString() == "smallPlanet")
		result.projection = ProjectionMode::SmallPlanet;

	auto const fov = settings.value ("view/defaultFov", result.default_fov).toDouble (&valid);
	auto const maximum_fov = result.projection == ProjectionMode::SmallPlanet ? 350.0 : 150.0;

	if (valid && std::isfinite (fov) && fov >= 10.0 && fov <= maximum_fov)
		result.default_fov = fov;

	if (settings.value ("view/fovAxis").toString() == "vertical")
		result.fov_axis = FovAxis::Vertical;

	if (settings.value ("view/navigation").toString() == "autoPanning")
		result.navigation = NavigationMode::AutoPanning;
	else if (settings.value ("view/navigation").toString() == "holdToPan")
		result.navigation = NavigationMode::HoldToPan;

	result.language = language_from_code (settings.value ("ui/language", "system").toString());
	auto const sensitivity = settings.value ("view/holdPanSensitivity", result.hold_pan_sensitivity).toDouble (&valid);
	if (valid && std::isfinite (sensitivity) && sensitivity >= Preferences::minimum_hold_pan_sensitivity
		&& sensitivity <= Preferences::maximum_hold_pan_sensitivity)
		result.hold_pan_sensitivity = sensitivity;
	return result;
}


QString
projection_mode_name (ProjectionMode const mode)
{
	return mode == ProjectionMode::SmallPlanet ? Translations::tr ("Small planet") : Translations::tr ("Standard");
}


QKeySequence
projection_mode_shortcut (ProjectionMode const mode)
{
	return QKeySequence (Qt::AltModifier | (mode == ProjectionMode::Standard ? Qt::Key_1 : Qt::Key_2));
}


void
Preferences::save() const
{
	QSettings settings;
	settings.setValue ("ui/language", language_code (this->language));
	settings.setValue ("view/holdPanSensitivity", this->hold_pan_sensitivity);
	settings.setValue ("view/defaultFov", this->default_fov);
	settings.setValue ("view/fovAxis", this->fov_axis == FovAxis::Horizontal ? "horizontal" : "vertical");
	auto const navigation = this->navigation == NavigationMode::AutoPanning ? "autoPanning"
		: this->navigation == NavigationMode::HoldToPan ? "holdToPan" : "clickAndDrag";
	settings.setValue ("view/navigation", navigation);
	settings.setValue ("view/projection", this->projection == ProjectionMode::SmallPlanet ? "smallPlanet" : "standard");
}


PreferencesDialog::PreferencesDialog (Preferences const& preferences, QWidget* const parent):
	QDialog (parent),
	_initial_preferences (preferences),
	_fov (new QDoubleSpinBox (this)),
	_axis (new QComboBox (this)),
	_navigation (new QComboBox (this)),
	_projection (new QComboBox (this)),
	_language (new QComboBox (this))
{
	setWindowTitle (Translations::tr ("Preferences"));
	setMinimumWidth (360);
	_fov->setRange (10.0, preferences.projection == ProjectionMode::SmallPlanet ? 350.0 : 150.0);
	_fov->setDecimals (1);
	_fov->setSuffix (QString::fromUtf8 ("°"));
	_fov->setValue (preferences.default_fov);
	_axis->addItems ({ Translations::tr ("Horizontal"), Translations::tr ("Vertical") });
	_axis->setCurrentIndex (preferences.fov_axis == FovAxis::Horizontal ? 0 : 1);

	for (auto const mode: { NavigationMode::ClickAndDrag, NavigationMode::AutoPanning, NavigationMode::HoldToPan })
		_navigation->addItem (navigation_mode_name (mode), static_cast<int> (mode));

	_navigation->setCurrentIndex (_navigation->findData (static_cast<int> (preferences.navigation)));

	for (auto const mode: { ProjectionMode::Standard, ProjectionMode::SmallPlanet })
		_projection->addItem (projection_mode_name (mode), static_cast<int> (mode));

	_projection->setCurrentIndex (_projection->findData (static_cast<int> (preferences.projection)));
	connect (_projection, &QComboBox::currentIndexChanged, this, [this] {
		auto const small_planet = static_cast<ProjectionMode> (_projection->currentData().toInt()) == ProjectionMode::SmallPlanet;
		_fov->setMaximum (small_planet ? 350.0 : 150.0);
	});

	auto* const form = new QFormLayout;
	auto const add_explanation = [this, form] (QString const& text) {
		auto* const explanation = new QLabel (text, this);
		explanation->setWordWrap (true);
		form->addRow (QString(), explanation);
	};
	_language->setObjectName ("language");
	_language->addItem (Translations::tr ("System default"), static_cast<int> (Language::System));
	_language->addItem ("English", static_cast<int> (Language::English));
	_language->addItem ("Polski", static_cast<int> (Language::Polish));
	_language->addItem ("日本語", static_cast<int> (Language::Japanese));
	_language->addItem ("Українська", static_cast<int> (Language::Ukrainian));
	_language->addItem ("Français", static_cast<int> (Language::French));
	_language->setCurrentIndex (_language->findData (static_cast<int> (preferences.language)));
	form->addRow (Translations::tr ("Language:"), _language);
	add_explanation (Translations::tr ("System default uses a supported system language, or English. Changes take effect when you accept this dialog."));
	form->addRow (Translations::tr ("Default FOV:"), _fov);
	add_explanation (Translations::tr ("The mouse wheel changes the FOV. Changing view settings resets the current FOV."));
	form->addRow (Translations::tr ("FOV axis:"), _axis);
	form->addRow (Translations::tr ("Navigation:"), _navigation);
	add_explanation (
		Translations::tr ("Auto panning: move the pointer away from the center to turn. "
		"Return to the center or move outside the image to stop.\n\n"
		"Hold to pan: press the primary mouse button to set the neutral point. "
		"Move away to turn; return to that point or release the button to stop.")
	);
	form->addRow (Translations::tr ("Projection:"), _projection);
	add_explanation (Translations::tr ("Small planet: zoom out to turn the panorama into a tiny planet."));
	auto* const buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	auto* const layout = new QVBoxLayout (this);
	layout->addLayout (form);
	layout->addWidget (buttons);
}


Preferences
PreferencesDialog::preferences() const
{
	auto result = _initial_preferences;
	result.default_fov = _fov->value();
	result.fov_axis = _axis->currentIndex() == 0 ? FovAxis::Horizontal : FovAxis::Vertical;
	result.navigation = static_cast<NavigationMode> (_navigation->currentData().toInt());
	result.projection = static_cast<ProjectionMode> (_projection->currentData().toInt());
	result.language = static_cast<Language> (_language->currentData().toInt());
	return result;
}

} // namespace mawari

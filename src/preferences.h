// vim:ts=4

#ifndef MAWARI__PREFERENCES_H__INCLUDED
#define MAWARI__PREFERENCES_H__INCLUDED

// Local:
#include "translations.h"

// Qt:
#include <QDialog>
#include <QKeySequence>


class QComboBox;
class QDoubleSpinBox;


namespace mawari {

enum class FovAxis
{
	Horizontal,
	Vertical,
};


enum class NavigationMode
{
	ClickAndDrag,
	AutoPanning,
	HoldToPan,
};


enum class ProjectionMode
{
	Standard,
	SmallPlanet,
};


QString
navigation_mode_name (NavigationMode);


QKeySequence
navigation_mode_shortcut (NavigationMode);


QString
projection_mode_name (ProjectionMode);


QKeySequence
projection_mode_shortcut (ProjectionMode);


struct Preferences
{
	double			default_fov	= 90.0;
	FovAxis			fov_axis	= FovAxis::Horizontal;
	NavigationMode	navigation	= NavigationMode::ClickAndDrag;
	ProjectionMode	projection	= ProjectionMode::Standard;

	Language			language	= Language::System;
	double			hold_pan_sensitivity = 4.0;

	static constexpr double minimum_hold_pan_sensitivity = 0.25;
	static constexpr double maximum_hold_pan_sensitivity = 8.0;

	static Preferences
	load();

	void
	save() const;
};


class PreferencesDialog final: public QDialog
{
  public:
	explicit
	PreferencesDialog (Preferences const&, QWidget* parent = nullptr);

	[[nodiscard]]
	Preferences
	preferences() const;

  private:
	Preferences		_initial_preferences;
	QDoubleSpinBox*	_fov;
	QComboBox*		_axis;
	QComboBox*		_navigation;
	QComboBox*		_projection;
	QComboBox*		_language;
};

} // namespace mawari

#endif

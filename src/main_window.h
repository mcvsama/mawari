// vim:ts=4

#ifndef MAWARI__MAIN_WINDOW_H__INCLUDED
#define MAWARI__MAIN_WINDOW_H__INCLUDED

// Local:
#include "preferences.h"
#include "image_exporter.h"

// Qt:
#include <QMainWindow>
#include <QFutureWatcher>
#include <QTimer>


class QAction;
class QLabel;
class QMenu;
class QProgressDialog;
class QSlider;


namespace mawari {

class PanoramaView;


class MainWindow final: public QMainWindow
{
  public:
	explicit
	MainWindow (QWidget* parent = nullptr);

	~MainWindow() override;

	void
	open_file (QString const& path);

  protected:
	void
	closeEvent (QCloseEvent*) override;

	void
	changeEvent (QEvent*) override;

	void
	keyPressEvent (QKeyEvent*) override;

  private:
	void
	toggle_full_screen();

	void
	update_full_screen_ui();

	void
	choose_file();

	void
	save_image();

	void
	update_save_action();

	void
	update_export_progress();

	void
	finish_export();

	void
	show_preferences();

	void
	show_about();

	void
	update_view_status();

	void
	update_sensitivity_ui();

	void
	retranslate_ui();

	void
	update_image_status();

  private:
	Preferences		_preferences;
	PanoramaView*	_view;
	QLabel*			_view_status;
	QWidget*		_sensitivity_controls;
	QLabel*			_sensitivity_label;
	QSlider*		_sensitivity_slider;
	QLabel*			_sensitivity_value;
	QAction*		_full_screen_action;
	QAction*		_save_action;
	QString			_image_directory;
	QMenu*			_file_menu;
	QMenu*			_view_menu;
	QMenu*			_navigation_menu;
	QMenu*			_projection_menu;
	QMenu*			_help_menu;
	QAction*		_open_action;
	QAction*		_preferences_action;
	QAction*		_quit_action;
	QAction*		_reset_action;
	QAction*		_about_action;
	QSize			_displayed_size;
	bool			_loading = false;

	QFutureWatcher<ImageExportResult>	_exporter;
	std::shared_ptr<ImageExportProgress> _export_progress;
	QProgressDialog*	_export_dialog = nullptr;
	QTimer				_export_timer;
	QString				_export_path;
	QSize				_export_size;
};

} // namespace mawari

#endif

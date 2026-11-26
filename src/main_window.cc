// vim:ts=4

// Local:
#include "main_window.h"
#include "panorama_view.h"

// Qt:
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QSpinBox>
#include <QSlider>
#include <QtConcurrentRun>

// Standard:
#include <algorithm>


namespace mawari {

MainWindow::MainWindow (QWidget* const parent):
	QMainWindow (parent),
	_preferences (Preferences::load()),
	_view (new PanoramaView (this)),
	_view_status (new QLabel (this)),
	_sensitivity_controls (new QWidget (this)),
	_sensitivity_label (new QLabel (_sensitivity_controls)),
	_sensitivity_slider (new QSlider (Qt::Horizontal, _sensitivity_controls)),
	_sensitivity_value (new QLabel (_sensitivity_controls)),
	_full_screen_action (new QAction (Translations::tr ("Full screen"), this)),
	_save_action (new QAction (Translations::tr ("&Save image…"), this))
{
	setWindowTitle (Translations::tr ("Mawari — 360° photo viewer"));
	resize (1100, 720);
	setCentralWidget (_view);
	_view->apply_preferences (_preferences);

	QSettings settings;
	restoreGeometry (settings.value ("window/geometry").toByteArray());
	_image_directory = settings.value ("files/directory", QStandardPaths::writableLocation (QStandardPaths::PicturesLocation)).toString();

	// Reserve Alt+F for full screen instead of the File menu mnemonic.
	_file_menu = menuBar()->addMenu (Translations::tr ("File"));
	_open_action = _file_menu->addAction (Translations::tr ("&Open…"), QKeySequence::Open, this, &MainWindow::choose_file);
	_save_action->setShortcut (QKeySequence::Save);
	_save_action->setEnabled (false);
	addAction (_save_action);
	_file_menu->addAction (_save_action);
	connect (_save_action, &QAction::triggered, this, &MainWindow::save_image);
	_preferences_action = _file_menu->addAction (Translations::tr ("&Preferences…"), QKeySequence::Preferences, this, &MainWindow::show_preferences);
	_file_menu->addSeparator();
	_quit_action = _file_menu->addAction (Translations::tr ("&Quit"), QKeySequence::Quit, qApp, &QApplication::quit);
	_view_menu = menuBar()->addMenu (Translations::tr ("&View"));
	_full_screen_action->setCheckable (true);
	_full_screen_action->setShortcut (QKeySequence (Qt::AltModifier | Qt::Key_F));
	_full_screen_action->setAutoRepeat (false);
	// Associate the action with the window so its shortcut remains active
	// while the menu bar is hidden in full screen.
	addAction (_full_screen_action);
	_view_menu->addAction (_full_screen_action);
	connect (_full_screen_action, &QAction::triggered, this, &MainWindow::toggle_full_screen);
	_reset_action = _view_menu->addAction (Translations::tr ("Reset to default FOV"), _view, &PanoramaView::reset_fov);
	_navigation_menu = _view_menu->addMenu (Translations::tr ("Navigation"));
	auto* const navigation = new QActionGroup (_navigation_menu);

	for (auto const mode: { NavigationMode::ClickAndDrag, NavigationMode::AutoPanning, NavigationMode::HoldToPan })
	{
		auto* const action = _navigation_menu->addAction (navigation_mode_name (mode));
		action->setData (static_cast<int> (mode));
		action->setCheckable (true);
		action->setChecked (_preferences.navigation == mode);
		action->setShortcut (navigation_mode_shortcut (mode));
		action->setAutoRepeat (false);
		addAction (action);
		navigation->addAction (action);
		connect (action, &QAction::triggered, _view, [this, mode] { _view->set_navigation_mode (mode); });
		connect (_view, &PanoramaView::navigation_mode_changed, action, [action, mode] (NavigationMode const current) {
			action->setChecked (current == mode);
		});
	}

	_projection_menu = _view_menu->addMenu (Translations::tr ("Projection"));
	auto* const projections = new QActionGroup (_projection_menu);

	for (auto const mode: { ProjectionMode::Standard, ProjectionMode::SmallPlanet })
	{
		auto* const action = _projection_menu->addAction (projection_mode_name (mode));
		action->setData (static_cast<int> (mode));
		action->setCheckable (true);
		action->setChecked (_preferences.projection == mode);
		action->setShortcut (projection_mode_shortcut (mode));
		action->setAutoRepeat (false);
		addAction (action);
		projections->addAction (action);
		connect (action, &QAction::triggered, _view, [this, mode] { _view->set_projection_mode (mode); });
		connect (_view, &PanoramaView::projection_mode_changed, action, [action, mode] (ProjectionMode const current) {
			action->setChecked (current == mode);
		});
	}

	// Reserve Alt+H for Hold to pan instead of the Help menu mnemonic.
	_help_menu = menuBar()->addMenu (Translations::tr ("Help"));
	_about_action = _help_menu->addAction (Translations::tr ("&About Mawari"), this, &MainWindow::show_about);

	statusBar()->addPermanentWidget (_view_status);
	_sensitivity_controls->setObjectName ("sensitivity_controls");
	_sensitivity_controls->setSizePolicy (QSizePolicy::Fixed, QSizePolicy::Preferred);
	_sensitivity_label->setSizePolicy (QSizePolicy::Fixed, QSizePolicy::Preferred);
	_sensitivity_value->setSizePolicy (QSizePolicy::Fixed, QSizePolicy::Preferred);
	_sensitivity_slider->setObjectName ("hold_pan_sensitivity");
	_sensitivity_slider->setRange (static_cast<int> (100 * Preferences::minimum_hold_pan_sensitivity),
		static_cast<int> (100 * Preferences::maximum_hold_pan_sensitivity));
	_sensitivity_slider->setSingleStep (25);
	_sensitivity_slider->setPageStep (100);
	_sensitivity_slider->setFixedWidth (110);
	_sensitivity_slider->setValue (static_cast<int> (_preferences.hold_pan_sensitivity * 100));
	_sensitivity_label->setBuddy (_sensitivity_slider);
	auto* const sensitivity_layout = new QHBoxLayout (_sensitivity_controls);
	sensitivity_layout->setContentsMargins (6, 0, 6, 0);
	sensitivity_layout->addWidget (_sensitivity_label);
	sensitivity_layout->addWidget (_sensitivity_slider);
	sensitivity_layout->addWidget (_sensitivity_value);
	statusBar()->addPermanentWidget (_sensitivity_controls);
	connect (_sensitivity_slider, &QSlider::valueChanged, this, [this] (int const value) {
		_preferences.hold_pan_sensitivity = value / 100.0;
		_view->set_hold_pan_sensitivity (_preferences.hold_pan_sensitivity);
		_preferences.save();
		update_sensitivity_ui();
	});
	statusBar()->showMessage (Translations::tr ("Open or drop an equirectangular panorama (usually 2:1)."));
	connect (_view, &PanoramaView::open_requested, this, &MainWindow::choose_file);
	connect (_view, &PanoramaView::save_requested, this, &MainWindow::save_image);
	connect (_view, &PanoramaView::preferences_requested, this, &MainWindow::show_preferences);
	connect (_view, &PanoramaView::view_changed, this, &MainWindow::update_view_status);
	connect (_view, &PanoramaView::navigation_mode_changed, this, [this] (NavigationMode const mode) {
		_preferences.navigation = mode;
		_preferences.save();
		update_view_status();
		update_sensitivity_ui();
	});
	connect (_view, &PanoramaView::projection_mode_changed, this, [this] (ProjectionMode const mode) {
		_preferences.projection = mode;
		_preferences.default_fov = std::clamp (_preferences.default_fov, 10.0, _view->camera().maximum_fov());
		_preferences.save();
		update_view_status();
	});
	connect (_view, &PanoramaView::loading_changed, this, [this] (bool const loading) {
		_loading = loading;
		update_save_action();
		if (loading)
			statusBar()->showMessage (Translations::tr ("Loading panorama…"));
		else
			statusBar()->clearMessage();
	});
	connect (_view, &PanoramaView::load_failed, this, [this] (QString const& message) {
		QMessageBox::warning (this, Translations::tr ("Could not load panorama"), message);
	});
	connect (_view, &PanoramaView::file_loaded, this, [this] (QString const& path, QSize, QSize const displayed) {
		_image_directory = QFileInfo (path).absolutePath();
		_displayed_size = displayed;
		update_image_status();
		_view->setToolTip (path);
	});
	_export_timer.setInterval (50);
	connect (&_export_timer, &QTimer::timeout, this, &MainWindow::update_export_progress);
	connect (&_exporter, &QFutureWatcher<ImageExportResult>::finished, this, &MainWindow::finish_export);
	retranslate_ui();
	update_full_screen_ui();
}


MainWindow::~MainWindow()
{
	if (_export_progress)
	{
		_export_progress->cancelled = true;
		// Finish using image codecs before QApplication tears down its plugins.
		_exporter.waitForFinished();
	}
}


void
MainWindow::open_file (QString const& path)
{
	_view->load_file (path);
}


void
MainWindow::toggle_full_screen()
{
	setWindowState (windowState() ^ Qt::WindowFullScreen);
}


void
MainWindow::update_full_screen_ui()
{
	auto const full_screen = isFullScreen();
	menuBar()->setVisible (!full_screen);
	statusBar()->setVisible (!full_screen);
	_full_screen_action->setChecked (full_screen);
}


void
MainWindow::changeEvent (QEvent* const event)
{
	QMainWindow::changeEvent (event);

	if (event->type() == QEvent::LanguageChange)
		retranslate_ui();

	// Window managers can change the state independently of our shortcut.
	if (event->type() == QEvent::WindowStateChange)
		update_full_screen_ui();
}


void
MainWindow::keyPressEvent (QKeyEvent* const event)
{
	if (event->key() == Qt::Key_Escape && isFullScreen())
	{
		setWindowState (windowState() & ~Qt::WindowFullScreen);
		event->accept();
	}
	else
		QMainWindow::keyPressEvent (event);
}


void
MainWindow::choose_file()
{
	QStringList extensions;

	for (auto const& format: QImageReader::supportedImageFormats())
		extensions.append ("*." + QString::fromLatin1 (format));

	auto const path = QFileDialog::getOpenFileName (
		this,
		Translations::tr ("Open panorama"),
		_image_directory,
		Translations::tr ("Images (%1);;All files (*)").arg (extensions.join (' ')),
		nullptr, QFileDialog::DontUseNativeDialog
	);

	if (!path.isEmpty())
		open_file (path);
}


void
MainWindow::save_image()
{
	if (!_view->can_save_image() || _export_progress)
		return;

	ImageExportView const view { _view->camera(), _view->image_original_size(),
		static_cast<double> (_view->width()) / std::max (1, _view->height()) };
	auto const source_path = _view->image_path();
	auto const suggested = suggested_export_size (view);
	QDialog options (this);
	options.setWindowTitle (Translations::tr ("Save image"));
	QFormLayout layout (&options);
	QLabel explanation (Translations::tr ("Save the current framing and projection using the original panorama.\n"
		"Suggested size is based on source detail and field of view, capped at\n"
		"the panorama's pixel count. You can choose a larger or smaller width."), &options);
	explanation.setWordWrap (true);
	layout.addRow (&explanation);
	QLabel source_label (Translations::tr ("%1 × %2 pixels").arg (view.source_size.width()).arg (view.source_size.height()), &options);
	layout.addRow (Translations::tr ("Source panorama:"), &source_label);
	QLabel suggested_label (Translations::tr ("%1 × %2 pixels").arg (suggested.width()).arg (suggested.height()), &options);
	layout.addRow (Translations::tr ("Suggested size:"), &suggested_label);
	QSpinBox width (&options);
	width.setObjectName ("export_width");
	width.setRange (1, std::max (100000, suggested.width()));
	width.setValue (suggested.width());
	width.setSuffix (" px");
	layout.addRow (Translations::tr ("Width:"), &width);
	QLabel height (&options);
	layout.addRow (Translations::tr ("Height:"), &height);
	QLabel memory (&options);
	layout.addRow (Translations::tr ("Approximate memory:"), &memory);
	QDialogButtonBox buttons (QDialogButtonBox::Save | QDialogButtonBox::Cancel, &options);
	layout.addRow (&buttons);
	connect (&buttons, &QDialogButtonBox::accepted, &options, &QDialog::accept);
	connect (&buttons, &QDialogButtonBox::rejected, &options, &QDialog::reject);
	auto const update_size = [&] {
		auto const size = export_size_for_width (width.value(), view.aspect);
		height.setText (size.isValid() ? Translations::tr ("%1 px (aspect ratio preserved)").arg (size.height()) : Translations::tr ("Too large"));
		buttons.button (QDialogButtonBox::Save)->setEnabled (size.isValid());
		// Source + mipmaps + result, excluding the existing preview and codecs.
		auto const bytes = 4.0 * (static_cast<double> (view.source_size.width()) * view.source_size.height() * 4.0 / 3.0
			+ static_cast<double> (size.width()) * size.height());
		memory.setText (Translations::tr ("%1 GiB, plus preview and image encoding")
			.arg (bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2));
	};
	connect (&width, &QSpinBox::valueChanged, &options, update_size);
	update_size();

	if (options.exec() != QDialog::Accepted)
		return;

	QSettings settings;
	auto const directory = settings.value ("files/export_directory", _image_directory).toString();
	auto const default_path = QDir (directory).filePath (QFileInfo (source_path).completeBaseName() + "-view.png");
	QFileDialog file_dialog (this, Translations::tr ("Save image"), directory);
	file_dialog.setOption (QFileDialog::DontUseNativeDialog);
	file_dialog.setAcceptMode (QFileDialog::AcceptSave);
	auto const jpeg_filter = Translations::tr ("JPEG image (*.jpg *.jpeg)");
	file_dialog.setNameFilters ({ Translations::tr ("PNG image (*.png)"), jpeg_filter });
	file_dialog.setDefaultSuffix ("png");
	file_dialog.selectFile (default_path);
	connect (&file_dialog, &QFileDialog::filterSelected, &file_dialog, [&file_dialog, jpeg_filter] (QString const& filter) {
		file_dialog.setDefaultSuffix (filter == jpeg_filter ? "jpg" : "png");
	});

	if (file_dialog.exec() != QDialog::Accepted)
		return;

	auto const path = file_dialog.selectedFiles().front();
	auto const suffix = QFileInfo (path).suffix().toLower();

	if (suffix != "png" && suffix != "jpg" && suffix != "jpeg")
	{
		QMessageBox::warning (this, Translations::tr ("Could not save image"), Translations::tr ("Choose a filename ending in .png, .jpg or .jpeg."));
		return;
	}

	auto const source_canonical = QFileInfo (source_path).canonicalFilePath();

	if (!source_canonical.isEmpty() && QFileInfo (path).canonicalFilePath() == source_canonical)
	{
		QMessageBox::warning (this, Translations::tr ("Could not save image"), Translations::tr ("Choose a different filename to preserve the original panorama."));
		return;
	}

	ImageExportRequest const request { view, export_size_for_width (width.value(), view.aspect), source_path,
		path, suffix == "png" ? QByteArray ("png") : QByteArray ("jpeg") };
	_export_path = path;
	_export_size = request.size;
	_export_progress = std::make_shared<ImageExportProgress>();
	_export_dialog = new QProgressDialog (Translations::tr ("Reading source panorama…"), Translations::tr ("Cancel"), 0, 0, this);
	_export_dialog->setWindowTitle (Translations::tr ("Save image"));
	_export_dialog->setWindowModality (Qt::WindowModal);
	_export_dialog->setMinimumDuration (0);
	_export_dialog->setAutoClose (false);
	_export_dialog->setAutoReset (false);
	connect (_export_dialog, &QProgressDialog::canceled, this, [this, progress = _export_progress] {
		progress->cancelled = true;

		if (_export_progress == progress)
			statusBar()->showMessage (Translations::tr ("Cancelling image export…"));
	});
	_export_dialog->show();
	_export_timer.start();
	settings.setValue ("files/export_directory", QFileInfo (path).absolutePath());
	update_save_action();
	_exporter.setFuture (QtConcurrent::run (export_image, request, _export_progress));
}


void
MainWindow::update_save_action()
{
	_save_action->setEnabled (_view->can_save_image() && !_export_progress);
}


void
MainWindow::update_export_progress()
{
	if (!_export_progress || _export_progress->cancelled)
		return;

	switch (_export_progress->stage.load())
	{
		case ImageExportStage::Reading:
		{
			auto const& read = _export_progress->reading;
			auto const total = read->total_bytes.load();

			if (read->stage.load() == ImageLoadStage::Reading && total > 0)
			{
				_export_dialog->setLabelText (Translations::tr ("Reading and decoding source panorama…"));
				_export_dialog->setRange (0, 100);
				_export_dialog->setValue (static_cast<int> (100.0 * read->bytes_read.load() / total));
			}
			else
			{
				_export_dialog->setLabelText (Translations::tr ("Decoding source panorama…"));
				_export_dialog->setRange (0, 0);
			}
			break;
		}
		case ImageExportStage::Preparing:
			_export_dialog->setLabelText (Translations::tr ("Preparing source panorama…"));
			_export_dialog->setRange (0, 0);
			break;
		case ImageExportStage::Rendering:
			_export_dialog->setLabelText (Translations::tr ("Rendering image…"));
			// Use a fixed percentage scale across the different stages.
			_export_dialog->setRange (0, 100);
			_export_dialog->setValue (static_cast<int> (100.0 * _export_progress->completed_rows.load()
				/ _export_size.height()));
			break;
		case ImageExportStage::Writing:
			_export_dialog->setLabelText (Translations::tr ("Saving image…"));
			_export_dialog->setRange (0, 0);
			break;
	}
}


void
MainWindow::finish_export()
{
	auto const result = _exporter.result();
	_export_timer.stop();
	_export_dialog->hide();
	_export_dialog->deleteLater();
	_export_dialog = nullptr;
	_export_progress.reset();
	update_save_action();

	if (!result.error.isEmpty())
		QMessageBox::warning (this, Translations::tr ("Could not save image"), result.error);
	else if (result.cancelled)
		statusBar()->showMessage (Translations::tr ("Image export cancelled."), 5000);
	else
		statusBar()->showMessage (Translations::tr ("Saved image: ") + _export_path, 8000);
}


void
MainWindow::show_preferences()
{
	PreferencesDialog dialog (_preferences, this);

	if (dialog.exec() == QDialog::Accepted)
	{
		auto const next = dialog.preferences();
		auto const view_changed = next.default_fov != _preferences.default_fov || next.fov_axis != _preferences.fov_axis
			|| next.navigation != _preferences.navigation || next.projection != _preferences.projection;
		_preferences = next;
		_preferences.save();
		if (view_changed)
			_view->apply_preferences (_preferences);
		Translations::instance().set_language (_preferences.language);
	}
}


void
MainWindow::show_about()
{
	QMessageBox dialog (this);
	dialog.setWindowTitle (Translations::tr ("About Mawari"));
	dialog.setTextFormat (Qt::RichText);
	dialog.setText (
		Translations::tr ("<h3>Mawari</h3>"
		"<p>A 360° panorama viewer</p>"
		"<p><b>Author:</b> GPT-6.1-Sol high<br>"
		"<b>Project orchestration:</b> Michał Gawron (mcvsama)</p>")
	);
	dialog.setIconPixmap (windowIcon().pixmap (QSize (96, 96), devicePixelRatioF()));
	dialog.setStandardButtons (QMessageBox::Ok);
	dialog.exec();
}


void
MainWindow::update_view_status()
{
	auto const& camera = _view->camera();
	_view_status->setText (Translations::tr ("%1 · %2 · %3 FOV %4°")
		.arg (navigation_mode_name (_preferences.navigation))
		.arg (projection_mode_name (camera.projection))
		.arg (camera.axis == FovAxis::Horizontal ? Translations::tr ("Horizontal") : Translations::tr ("Vertical"))
		.arg (camera.fov, 0, 'f', 1));
}


void
MainWindow::update_sensitivity_ui()
{
	_sensitivity_label->setText (Translations::tr ("Sensitivity:"));
	_sensitivity_value->setText (QString::number (_sensitivity_slider->value()) + '%');
	_sensitivity_controls->setToolTip (Translations::tr (
		"Hold to pan sensitivity. Higher values turn faster with smaller pointer movements."));
	_sensitivity_slider->setAccessibleName (Translations::tr ("Hold to pan sensitivity"));
	_sensitivity_controls->setEnabled (_preferences.navigation == NavigationMode::HoldToPan);
}


void
MainWindow::retranslate_ui()
{
	_file_menu->setTitle (Translations::tr ("File"));
	_view_menu->setTitle (Translations::tr ("&View"));
	_navigation_menu->setTitle (Translations::tr ("Navigation"));
	_projection_menu->setTitle (Translations::tr ("Projection"));
	_help_menu->setTitle (Translations::tr ("Help"));
	_open_action->setText (Translations::tr ("&Open…"));
	_save_action->setText (Translations::tr ("&Save image…"));
	_preferences_action->setText (Translations::tr ("&Preferences…"));
	_quit_action->setText (Translations::tr ("&Quit"));
	_full_screen_action->setText (Translations::tr ("Full screen"));
	_reset_action->setText (Translations::tr ("Reset to default FOV"));
	_about_action->setText (Translations::tr ("&About Mawari"));

	for (auto* const action: _navigation_menu->actions())
		action->setText (navigation_mode_name (static_cast<NavigationMode> (action->data().toInt())));
	for (auto* const action: _projection_menu->actions())
		action->setText (projection_mode_name (static_cast<ProjectionMode> (action->data().toInt())));

	update_view_status();
	update_sensitivity_ui();
	update_image_status();
}


void
MainWindow::update_image_status()
{
	if (!_view->has_image())
	{
		setWindowTitle (Translations::tr ("Mawari — 360° photo viewer"));
		statusBar()->showMessage (Translations::tr ("Open or drop an equirectangular panorama (usually 2:1)."));
	}
	else
	{
		auto const file = QFileInfo (_view->image_path());
		auto const original = _view->image_original_size();
		setWindowTitle (Translations::tr ("%1 — Mawari").arg (file.fileName()));
		auto message = Translations::tr ("%1 · %2 × %3").arg (file.fileName()).arg (original.width()).arg (original.height());
		if (original != _displayed_size)
			message += Translations::tr (" · Displayed at %1 × %2 (GPU texture limit)").arg (_displayed_size.width()).arg (_displayed_size.height());
		if (original.width() != 2 * original.height())
			message += Translations::tr (" · Image is not 2:1; assumed to cover 360° × 180°");
		statusBar()->showMessage (message);
	}
	if (_loading)
		statusBar()->showMessage (Translations::tr ("Loading panorama…"));
}


void
MainWindow::closeEvent (QCloseEvent* const event)
{
	QSettings settings;
	settings.setValue ("window/geometry", saveGeometry());
	settings.setValue ("files/directory", _image_directory);
	QMainWindow::closeEvent (event);
}

} // namespace mawari

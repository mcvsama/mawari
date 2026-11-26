// vim:ts=4

// Local:
#include "camera.h"
#include "main_window.h"
#include "panorama_view.h"
#include "preferences.h"
#include "image_exporter.h"

// Qt:
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QFocusEvent>
#include <QFileInfo>
#include <QFile>
#include <QFileDialog>
#include <QImageReader>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QSettings>
#include <QSpinBox>
#include <QSlider>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSurfaceFormat>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QWheelEvent>
#include <QtConcurrentRun>

// Standard:
#include <cmath>
#include <numbers>
#include <limits>


using namespace mawari;


/** Encode the world ray as RGB so framebuffer samples verify the projection. */
static QColor
direction_color (double const x, double const y, double const z)
{
	return QColor::fromRgbF ((x + 1.0) / 2.0, (y + 1.0) / 2.0, (z + 1.0) / 2.0);
}


static QImage
make_panorama()
{
	QImage image (1024, 512, QImage::Format_RGB32);

	for (int y = 0; y < image.height(); ++y)
	{
		auto const latitude = (0.5 - (y + 0.5) / image.height()) * std::numbers::pi;

		for (int x = 0; x < image.width(); ++x)
		{
			auto const longitude = ((x + 0.5) / image.width() - 0.5) * 2.0 * std::numbers::pi;
			image.setPixelColor (x, y, direction_color (
				std::sin (longitude) * std::cos (latitude),
				std::sin (latitude),
				std::cos (longitude) * std::cos (latitude)
			));
		}
	}

	return image;
}


static bool
close_color (QColor const& actual, QColor const& expected)
{
	return std::abs (actual.red() - expected.red()) <= 4 &&
		std::abs (actual.green() - expected.green()) <= 4 &&
		std::abs (actual.blue() - expected.blue()) <= 4;
}


static void
drag (PanoramaView& view, QPoint const& start, QPoint const& end)
{
	QTest::mousePress (&view, Qt::LeftButton, Qt::NoModifier, start);
	QMouseEvent move (QEvent::MouseMove, end, view.mapToGlobal (end), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
	QApplication::sendEvent (&view, &move);
	QTest::mouseRelease (&view, Qt::LeftButton, Qt::NoModifier, end);
}


class ViewerTest: public QObject
{
	Q_OBJECT

  private slots:
	void
	initTestCase();

	void
	fov_axis_and_limits();

	void
	projection_and_zoom();

	void
	small_sky_navigation();

	void
	preferences_round_trip();

	void
	pan_sensitivity_slider();

	void
	large_panorama_loading();

	void
	image_read_progress();

	void
	loading_indicator();

	void
	export_resolution();

	void
	export_projection();

	void
	export_files_and_failures();

	void
	save_image_action();

	void
	rendering_and_input();

	void
	context_menu_and_panning();

	void
	hold_to_pan_navigation();

	void
	drop_and_failed_load();

	void
	full_screen_state_changes();

  private:
	QTemporaryDir _directory;
	QString _panorama_path;
};


void
ViewerTest::initTestCase()
{
	QVERIFY (_directory.isValid());
	QSettings::setDefaultFormat (QSettings::IniFormat);
	QSettings::setPath (QSettings::IniFormat, QSettings::UserScope, _directory.path());
	_panorama_path = _directory.filePath ("panorama.png");
	QVERIFY (make_panorama().save (_panorama_path));
}


void
ViewerTest::fov_axis_and_limits()
{
	Camera camera;
	auto extents = camera.extents (2.0);
	QVERIFY (std::abs (extents.horizontal - 1.0) < 1e-12);
	QVERIFY (std::abs (extents.vertical - 0.5) < 1e-12);
	camera.axis = FovAxis::Vertical;
	extents = camera.extents (2.0);
	QVERIFY (std::abs (extents.horizontal - 2.0) < 1e-12);
	QVERIFY (std::abs (extents.vertical - 1.0) < 1e-12);
	extents = camera.extents (0.5);
	QVERIFY (std::abs (extents.horizontal - 0.5) < 1e-12);
	QVERIFY (std::abs (extents.vertical - 1.0) < 1e-12);
	camera.turn (1081.0, 200.0);
	QCOMPARE (camera.yaw, 1.0);
	QCOMPARE (camera.pitch, 89.9);
	camera.turn (-1082.0, -400.0);
	QCOMPARE (camera.yaw, -1.0);
	QCOMPARE (camera.pitch, -89.9);
	camera.zoom (1.0);
	QCOMPARE (camera.fov, 87.3);
	camera.zoom (-1.0);
	QVERIFY (std::abs (camera.fov - 90.0) < 1e-10);
	camera.zoom (1000.0);
	QCOMPARE (camera.fov, 10.0);
	camera.zoom (-1000.0);
	QCOMPARE (camera.fov, 150.0);
}


void
ViewerTest::projection_and_zoom()
{
	Camera camera;
	auto const initial_extents = camera.extents (2.0);
	camera.set_projection (ProjectionMode::SmallPlanet);
	QCOMPARE (camera.fov, 90.0);
	QCOMPARE (camera.projection_blend(), 0.0);
	QCOMPARE (camera.view_pitch(), 0.0);
	QCOMPARE (camera.extents (2.0).horizontal, initial_extents.horizontal);

	camera.fov = 145.0;
	QCOMPARE (camera.projection_blend(), 0.5);
	QVERIFY (camera.view_pitch() < 0.0);
	QVERIFY (camera.view_pitch() > -90.0);
	camera.fov = 180.0;
	QCOMPARE (camera.projection_blend(), 1.0);
	auto extents = camera.stereographic_extents (2.0);
	// At 180 degrees the edge ray is on the stereographic horizon (r=1).
	QVERIFY (std::abs (extents.horizontal - 1.0) < 1e-12);
	QVERIFY (std::abs (extents.vertical - 0.5) < 1e-12);
	camera.axis = FovAxis::Vertical;
	extents = camera.stereographic_extents (2.0);
	QVERIFY (std::abs (extents.horizontal - 2.0) < 1e-12);
	QVERIFY (std::abs (extents.vertical - 1.0) < 1e-12);
	camera.fov = 300.0;
	QCOMPARE (camera.view_pitch(), -90.0);
	QVERIFY (std::isfinite (camera.extents (2.0).horizontal));
	QVERIFY (camera.stereographic_extents (2.0).horizontal > 1.0);

	// Planet size steps stay uniform even close to the maximum zoom-out.
	for (auto const fov: { 260.0, 300.0, 340.0, 350.0 })
	{
		camera.fov = fov;
		auto const previous_radius = camera.stereographic_extents (2.0).vertical;
		camera.zoom (1.0);
		auto const radius = camera.stereographic_extents (2.0).vertical;
		QVERIFY (std::abs (previous_radius / radius - 1.0) < 0.04);
		QVERIFY (camera.fov < fov);
		camera.zoom (-1.0);
		QVERIFY (std::abs (camera.fov - fov) < 1e-10);
	}

	camera.pitch = 25.0;
	QCOMPARE (camera.view_pitch(), -65.0);
	camera.zoom (-1000.0);
	QCOMPARE (camera.fov, 350.0);
	camera.zoom (1000.0);
	QCOMPARE (camera.fov, 10.0);
	camera.fov = 300.0;
	camera.set_projection (ProjectionMode::Standard);
	QCOMPARE (camera.fov, 150.0);
	QCOMPARE (camera.projection_blend(), 0.0);
	QCOMPARE (camera.view_pitch(), -65.0);

	PanoramaView view;
	Preferences preferences;
	preferences.default_fov = 300.0;
	preferences.projection = ProjectionMode::SmallPlanet;
	view.apply_preferences (preferences);
	QCOMPARE (view.camera().projection, ProjectionMode::SmallPlanet);
	QCOMPARE (view.camera().fov, 300.0);
	view.set_projection_mode (ProjectionMode::Standard);
	QCOMPARE (view.camera().fov, 150.0);
	view.reset_fov();
	QCOMPARE (view.camera().fov, 150.0);
}


void
ViewerTest::small_sky_navigation()
{
	Camera camera;
	camera.set_projection (ProjectionMode::SmallPlanet);
	camera.set_fov (300.0);
	QCOMPARE (camera.view_pitch(), -90.0);
	camera.turn (0.0, 90.0);
	QCOMPARE (camera.view_pitch(), 0.0);
	camera.turn (0.0, 45.0);
	QCOMPARE (camera.view_pitch(), 45.0);
	camera.turn (0.0, 45.0);
	QCOMPARE (camera.view_pitch(), 90.0);
	camera.turn (0.0, 20.0);
	QCOMPARE (camera.view_pitch(), 90.0);
	// No hidden excess pitch: reversing immediately moves away from the pole.
	camera.turn (0.0, -1.0);
	QCOMPARE (camera.view_pitch(), 89.0);
	camera.turn (0.0, -179.0);
	QCOMPARE (camera.view_pitch(), -90.0);
	camera.turn (0.0, -20.0);
	QCOMPARE (camera.view_pitch(), -90.0);
	camera.turn (0.0, 1.0);
	QCOMPARE (camera.view_pitch(), -89.0);

	// Zooming in from small sky must not turn the camera upside down.
	camera.turn (0.0, 179.0);
	QCOMPARE (camera.view_pitch(), 90.0);
	camera.zoom (1000.0);
	QCOMPARE (camera.fov, 10.0);
	QCOMPARE (camera.view_pitch(), 90.0);
	camera.set_fov (300.0);
	QVERIFY (camera.view_pitch() >= -90.0);
	QVERIFY (camera.view_pitch() <= 90.0);
	camera.turn (0.0, 180.0);
	camera.set_projection (ProjectionMode::Standard);
	QCOMPARE (camera.view_pitch(), 89.9);
	camera.set_projection (ProjectionMode::SmallPlanet);
	QVERIFY (std::abs (camera.view_pitch() - 89.9) < 1e-10);
}


void
ViewerTest::preferences_round_trip()
{
	Preferences preferences { 72.5, FovAxis::Vertical, NavigationMode::AutoPanning };
	preferences.save();
	auto const loaded = Preferences::load();
	QCOMPARE (loaded.default_fov, 72.5);
	QCOMPARE (loaded.fov_axis, FovAxis::Vertical);
	QCOMPARE (loaded.navigation, NavigationMode::AutoPanning);
	PreferencesDialog dialog (loaded);
	QCOMPARE (dialog.preferences().default_fov, 72.5);
	QCOMPARE (dialog.preferences().fov_axis, FovAxis::Vertical);
	QCOMPARE (dialog.preferences().navigation, NavigationMode::AutoPanning);

	preferences.navigation = NavigationMode::HoldToPan;
	preferences.save();
	QCOMPARE (Preferences::load().navigation, NavigationMode::HoldToPan);
	PreferencesDialog hold_dialog (Preferences::load());
	QCOMPARE (hold_dialog.preferences().navigation, NavigationMode::HoldToPan);
	bool navigation_found = false;

	for (auto* const combo: hold_dialog.findChildren<QComboBox*>())
		if (combo->findText ("Hold to pan") >= 0)
		{
			navigation_found = true;
			QCOMPARE (combo->currentText(), "Hold to pan");
			combo->setCurrentIndex (combo->findText ("Click and drag"));
			QCOMPARE (hold_dialog.preferences().navigation, NavigationMode::ClickAndDrag);
		}

	QVERIFY (navigation_found);

	preferences.projection = ProjectionMode::SmallPlanet;
	preferences.default_fov = 300.0;
	preferences.save();
	QCOMPARE (Preferences::load().projection, ProjectionMode::SmallPlanet);
	QCOMPARE (Preferences::load().default_fov, 300.0);
	PreferencesDialog planet_dialog (Preferences::load());
	QCOMPARE (planet_dialog.preferences().projection, ProjectionMode::SmallPlanet);
	QCOMPARE (planet_dialog.preferences().default_fov, 300.0);
	bool projection_found = false;

	for (auto* const combo: planet_dialog.findChildren<QComboBox*>())
		if (combo->findText ("Small planet") >= 0)
		{
			projection_found = true;
			combo->setCurrentIndex (combo->findText ("Standard"));
			QCOMPARE (planet_dialog.preferences().projection, ProjectionMode::Standard);
			QCOMPARE (planet_dialog.preferences().default_fov, 150.0);
		}

	QVERIFY (projection_found);

	QSettings settings;
	settings.setValue ("view/defaultFov", "nan");
	QCOMPARE (Preferences::load().default_fov, 90.0);
	settings.setValue ("view/defaultFov", -30.0);
	QCOMPARE (Preferences::load().default_fov, 90.0);
	settings.setValue ("view/defaultFov", "broken");
	QCOMPARE (Preferences::load().default_fov, 90.0);
	settings.setValue ("view/navigation", "broken");
	QCOMPARE (Preferences::load().navigation, NavigationMode::ClickAndDrag);
	settings.setValue ("view/projection", "broken");
	QCOMPARE (Preferences::load().projection, ProjectionMode::Standard);
	settings.clear();
}


void
ViewerTest::pan_sensitivity_slider()
{
	QSettings settings;
	settings.clear();
	auto const clear_settings = qScopeGuard ([&] { settings.clear(); });
	QCOMPARE (Preferences::load().hold_pan_sensitivity, 4.0);
	MainWindow window;
	auto* const slider = window.findChild<QSlider*> ("hold_pan_sensitivity");
	auto* const view = window.findChild<PanoramaView*>();
	QVERIFY (slider);
	QVERIFY (view);
	QCOMPARE (slider->minimum(), 25);
	QCOMPARE (slider->maximum(), 800);
	QCOMPARE (slider->value(), 400);
	QVERIFY (!slider->isEnabled());
	view->set_navigation_mode (NavigationMode::HoldToPan);
	QVERIFY (slider->isEnabled());
	QSignalSpy loaded (view, &PanoramaView::file_loaded);
	window.open_file (_panorama_path);
	QTRY_COMPARE (loaded.count(), 1);
	QWheelEvent wheel ({ 100, 100 }, { 100, 100 }, {}, { 0, 240 }, Qt::NoButton,
		Qt::NoModifier, Qt::NoScrollPhase, false);
	QApplication::sendEvent (view, &wheel);
	auto const camera = view->camera();
	slider->setValue (175);
	QCOMPARE (view->hold_pan_sensitivity(), 1.75);
	QCOMPARE (Preferences::load().hold_pan_sensitivity, 1.75);
	QCOMPARE (view->camera().fov, camera.fov);
	QCOMPARE (view->camera().yaw, camera.yaw);
	QCOMPARE (view->camera().pitch, camera.pitch);
	PreferencesDialog dialog (Preferences::load());
	QCOMPARE (dialog.preferences().hold_pan_sensitivity, 1.75);
	MainWindow restored;
	QCOMPARE (restored.findChild<QSlider*> ("hold_pan_sensitivity")->value(), 175);
	QCOMPARE (restored.findChild<PanoramaView*>()->hold_pan_sensitivity(), 1.75);
	view->set_navigation_mode (NavigationMode::AutoPanning);
	QVERIFY (!slider->isEnabled());
	view->set_navigation_mode (NavigationMode::HoldToPan);
	QVERIFY (slider->isEnabled());
	QCOMPARE (slider->value(), 175);
	for (auto const& invalid: { "nan", "broken", "-1", "1000" })
	{
		settings.setValue ("view/holdPanSensitivity", invalid);
		QCOMPARE (Preferences::load().hold_pan_sensitivity, 4.0);
	}
}


void
ViewerTest::large_panorama_loading()
{
	auto const path = _directory.filePath ("large-panorama.jpg");
	auto const original_size = QSize (12000, 6000);
	auto const color = QColor (45, 110, 185);

	// The JPEG is small on disk, but needs 288 MB at 32 bits per pixel.
	{
		QImage image (original_size, QImage::Format_RGB32);
		QVERIFY (!image.isNull());
		image.fill (color);
		QVERIFY (image.save (path, "JPEG"));
	}

	{
		auto const previous_limit = QImageReader::allocationLimit();
		auto const restore_limit = qScopeGuard ([previous_limit] { QImageReader::setAllocationLimit (previous_limit); });
		QImageReader::setAllocationLimit (256);
		QTest::ignoreMessage (QtWarningMsg, "QImageIOHandler: Rejecting image as it exceeds the current allocation limit of 256 megabytes");
		QVERIFY (QImageReader (path).read().isNull());
	}

	PanoramaView view;
	QCOMPARE (QImageReader::allocationLimit(), 0);

	{
		auto const decoded = QImageReader (path).read();
		QVERIFY (!decoded.isNull());
		QCOMPARE (decoded.size(), original_size);
		QVERIFY (decoded.sizeInBytes() > 256 * 1024 * 1024);
	}

	QSignalSpy loaded (&view, &PanoramaView::file_loaded);
	QSignalSpy failed (&view, &PanoramaView::load_failed);
	view.load_file (path);
	QTRY_COMPARE_WITH_TIMEOUT (loaded.count() + failed.count(), 1, 30000);
	QCOMPARE (failed.count(), 0);
	QCOMPARE (loaded.count(), 1);
	QCOMPARE (loaded.front()[1].toSize(), original_size);
	QCOMPARE (loaded.front()[2].toSize(), QSize (8192, 4096));
	QVERIFY (view.has_image());
}


void
ViewerTest::image_read_progress()
{
	// Both codecs seek and reread headers; percentages must count unique bytes.
	for (auto const* const format: { "PNG", "JPEG" })
	{
		auto const path = _directory.filePath (QString ("progress.%1").arg (format));
		auto const panorama = make_panorama();
		QVERIFY (panorama.save (path, format));
		auto const progress = std::make_shared<ImageLoadProgress>();
		auto const result = read_image (path, 512, progress);
		QVERIFY2 (result.error.isEmpty(), qPrintable (result.error));
		QCOMPARE (result.original_size, panorama.size());
		QCOMPARE (result.image.size(), QSize (512, 256));
		QCOMPARE (result.image.format(), QImage::Format_RGBA8888);
		QCOMPARE (progress->total_bytes.load(), QFileInfo (path).size());
		QCOMPARE (progress->bytes_read.load(), progress->total_bytes.load());
		QCOMPARE (progress->stage.load(), ImageLoadStage::Preparing);
		QVERIFY (close_color (result.image.pixelColor (256, 128), panorama.pixelColor (512, 256)));
	}

	auto const progress = std::make_shared<ImageLoadProgress>();
	auto const missing = read_image (_directory.filePath ("missing-progress.jpg"), 512, progress);
	QVERIFY (missing.image.isNull());
	QVERIFY (!missing.error.isEmpty());
	QCOMPARE (progress->bytes_read.load(), 0);
}


void
ViewerTest::loading_indicator()
{
	PanoramaView view;
	view.resize (640, 320);
	view.showFullScreen();
	QVERIFY (QTest::qWaitForWindowExposed (&view));
	auto* const overlay = view.findChild<QWidget*> ("load_overlay");
	auto* const bar = view.findChild<QProgressBar*> ("load_progress");
	QVERIFY (overlay);
	QVERIFY (bar);
	QVERIFY (overlay->isHidden());
	QSignalSpy loading (&view, &PanoramaView::loading_changed);
	QSignalSpy loaded (&view, &PanoramaView::file_loaded);
	QSignalSpy failed (&view, &PanoramaView::load_failed);
	view.load_file (_panorama_path);
	QVERIFY (overlay->isVisible());
	QCOMPARE (loading.count(), 1);
	QCOMPARE (loading.back().front().toBool(), true);
	QCOMPARE (bar->minimum(), 0);
	QCOMPARE (bar->maximum(), 0);
	QTRY_COMPARE (loaded.count(), 1);
	QCOMPARE (failed.count(), 0);
	QVERIFY (overlay->isHidden());
	QCOMPARE (loading.count(), 2);
	QCOMPARE (loading.back().front().toBool(), false);

	view.load_file (_directory.filePath ("missing-progress.jpg"));
	QVERIFY (overlay->isVisible());
	QTRY_COMPARE (failed.count(), 1);
	QVERIFY (overlay->isHidden());
	QVERIFY (view.has_image());
	QCOMPARE (loaded.count(), 1);
	QCOMPARE (loading.back().front().toBool(), false);

	view.load_file (_panorama_path);
	view.load_file (_directory.filePath ("superseded-progress.jpg"));
	view.load_file (_panorama_path);
	QTRY_COMPARE (loaded.count(), 2);
	QCOMPARE (failed.count(), 1);
	QVERIFY (overlay->isHidden());
	QCOMPARE (loading.back().front().toBool(), false);

	// Loading must also finish when there is no visible frame to present.
	view.load_file (_panorama_path);
	view.hide();
	QTRY_COMPARE (loaded.count(), 3);
	QVERIFY (overlay->isHidden());
	QCOMPARE (loading.back().front().toBool(), false);

	// The worker owns its progress state and can safely outlive the viewer.
	auto* const temporary = new PanoramaView;
	temporary->load_file (_panorama_path);
	delete temporary;
	QTest::qWait (60);
}


void
ViewerTest::export_resolution()
{
	ImageExportView view { {}, QSize (30000, 15000), 2.0 };
	auto size = suggested_export_size (view);
	// At 90°, a perspective image has 2*tan(45°) radians per image width
	// at its center. This differs from simply multiplying by FOV/360°.
	QVERIFY (std::abs (size.width() - 30000.0 / std::numbers::pi) < 2.0);
	QCOMPARE (size.height(), qRound (size.width() / 2.0));
	auto const horizontal_size = size;
	view.camera.axis = FovAxis::Vertical;
	size = suggested_export_size (view);
	QVERIFY (std::abs (size.width() - 2 * horizontal_size.width()) <= 2);
	view.camera.axis = FovAxis::Horizontal;
	view.camera.set_fov (10.0);
	QVERIFY (suggested_export_size (view).width() < horizontal_size.width());
	view.camera.set_projection (ProjectionMode::SmallPlanet);
	view.camera.set_fov (300.0);
	QCOMPARE (suggested_export_size (view), view.source_size);
	view.aspect = 1.0;
	size = suggested_export_size (view);
	QCOMPARE (size.width(), size.height());
	QVERIFY (static_cast<qint64> (size.width()) * size.height() <= 450000000);
	QVERIFY (size.width() > 16384); // Not constrained by common GPU texture limits.
	QCOMPARE (export_size_for_width (60000, 2.0), QSize (60000, 30000));
	QVERIFY (!export_size_for_width (0, 2.0).isValid());
	QVERIFY (!export_size_for_width (1000, 0.0).isValid());
	QVERIFY (!export_size_for_width (1000, std::numeric_limits<double>::infinity()).isValid());
	view.source_size = {};
	QVERIFY (!suggested_export_size (view).isValid());
}


void
ViewerTest::export_projection()
{
	auto const source = make_panorama();
	ImageExportView view { {}, source.size(), 2.0 };
	auto render = [&] {
		return render_export_image (source, view, QSize (321, 161), std::make_shared<ImageExportProgress>());
	};
	auto image = render();
	QVERIFY (!image.isNull());
	QVERIFY (close_color (image.pixelColor (160, 80), direction_color (0.0, 0.0, 1.0)));
	QVERIFY (image.pixelColor (260, 80).red() > image.pixelColor (60, 80).red());
	QVERIFY (image.pixelColor (160, 20).green() > image.pixelColor (160, 140).green());
	view.camera.yaw = 180.0;
	image = render();
	QVERIFY (close_color (image.pixelColor (160, 80), direction_color (0.0, 0.0, -1.0)));
	QVERIFY (std::abs (image.pixelColor (159, 80).red() - image.pixelColor (161, 80).red()) < 4);
	view.camera.yaw = 0.0;
	view.camera.turn (0.0, 45.0);
	image = render();
	QVERIFY (close_color (image.pixelColor (160, 80), direction_color (0.0, std::sqrt (0.5), std::sqrt (0.5))));
	view.camera.pitch = 0.0;
	view.camera.axis = FovAxis::Vertical;
	image = render();
	auto const x = 2.0 * (240.5 / 321.0) - 1.0;
	auto const ray_x = 2.0 * x;
	auto const length = std::hypot (ray_x, 1.0);
	QVERIFY (close_color (image.pixelColor (240, 80), direction_color (ray_x / length, 0.0, 1.0 / length)));
	view.camera.axis = FovAxis::Horizontal;
	view.camera.set_projection (ProjectionMode::SmallPlanet);

	for (double const fov: { 130.0, 180.0, 280.0, 340.0 })
		for (double const pitch: { -90.0, 0.0, 90.0 })
		{
			view.camera.set_fov (fov);
			view.camera.turn (0.0, pitch - view.camera.view_pitch());
			image = render();
			auto const radians = pitch * std::numbers::pi / 180.0;
			auto const actual = image.pixelColor (160, 80);
			auto const expected = direction_color (0.0, std::sin (radians), std::cos (radians));
			QVERIFY2 (close_color (actual, expected), qPrintable (QString ("FOV %1, pitch %2: %3, expected %4")
				.arg (fov).arg (pitch).arg (actual.name()).arg (expected.name())));
		}

	view.camera.set_fov (280.0);
	view.camera.turn (0.0, -90.0 - view.camera.view_pitch());
	image = render();

	for (QPoint const pixel: { QPoint (0, 80), QPoint (160, 0), QPoint (320, 160), QPoint (80, 40) })
	{
		auto const qx = (2.0 * (pixel.x() + 0.5) / 321.0 - 1.0) * std::tan (280.0 * std::numbers::pi / 720.0);
		auto const qy = (1.0 - 2.0 * (pixel.y() + 0.5) / 161.0) * std::tan (280.0 * std::numbers::pi / 720.0) / 2.0;
		auto const d = 1.0 + qx * qx + qy * qy;
		QVERIFY (close_color (image.pixelColor (pixel), direction_color (2.0 * qx / d,
			-(1.0 - qx * qx - qy * qy) / d, 2.0 * qy / d)));
	}
}


void
ViewerTest::export_files_and_failures()
{
	ImageExportRequest request { { {}, QSize (1024, 512), 2.0 }, QSize (800, 400), _panorama_path,
		_directory.filePath ("export.png"), "png" };
	auto progress = std::make_shared<ImageExportProgress>();
	auto result = export_image (request, progress);
	QVERIFY2 (result.error.isEmpty(), qPrintable (result.error));
	QVERIFY (!result.cancelled);
	QImage saved (request.destination_path);
	QCOMPARE (saved.size(), request.size);
	QVERIFY (close_color (saved.pixelColor (400, 200), direction_color (0.0, 0.0, 1.0)));
	QCOMPARE (progress->completed_rows.load(), request.size.height());
	QCOMPARE (progress->reading->bytes_read.load(), QFileInfo (_panorama_path).size());

	request.destination_path = _directory.filePath ("export.jpg");
	request.format = "jpeg";
	result = export_image (request, std::make_shared<ImageExportProgress>());
	QVERIFY2 (result.error.isEmpty(), qPrintable (result.error));
	saved.load (request.destination_path);
	QCOMPARE (saved.size(), request.size);
	QVERIFY (close_color (saved.pixelColor (400, 200), direction_color (0.0, 0.0, 1.0)));

	// Fine source stripes disappear in a downscaled preview, but survive an
	// export that rereads the original instead of using the preview texture.
	QImage stripes (1920, 960, QImage::Format_RGB32);

	for (int y = 0; y < stripes.height(); ++y)
		for (int x = 0; x < stripes.width(); ++x)
			stripes.setPixel (x, y, (x / 4) % 2 ? qRgb (255, 255, 255) : qRgb (0, 0, 0));

	request.source_path = _directory.filePath ("fine-stripes.png");
	QVERIFY (stripes.save (request.source_path));
	request.view.source_size = stripes.size();
	request.view.camera.set_fov (10.0);
	request.destination_path = _directory.filePath ("fine-stripes-export.png");
	request.format = "png";
	result = export_image (request, std::make_shared<ImageExportProgress>());
	QVERIFY2 (result.error.isEmpty(), qPrintable (result.error));
	saved.load (request.destination_path);
	int minimum = 255, maximum = 0;

	for (int x = 0; x < saved.width(); ++x)
	{
		minimum = std::min (minimum, saved.pixelColor (x, 200).red());
		maximum = std::max (maximum, saved.pixelColor (x, 200).red());
	}

	QVERIFY (minimum < 10);
	QVERIFY (maximum > 245);
	auto const preview = read_image (request.source_path, 64, std::make_shared<ImageLoadProgress>()).image;
	QVERIFY (std::abs (preview.pixelColor (32, 16).red() - 128) < 20);

	request.destination_path = _directory.filePath ("preserved-target.png");
	{
		QFile file (request.destination_path);
		QVERIFY (file.open (QIODevice::WriteOnly));
		QCOMPARE (file.write ("original destination"), 20);
	}

	request.size = QSize (6000, 3000);
	progress = std::make_shared<ImageExportProgress>();
	auto future = QtConcurrent::run (export_image, request, progress);
	QTRY_VERIFY_WITH_TIMEOUT (progress->stage.load() == ImageExportStage::Rendering || future.isFinished(), 10000);
	QVERIFY (!future.isFinished());
	progress->cancelled = true;
	QTRY_VERIFY_WITH_TIMEOUT (future.isFinished(), 10000);
	QVERIFY (future.result().cancelled);
	QFile preserved (request.destination_path);
	QVERIFY (preserved.open (QIODevice::ReadOnly));
	QCOMPARE (preserved.readAll(), QByteArray ("original destination"));
	preserved.close();
	request.source_path = _directory.filePath ("missing-source.jpg");
	result = export_image (request, std::make_shared<ImageExportProgress>());
	QVERIFY (!result.error.isEmpty());
	QVERIFY (preserved.open (QIODevice::ReadOnly));
	QCOMPARE (preserved.readAll(), QByteArray ("original destination"));
	preserved.close();
	request.source_path = _panorama_path;
	request.view.source_size = QSize (17, 9);
	result = export_image (request, std::make_shared<ImageExportProgress>());
	QVERIFY (result.error.contains ("changed"));
	request.destination_path = _directory.filePath ("nonexistent-directory/export.png");
	result = export_image (request, std::make_shared<ImageExportProgress>());
	QVERIFY (!result.error.isEmpty());
}


void
ViewerTest::save_image_action()
{
	MainWindow window;
	window.showFullScreen();
	window.activateWindow();
	window.centralWidget()->setFocus();
	QVERIFY (QTest::qWaitForWindowActive (&window));
	auto* const view = qobject_cast<PanoramaView*> (window.centralWidget());
	QVERIFY (view);
	QAction* save = nullptr;

	for (auto* const action: window.actions())
		if (action->shortcut() == QKeySequence::Save)
			save = action;

	QVERIFY (save);
	QVERIFY (!save->isEnabled());
	QSignalSpy loaded (view, &PanoramaView::file_loaded);
	window.open_file (_panorama_path);
	QTRY_COMPARE (loaded.count(), 1);
	QVERIFY (save->isEnabled());
	QCOMPARE (view->image_path(), _panorama_path);
	QCOMPARE (view->image_original_size(), QSize (1024, 512));
	auto const aspect = static_cast<double> (view->width()) / view->height();
	auto const output_width = view->width() == 1600 ? 1601 : 1600;
	auto const destination = _directory.filePath ("save-action.png");
	bool options_seen = false, file_dialog_seen = false;
	QTimer::singleShot (5000, &window, [] {
		if (auto* const dialog = qobject_cast<QDialog*> (QApplication::activeModalWidget()))
			dialog->reject();
	});
	QTimer::singleShot (0, &window, [&] {
		auto* const dialog = qobject_cast<QDialog*> (QApplication::activeModalWidget());
		QVERIFY (dialog);
		auto* const width = dialog->findChild<QSpinBox*> ("export_width");

		if (!width)
		{
			dialog->reject();
			QFAIL ("Export options should contain a width control.");
		}

		options_seen = true;
		width->setValue (output_width);
		QTimer::singleShot (0, &window, [&] {
			auto* const file_dialog = qobject_cast<QFileDialog*> (QApplication::activeModalWidget());
			QVERIFY (file_dialog);
			file_dialog_seen = true;
			file_dialog->selectFile (destination);
			static_cast<QDialog*> (file_dialog)->accept();
		});
		dialog->accept();
	});
	QTest::keyClick (view, Qt::Key_S, Qt::ControlModifier);
	QVERIFY (options_seen);
	QVERIFY (file_dialog_seen);
	QTRY_VERIFY_WITH_TIMEOUT (QFileInfo::exists (destination), 10000);
	QTRY_VERIFY (save->isEnabled());
	QImage image (destination);
	QCOMPARE (image.size(), export_size_for_width (output_width, aspect));
	QVERIFY (image.size() != view->size());
}


void
ViewerTest::rendering_and_input()
{
	PanoramaView view;
	view.resize (640, 320);
	view.show();
	QVERIFY (QTest::qWaitForWindowExposed (&view));
	QVERIFY (view.isValid());
	QSignalSpy loaded (&view, &PanoramaView::file_loaded);
	QSignalSpy failed (&view, &PanoramaView::load_failed);
	view.load_file (_panorama_path);
	QTRY_COMPARE (loaded.count(), 1);
	QCOMPARE (failed.count(), 0);
	auto frame = view.grabFramebuffer();
	QVERIFY (!frame.isNull());
	QVERIFY (close_color (frame.pixelColor (320, 160), direction_color (0.0, 0.0, 1.0)));

	// Off-center samples verify perspective, orientation, and aspect ratio.
	auto const x = 2.0 * 480.5 / 640.0 - 1.0;
	auto const y = (1.0 - 2.0 * 80.5 / 320.0) * 0.5;
	auto const length = std::sqrt (x * x + y * y + 1.0);
	QVERIFY (close_color (frame.pixelColor (480, 80), direction_color (x / length, y / length, 1.0 / length)));
	QVERIFY (frame.pixelColor (320, 30).green() > frame.pixelColor (320, 290).green());
	QVERIFY (frame.pixelColor (600, 160).red() > frame.pixelColor (40, 160).red());

	// Dragging the image to the right looks left.
	drag (view, { 160, 160 }, { 480, 160 });
	QVERIFY (view.camera().yaw < -50.0);
	frame = view.grabFramebuffer();
	QVERIFY (frame.pixelColor (320, 160).red() < 40);

	// Exercise longitude wrap; the back of the sphere must remain continuous.
	Preferences preferences;
	preferences.default_fov = 120.0;
	view.apply_preferences (preferences);
	view.load_file (_panorama_path);
	QTRY_COMPARE (loaded.count(), 2);
	drag (view, { 0, 160 }, { 640, 160 });
	drag (view, { 160, 160 }, { 480, 160 });
	frame = view.grabFramebuffer();
	auto const yaw = view.camera().yaw * std::numbers::pi / 180.0;
	QVERIFY (close_color (frame.pixelColor (320, 160), direction_color (std::sin (yaw), 0.0, std::cos (yaw))));

	QWheelEvent wheel (QPointF (320, 160), view.mapToGlobal (QPoint (320, 160)), {}, { 0, 120 },
		Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
	QApplication::sendEvent (&view, &wheel);
	QCOMPARE (view.camera().fov, 116.4);
	view.reset_fov();
	QCOMPARE (view.camera().fov, 120.0);
	preferences.fov_axis = FovAxis::Vertical;
	preferences.default_fov = 90.0;
	view.apply_preferences (preferences);
	view.load_file (_panorama_path);
	QTRY_COMPARE (loaded.count(), 3);
	frame = view.grabFramebuffer();
	auto const vertical_color = frame.pixelColor (320, 80);
	QVERIFY (vertical_color.green() > 180);
	view.resize (320, 640);
	frame = view.grabFramebuffer();
	QVERIFY (close_color (frame.pixelColor (160, 160), vertical_color));

	view.resize (640, 320);
	preferences.projection = ProjectionMode::SmallPlanet;
	preferences.fov_axis = FovAxis::Horizontal;
	preferences.default_fov = 300.0;
	view.apply_preferences (preferences);
	frame = view.grabFramebuffer();
	QVERIFY (close_color (frame.pixelColor (320, 160), direction_color (0.0, -1.0, 0.0)));
	QVERIFY (frame.pixelColor (100, 160).green() > 200);
	// Equal radii must have the same latitude: the planet stays round.
	QVERIFY (std::abs (frame.pixelColor (380, 160).green() - frame.pixelColor (320, 100).green()) < 4);
	// Looking up produces small sky, with the ground outside the circle.
	drag (view, { 320, 10 }, { 320, 310 });
	QCOMPARE (view.camera().view_pitch(), 90.0);
	frame = view.grabFramebuffer();
	QVERIFY (close_color (frame.pixelColor (320, 160), direction_color (0.0, 1.0, 0.0)));
	QVERIFY (frame.pixelColor (100, 160).green() < 55);
	QVERIFY (std::abs (frame.pixelColor (380, 160).green() - frame.pixelColor (320, 100).green()) < 4);
	drag (view, { 320, 310 }, { 320, 10 });
	QCOMPARE (view.camera().view_pitch(), -90.0);
	preferences.default_fov = 90.0;
	view.apply_preferences (preferences);
	frame = view.grabFramebuffer();
	QVERIFY (close_color (frame.pixelColor (320, 160), direction_color (0.0, 0.0, 1.0)));

	// Leave a reproducible artifact for inspection when requested.
	if (!qEnvironmentVariableIsEmpty ("MAWARI_TEST_FRAME"))
		QVERIFY (frame.save (qEnvironmentVariable ("MAWARI_TEST_FRAME")));
}


void
ViewerTest::context_menu_and_panning()
{
	PanoramaView view;
	view.resize (640, 320);
	view.show();
	view.activateWindow();
	QVERIFY (QTest::qWaitForWindowActive (&view));
	QSignalSpy loaded (&view, &PanoramaView::file_loaded);
	view.load_file (_panorama_path);
	QTRY_COMPARE (loaded.count(), 1);
	bool inspected = false;
	QTimer::singleShot (50, &view, [&] {
		auto* const menu = qobject_cast<QMenu*> (QApplication::activePopupWidget());
		QVERIFY (menu);
		auto const close_menu = qScopeGuard ([menu] { menu->close(); });
		auto const actions = menu->actions();
		QCOMPARE (actions.size(), 8);
		QCOMPARE (actions[0]->text(), "Navigation");
		QCOMPARE (actions[1]->text(), "Projection");
		QCOMPARE (actions[2]->text(), "Reset to default FOV");
		QVERIFY (actions[3]->isSeparator());
		QCOMPARE (actions[4]->text(), "Open…");
		QCOMPARE (actions[5]->text(), "Save image…");
		QVERIFY (actions[5]->isEnabled());
		QCOMPARE (actions[6]->text(), "Preferences…");
		QCOMPARE (actions[7]->text(), "Quit");
		auto const projections = actions[1]->menu()->actions();
		QCOMPARE (projections.size(), 2);
		QVERIFY (projections[0]->isChecked());
		projections[1]->trigger();
		QCOMPARE (view.camera().projection, ProjectionMode::SmallPlanet);
		QVERIFY (projections[1]->isChecked());
		QVERIFY (!projections[0]->isChecked());
		projections[0]->trigger();
		QCOMPARE (view.camera().projection, ProjectionMode::Standard);
		auto const modes = actions[0]->menu()->actions();
		QCOMPARE (modes.size(), 3);
		QVERIFY (modes[0]->isChecked());
		QVERIFY (!modes[1]->isChecked());
		QCOMPARE (modes[2]->text(), QString ("Hold to pan\t") + QKeySequence ("Alt+H").toString (QKeySequence::NativeText));
		QVERIFY (!modes[2]->isChecked());
		modes[2]->trigger();
		QCOMPARE (view.navigation_mode(), NavigationMode::HoldToPan);
		QVERIFY (modes[2]->isChecked());
		QVERIFY (!modes[0]->isChecked());
		modes[1]->trigger();
		QVERIFY (!modes[0]->isChecked());
		QVERIFY (modes[1]->isChecked());
		QVERIFY (!modes[2]->isChecked());
		inspected = true;
	});
	QContextMenuEvent context_event (QContextMenuEvent::Mouse, { 320, 160 }, view.mapToGlobal (QPoint (320, 160)));
	QApplication::sendEvent (&view, &context_event);
	QVERIFY (inspected);
	QCOMPARE (view.navigation_mode(), NavigationMode::AutoPanning);
	// An off-center click must not change Auto panning's neutral point.
	QTest::mouseClick (&view, Qt::LeftButton, Qt::NoModifier, { 80, 160 });
	QTest::mouseMove (&view, { 320, 160 });
	QTest::qWait (60);
	QCOMPARE (view.camera().yaw, 0.0);
	QTest::mouseMove (&view, { 600, 160 });
	QTRY_VERIFY (view.camera().yaw > 1.0);
	QEvent leave (QEvent::Leave);
	QApplication::sendEvent (&view, &leave);
	auto const yaw = view.camera().yaw;
	QTest::qWait (80);
	QCOMPARE (view.camera().yaw, yaw);
	view.set_navigation_mode (NavigationMode::ClickAndDrag);
	QTest::mouseMove (&view, { 40, 160 });
	QTest::qWait (80);
	QCOMPARE (view.camera().yaw, yaw);
}


void
ViewerTest::hold_to_pan_navigation()
{
	PanoramaView view;
	view.resize (640, 320);
	view.set_navigation_mode (NavigationMode::HoldToPan);
	view.show();
	view.activateWindow();
	view.setFocus();
	QVERIFY (QTest::qWaitForWindowActive (&view));
	QSignalSpy loaded (&view, &PanoramaView::file_loaded);
	view.load_file (_panorama_path);
	QTRY_COMPARE (loaded.count(), 1);

	auto const move_pointer = [&view] (QPoint const& position, Qt::MouseButtons const buttons) {
		QMouseEvent move (QEvent::MouseMove, position, view.mapToGlobal (position), Qt::NoButton, buttons, Qt::NoModifier);
		QApplication::sendEvent (&view, &move);
	};
	move_pointer ({ 600, 160 }, Qt::NoButton);
	QTest::qWait (60);
	QCOMPARE (view.camera().yaw, 0.0);
	QTest::mousePress (&view, Qt::MiddleButton, Qt::NoModifier, { 600, 160 });
	move_pointer ({ 600, 160 }, Qt::MiddleButton);
	QTest::qWait (60);
	QCOMPARE (view.camera().yaw, 0.0);
	QTest::mouseRelease (&view, Qt::MiddleButton, Qt::NoModifier, { 600, 160 });

	// An off-center press is neutral; motion is relative to that press.
	QTest::mousePress (&view, Qt::LeftButton, Qt::NoModifier, { 160, 80 });
	QCOMPARE (view.cursor().shape(), Qt::BlankCursor);
	QTest::qWait (60);
	QCOMPARE (view.camera().yaw, 0.0);
	QCOMPARE (view.camera().pitch, 0.0);
	move_pointer ({ 600, 80 }, Qt::LeftButton);
	QTRY_VERIFY (view.camera().yaw > 1.0);
	QCOMPARE (view.camera().pitch, 0.0);
	move_pointer ({ 160, 80 }, Qt::LeftButton);
	auto const neutral_yaw = view.camera().yaw;
	QTest::qWait (60);
	QCOMPARE (view.camera().yaw, neutral_yaw);
	QTest::mouseRelease (&view, Qt::LeftButton, Qt::NoModifier, { 160, 80 });
	QCOMPARE (view.cursor().shape(), Qt::OpenHandCursor);
	auto yaw = view.camera().yaw;
	QTest::qWait (60);
	QCOMPARE (view.camera().yaw, yaw);
	move_pointer ({ 40, 160 }, Qt::NoButton);
	QTest::qWait (60);
	QCOMPARE (view.camera().yaw, yaw);

	// Holding in the dead zone stops; moving out resumes while still held.
	QTest::mousePress (&view, Qt::LeftButton, Qt::NoModifier, { 320, 160 });
	QCOMPARE (view.cursor().shape(), Qt::BlankCursor);
	QTest::qWait (60);
	QCOMPARE (view.camera().yaw, yaw);
	move_pointer ({ 600, 160 }, Qt::LeftButton);
	QTRY_VERIFY (view.camera().yaw > yaw + 1.0);
	move_pointer ({ 320, 160 }, Qt::LeftButton);
	yaw = view.camera().yaw;
	QTest::qWait (60);
	QCOMPARE (view.camera().yaw, yaw);
	move_pointer ({ 40, 160 }, Qt::LeftButton);
	QTRY_VERIFY (view.camera().yaw < yaw - 1.0);

	QFocusEvent focus_out (QEvent::FocusOut);
	QApplication::sendEvent (&view, &focus_out);
	QCOMPARE (view.cursor().shape(), Qt::OpenHandCursor);
	yaw = view.camera().yaw;
	move_pointer ({ 40, 160 }, Qt::LeftButton);
	QTest::qWait (60);
	QCOMPARE (view.camera().yaw, yaw);
	QTest::mouseRelease (&view, Qt::LeftButton, Qt::NoModifier, { 40, 160 });

	// Cancelling or changing modes must discard a previous held-button gesture.
	QTest::mousePress (&view, Qt::LeftButton, Qt::NoModifier, { 320, 160 });
	move_pointer ({ 600, 160 }, Qt::LeftButton);
	QTRY_VERIFY (view.camera().yaw > yaw + 1.0);
	QTest::keyClick (&view, Qt::Key_Escape);
	QCOMPARE (view.cursor().shape(), Qt::OpenHandCursor);
	yaw = view.camera().yaw;
	move_pointer ({ 600, 160 }, Qt::LeftButton);
	QTest::qWait (60);
	QCOMPARE (view.camera().yaw, yaw);
	view.set_navigation_mode (NavigationMode::ClickAndDrag);
	view.set_navigation_mode (NavigationMode::HoldToPan);
	move_pointer ({ 600, 160 }, Qt::LeftButton);
	QTest::qWait (60);
	QCOMPARE (view.camera().yaw, yaw);
	QTest::mouseRelease (&view, Qt::LeftButton, Qt::NoModifier, { 600, 160 });

	view.set_navigation_mode (NavigationMode::ClickAndDrag);
	QTest::mousePress (&view, Qt::LeftButton, Qt::NoModifier, { 320, 160 });
	QCOMPARE (view.cursor().shape(), Qt::BlankCursor);
	QTest::mouseRelease (&view, Qt::LeftButton, Qt::NoModifier, { 320, 160 });
	QCOMPARE (view.cursor().shape(), Qt::OpenHandCursor);
	QTest::mousePress (&view, Qt::LeftButton, Qt::NoModifier, { 320, 160 });
	QCOMPARE (view.cursor().shape(), Qt::BlankCursor);
	view.set_navigation_mode (NavigationMode::AutoPanning);
	QCOMPARE (view.cursor().shape(), Qt::CrossCursor);
	QTest::mouseRelease (&view, Qt::LeftButton, Qt::NoModifier, { 320, 160 });
	QCOMPARE (view.cursor().shape(), Qt::CrossCursor);
}


void
ViewerTest::drop_and_failed_load()
{
	PanoramaView view;
	view.resize (640, 320);
	view.show();
	QVERIFY (QTest::qWaitForWindowExposed (&view));
	QSignalSpy loaded (&view, &PanoramaView::file_loaded);
	QSignalSpy failed (&view, &PanoramaView::load_failed);
	QMimeData mime;
	mime.setUrls ({ QUrl::fromLocalFile (_panorama_path) });
	QDragEnterEvent enter ({ 10, 10 }, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
	QApplication::sendEvent (&view, &enter);
	QVERIFY (enter.isAccepted());
	QDropEvent drop (QPointF (10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
	QApplication::sendEvent (&view, &drop);
	QVERIFY (drop.isAccepted());
	QTRY_COMPARE (loaded.count(), 1);
	QVERIFY (view.has_image());
	auto const before = view.isValid() ? view.grabFramebuffer() : QImage();
	view.load_file (_directory.filePath ("missing.jpg"));
	QTRY_COMPARE (failed.count(), 1);
	QVERIFY (view.has_image());
	QCOMPARE (view.image_path(), _panorama_path);
	QCOMPARE (view.image_original_size(), QSize (1024, 512));
	if (view.isValid())
		QCOMPARE (view.grabFramebuffer(), before);

	// A newer request supersedes the active decode, including a failing one.
	view.load_file (_directory.filePath ("also-missing.jpg"));
	view.load_file (_panorama_path);
	QTRY_COMPARE (loaded.count(), 2);
	QCOMPARE (failed.count(), 1);

	QMimeData remote;
	remote.setUrls ({ QUrl ("https://example.com/panorama.jpg") });
	QDragEnterEvent remote_enter ({ 10, 10 }, Qt::CopyAction, &remote, Qt::LeftButton, Qt::NoModifier);
	QApplication::sendEvent (&view, &remote_enter);
	QVERIFY (!remote_enter.isAccepted());
}


void
ViewerTest::full_screen_state_changes()
{
	QSettings settings;
	settings.remove ("window/geometry");
	MainWindow window;
	window.show();
	window.activateWindow();
	window.centralWidget()->setFocus();
	QVERIFY (QTest::qWaitForWindowActive (&window));
	QVERIFY (!window.isFullScreen());
	QVERIFY (window.menuBar()->isVisible());
	QVERIFY (window.statusBar()->isVisible());
	QAction* full_screen_action = nullptr;

	for (auto* const action: window.actions())
		if (action->shortcut() == QKeySequence (Qt::AltModifier | Qt::Key_F))
			full_screen_action = action;

	QVERIFY (full_screen_action);
	QVERIFY (!full_screen_action->isChecked());
	QTest::keyClick (window.centralWidget(), Qt::Key_Escape);
	QVERIFY (!window.isFullScreen());
	QTest::keyClick (window.centralWidget(), Qt::Key_F, Qt::AltModifier);
	QTRY_VERIFY (window.isFullScreen());
	QVERIFY (window.menuBar()->isHidden());
	QVERIFY (window.statusBar()->isHidden());
	QVERIFY (full_screen_action->isChecked());

	// The window shortcut must still work with its menu hidden.
	QTest::keyClick (window.centralWidget(), Qt::Key_F, Qt::AltModifier);
	QTRY_VERIFY (!window.isFullScreen());
	QVERIFY (window.menuBar()->isVisible());
	QVERIFY (window.statusBar()->isVisible());
	QVERIFY (!full_screen_action->isChecked());

	// Escape also exits full screen entered through our shortcut.
	QTest::keyClick (window.centralWidget(), Qt::Key_F, Qt::AltModifier);
	QTRY_VERIFY (window.isFullScreen());
	QTest::keyClick (window.centralWidget(), Qt::Key_Escape);
	QTRY_VERIFY (!window.isFullScreen());
	QVERIFY (window.menuBar()->isVisible());
	QVERIFY (window.statusBar()->isVisible());
	QVERIFY (!full_screen_action->isChecked());

	// Change the state without triggering the action, as a window manager does.
	window.setWindowState (Qt::WindowMaximized | Qt::WindowFullScreen);
	QVERIFY (window.menuBar()->isHidden());
	QVERIFY (window.statusBar()->isHidden());
	QVERIFY (full_screen_action->isChecked());
	QTest::keyClick (window.centralWidget(), Qt::Key_F, Qt::AltModifier);
	QTRY_VERIFY (!window.isFullScreen());
	QVERIFY (window.isMaximized());
	QVERIFY (window.menuBar()->isVisible());
	QVERIFY (window.statusBar()->isVisible());
	QVERIFY (!full_screen_action->isChecked());
	window.setWindowState (Qt::WindowMaximized | Qt::WindowFullScreen);
	QVERIFY (window.menuBar()->isHidden());
	QTest::keyClick (window.centralWidget(), Qt::Key_Escape);
	QTRY_VERIFY (!window.isFullScreen());
	QVERIFY (window.isMaximized());
	QVERIFY (window.menuBar()->isVisible());
	QVERIFY (window.statusBar()->isVisible());
	QVERIFY (!full_screen_action->isChecked());

	// Restoring a saved full-screen window must initialize the same UI.
	window.setWindowState (Qt::WindowFullScreen);
	settings.setValue ("window/geometry", window.saveGeometry());
	MainWindow restored;
	QVERIFY (restored.isFullScreen());
	QVERIFY (restored.menuBar()->isHidden());
	QVERIFY (restored.statusBar()->isHidden());
	settings.remove ("window/geometry");
}


int
main (int argc, char* argv[])
{
	QSurfaceFormat format;
	format.setRenderableType (QSurfaceFormat::OpenGL);
	format.setVersion (3, 3);
	format.setProfile (QSurfaceFormat::CoreProfile);
	QSurfaceFormat::setDefaultFormat (format);
	QApplication application (argc, argv);
	QApplication::setOrganizationName ("MawariTests");
	QApplication::setApplicationName ("MawariTests");
	Translations::instance().set_language (Language::English);
	ViewerTest test;
	return QTest::qExec (&test, argc, argv);
}


#include "viewer.test.moc"

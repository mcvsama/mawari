// vim:ts=4

// Local:
#include "panorama_view.h"

// Qt:
#include <QActionGroup>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QImageReader>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QPainter>
#include <QUrl>
#include <QVector2D>
#include <QWheelEvent>
#include <QtConcurrentRun>

// Standard:
#include <algorithm>
#include <cmath>
#include <mutex>
#include <numbers>
#include <utility>


namespace mawari {

static constexpr auto kVertexShader = R"glsl(#version 330 core
out vec2 screen_position;

void main()
{
	// One oversized triangle covers the viewport without a diagonal seam.
	vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
	screen_position = positions[gl_VertexID];
	gl_Position = vec4(screen_position, 0.0, 1.0);
}
)glsl";


static constexpr auto kFragmentShader = R"glsl(#version 330 core
in vec2 screen_position;
out vec4 fragment_color;

uniform sampler2D panorama;
uniform vec2 half_extents;
uniform vec2 stereographic_extents;
uniform float projection_blend;
uniform vec2 yaw_pitch;

const float pi = 3.14159265358979323846;

void main()
{
	// Camera axes: +X right, +Y up, +Z forward. Positive pitch looks up.
	vec3 ray = normalize(vec3(screen_position * half_extents, 1.0));
	if (projection_blend > 0.0)
	{
		vec2 q = screen_position * stereographic_extents;
		float radius_squared = dot(q, q);
		vec3 stereographic_ray = vec3(2.0 * q, 1.0 - radius_squared) / (1.0 + radius_squared);
		ray = normalize(mix(ray, stereographic_ray, projection_blend));
	}

	float cp = cos(yaw_pitch.y), sp = sin(yaw_pitch.y);
	ray = vec3(ray.x, cp * ray.y + sp * ray.z, -sp * ray.y + cp * ray.z);
	float cy = cos(yaw_pitch.x), sy = sin(yaw_pitch.x);
	ray = vec3(cy * ray.x + sy * ray.z, ray.y, -sy * ray.x + cy * ray.z);

	// QImage's first row is uploaded at V=0, so north maps to V=0.
	vec2 uv = vec2(0.5 + atan(ray.x, ray.z) / (2.0 * pi),
	               0.5 - asin(clamp(ray.y, -1.0, 1.0)) / pi);

	// Correct derivatives at the longitude wrap before choosing mip levels.
	vec2 dx = dFdx(uv), dy = dFdy(uv);
	dx.x -= round(dx.x);
	dy.x -= round(dy.x);
	fragment_color = vec4(textureGrad(panorama, uv, dx, dy).rgb, 1.0);
}
)glsl";


static QString
local_drop_path (QMimeData const* const mime)
{
	if (!mime->hasUrls() || mime->urls().size() != 1)
		return {};

	auto const url = mime->urls().front();

	if (!url.isLocalFile() || !QFileInfo (url.toLocalFile()).isFile())
		return {};

	return url.toLocalFile();
}


PanoramaView::PanoramaView (QWidget* const parent):
	QOpenGLWidget (parent)
{
	// Large panoramas routinely exceed Qt's default decoded-image limit.
	// Configure it once, before starting any of our decoder threads.
	static std::once_flag image_io_configuration;
	std::call_once (image_io_configuration, [] { QImageReader::setAllocationLimit (0); });

	setAcceptDrops (true);
	setMouseTracking (true);
	setFocusPolicy (Qt::StrongFocus);
	setMinimumSize (320, 200);
	setAccessibleName (Translations::tr ("360 degree panorama"));
	_pan_timer.setInterval (16);
	_pan_timer.setTimerType (Qt::PreciseTimer);
	connect (&_pan_timer, &QTimer::timeout, this, &PanoramaView::update_panning);
	connect (&_loader, &QFutureWatcher<ImageLoadResult>::finished, this, &PanoramaView::finish_loading);
	_progress_timer.setInterval (50);
	connect (&_progress_timer, &QTimer::timeout, this, &PanoramaView::update_load_progress);
	connect (this, &QOpenGLWidget::frameSwapped, this, [this] {
		if (_defer_upload)
		{
			_defer_upload = false;
			update();
		}
		else if (_awaiting_first_frame)
			complete_loading();
	});

	auto* const overlay = new QFrame (this);
	_load_overlay = overlay;
	overlay->setObjectName ("load_overlay");
	overlay->setFrameStyle (QFrame::StyledPanel | QFrame::Raised);
	overlay->setAutoFillBackground (true);
	overlay->setMaximumWidth (440);
	overlay->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Fixed);
	auto* const contents = new QVBoxLayout (overlay);
	contents->setContentsMargins (20, 16, 20, 16);
	_load_label = new QLabel (overlay);
	_load_label->setObjectName ("load_label");
	_load_label->setWordWrap (true);
	_load_label->setAlignment (Qt::AlignCenter);
	_load_bar = new QProgressBar (overlay);
	_load_bar->setObjectName ("load_progress");
	contents->addWidget (_load_label);
	contents->addWidget (_load_bar);
	auto* const row = new QHBoxLayout;
	row->addStretch();
	row->addWidget (overlay, 1);
	row->addStretch();
	auto* const layout = new QVBoxLayout (this);
	layout->setContentsMargins (24, 24, 24, 24);
	layout->addStretch();
	layout->addLayout (row);
	layout->addStretch();
	overlay->hide();
	update_cursor();
}


PanoramaView::~PanoramaView()
{
	// The decoder captures values only and may finish after this widget is gone.
	disconnect (_context_cleanup);
	cleanup_gl();
}


void
PanoramaView::load_file (QString const& path)
{
	if (path.isEmpty())
		return;

	// Keep at most one decode active and remember only the newest request.
	if (_loading)
	{
		_pending_path = path;
		return;
	}

	_loading_path = path;
	_loading = true;
	_load_progress = std::make_shared<ImageLoadProgress>();
	stop_motion();
	show_load_stage (Translations::tr ("Opening panorama…"));
	_load_overlay->show();
	_progress_timer.start();
	emit loading_changed (true);
	_loader.setFuture (QtConcurrent::run (read_image, path, _max_texture_size, _load_progress));
	update();
}


void
PanoramaView::finish_loading()
{
	auto result = _loader.result();
	_progress_timer.stop();

	if (!_pending_path.isEmpty())
	{
		complete_loading();
		return;
	}

	if (!result.error.isEmpty())
	{
		complete_loading (result.error);
		return;
	}

	_pending_image = std::move (result.image);
	_loaded_original_size = result.original_size;
	_texture_dirty = true;

	if (_program && _gl_ready && isVisible())
	{
		show_load_stage (Translations::tr ("Uploading panorama to the graphics card…"));
		// Present this stage before entering the synchronous OpenGL upload.
		_defer_upload = true;
		update();
	}
	else
	{
		// Hidden widgets have no frame to wait for. Their texture is prepared
		// when shown; this also allows loading without an OpenGL context.
		commit_image();
		complete_loading();
	}
}


void
PanoramaView::commit_image()
{
	_image = std::exchange (_pending_image, {});
	_image_path = _loading_path;
	_image_original_size = _loaded_original_size;
	_pan_origin = { 0.5, 0.5 };
	_camera.yaw = 0.0;
	_camera.pitch = 0.0;
	reset_fov();
	update_cursor();
}


void
PanoramaView::complete_loading (QString const& error)
{
	bool const success = error.isEmpty() && _pending_image.isNull() && has_image();
	_loading = false;
	_awaiting_first_frame = false;
	_defer_upload = false;
	_pending_image = {};
	_progress_timer.stop();
	_load_progress.reset();

	if (!_pending_path.isEmpty())
	{
		auto const next_path = std::exchange (_pending_path, {});
		load_file (next_path);
		return;
	}

	_load_overlay->hide();
	emit loading_changed (false);

	if (!error.isEmpty())
		emit load_failed (error);
	else if (success)
		emit file_loaded (_loading_path, _loaded_original_size, _image.size());

	update_cursor();
	update();
}


void
PanoramaView::show_load_stage (QString const& text)
{
	_load_label->setText (text);
	_load_bar->setRange (0, 0);
	_load_bar->setTextVisible (false);
}


void
PanoramaView::update_load_progress()
{
	if (!_load_progress)
		return;

	switch (_load_progress->stage.load())
	{
		case ImageLoadStage::Reading:
		{
			auto const total = _load_progress->total_bytes.load();
			auto const read = _load_progress->bytes_read.load();

			if (total > 0)
			{
				_load_label->setText (Translations::tr ("Reading and decoding panorama…\n%1 / %2 MiB read")
					.arg (read / 1048576.0, 0, 'f', 1).arg (total / 1048576.0, 0, 'f', 1));
				_load_bar->setRange (0, 100);
				_load_bar->setValue (static_cast<int> (100.0 * read / total));
				_load_bar->setFormat (Translations::tr ("File read: %p%"));
				_load_bar->setTextVisible (true);
			}
			else
				show_load_stage (Translations::tr ("Opening panorama…"));
			break;
		}
		case ImageLoadStage::Decoding:
			show_load_stage (Translations::tr ("Decoding panorama…"));
			break;
		case ImageLoadStage::Preparing:
			show_load_stage (Translations::tr ("Preparing panorama…"));
			break;
	}
}


void
PanoramaView::apply_preferences (Preferences const& preferences)
{
	_preferences.default_fov = preferences.default_fov;
	_preferences.fov_axis = preferences.fov_axis;
	set_hold_pan_sensitivity (preferences.hold_pan_sensitivity);
	_camera.axis = preferences.fov_axis;
	set_projection_mode (preferences.projection);
	set_navigation_mode (preferences.navigation);
	reset_fov();
}


void
PanoramaView::set_navigation_mode (NavigationMode const mode)
{
	_preferences.navigation = mode;
	_pan_origin = { 0.5, 0.5 };
	stop_motion();
	update_cursor();
	emit navigation_mode_changed (mode);
}


void
PanoramaView::set_hold_pan_sensitivity (double const sensitivity)
{
	if (std::isfinite (sensitivity))
		_preferences.hold_pan_sensitivity = std::clamp (sensitivity,
			Preferences::minimum_hold_pan_sensitivity, Preferences::maximum_hold_pan_sensitivity);
}


void
PanoramaView::set_projection_mode (ProjectionMode const mode)
{
	_preferences.projection = mode;
	_camera.set_projection (mode);
	_preferences.default_fov = std::clamp (_preferences.default_fov, 10.0, _camera.maximum_fov());
	stop_motion();
	emit projection_mode_changed (mode);
	changed();
}


void
PanoramaView::reset_fov()
{
	_camera.set_fov (_preferences.default_fov);
	changed();
}


void
PanoramaView::initializeGL()
{
	_gl_ready = initializeOpenGLFunctions();

	if (!_gl_ready)
	{
		_gl_error = QT_TRANSLATE_NOOP ("mawari::Translations", "OpenGL 3.3 Core is required to display panoramas.");
		return;
	}

	_context_cleanup = connect (context(), &QOpenGLContext::aboutToBeDestroyed, this, &PanoramaView::cleanup_gl, Qt::DirectConnection);
	glGetIntegerv (GL_MAX_TEXTURE_SIZE, &_max_texture_size);
	_program = new QOpenGLShaderProgram;

	if (!_program->addShaderFromSourceCode (QOpenGLShader::Vertex, kVertexShader) ||
		!_program->addShaderFromSourceCode (QOpenGLShader::Fragment, kFragmentShader) ||
		!_program->link())
	{
		_gl_error = QT_TRANSLATE_NOOP ("mawari::Translations", "Could not initialize the panorama shader:\n");
		_gl_error_details = _program->log();
		delete _program;
		_program = nullptr;
		return;
	}

	_gl_error.clear();
	_gl_error_details.clear();
	_vao.create();
	_texture_dirty = !_image.isNull() || !_pending_image.isNull();
}


void
PanoramaView::cleanup_gl()
{
	if (!_gl_ready || !context())
		return;

	makeCurrent();

	if (_texture)
		glDeleteTextures (1, &_texture);

	_texture = 0;
	delete _program;
	_program = nullptr;
	_vao.destroy();
	doneCurrent();
	_gl_ready = false;
}


void
PanoramaView::paintGL()
{
	if (_gl_ready)
	{
		glViewport (0, 0, qRound (width() * devicePixelRatioF()), qRound (height() * devicePixelRatioF()));
		glDisable (GL_DEPTH_TEST);
		glDisable (GL_CULL_FACE);
		glDisable (GL_BLEND);
		glDisable (GL_SCISSOR_TEST);
		glClearColor (0.07f, 0.08f, 0.10f, 1.0f);
		glClear (GL_COLOR_BUFFER_BIT);
	}

	if (_program && _texture_dirty && !_defer_upload)
	{
		_texture_dirty = false;
		bool const new_image = !_pending_image.isNull();
		auto& image = new_image ? _pending_image : _image;

		if (image.width() > _max_texture_size || image.height() > _max_texture_size)
			image = image.scaled (_max_texture_size, _max_texture_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);

		GLuint texture = 0;
		glGenTextures (1, &texture);
		glActiveTexture (GL_TEXTURE0);
		glBindTexture (GL_TEXTURE_2D, texture);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		// Longitude stretches strongly near the poles. Anisotropic filtering
		// avoids selecting an excessively blurry mip level for latitude as well.
		if (context()->hasExtension (QByteArrayLiteral ("GL_EXT_texture_filter_anisotropic")))
		{
			GLfloat maximum = 1.0f;
			glGetFloatv (GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maximum);
			glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min (maximum, 16.0f));
		}

		glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, image.width(), image.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, image.constBits());
		glGenerateMipmap (GL_TEXTURE_2D);

		if (glGetError() == GL_NO_ERROR)
		{
			if (_texture)
				glDeleteTextures (1, &_texture);

			_texture = texture;
			_gl_error.clear();
			_gl_error_details.clear();

			if (new_image)
			{
				commit_image();
				show_load_stage (Translations::tr ("Displaying panorama…"));
				_awaiting_first_frame = true;
			}
		}
		else
		{
			glDeleteTextures (1, &texture);
			auto const error = Translations::tr ("Could not upload the panorama to the graphics card. Try a smaller image.");

			if (new_image)
			{
				_pending_image = {};
				// Report outside paintGL: a warning dialog can start an event loop.
				QTimer::singleShot (0, this, [this, error] { complete_loading (error); });
			}
			else
				_gl_error = QT_TRANSLATE_NOOP ("mawari::Translations", "Could not upload the panorama to the graphics card. Try a smaller image.");
		}
	}

	if (_program && _texture)
	{
		auto const extents = _camera.extents (aspect_ratio());
		auto const stereographic_extents = _camera.stereographic_extents (aspect_ratio());
		auto const radians = std::numbers::pi / 180.0;
		_program->bind();
		_program->setUniformValue ("panorama", 0);
		_program->setUniformValue ("half_extents", QVector2D (extents.horizontal, extents.vertical));
		_program->setUniformValue ("stereographic_extents", QVector2D (stereographic_extents.horizontal, stereographic_extents.vertical));
		_program->setUniformValue ("projection_blend", static_cast<float> (_camera.projection_blend()));
		_program->setUniformValue ("yaw_pitch", QVector2D (_camera.yaw * radians, _camera.view_pitch() * radians));
		glActiveTexture (GL_TEXTURE0);
		glBindTexture (GL_TEXTURE_2D, _texture);
		_vao.bind();
		glDrawArrays (GL_TRIANGLES, 0, 3);
		_vao.release();
		_program->release();
	}

	if (!_gl_error.isEmpty() || !_texture || _loading)
	{
		QPainter painter (this);
		painter.setPen (QColor (225, 228, 235));
		auto font = painter.font();
		font.setPointSize (12);
		painter.setFont (font);
		auto const text = !_gl_error.isEmpty() ? QCoreApplication::translate ("mawari::Translations", _gl_error.toUtf8().constData()) + _gl_error_details
			: Translations::tr ("Drop a 360° photo here\n\nRight-click for Open… and Preferences…\nScroll to zoom · Drag to look around");
		painter.fillRect (rect(), QColor (18, 20, 26, _texture ? 170 : 255));

		if (!_loading)
			painter.drawText (rect().adjusted (24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap, text);
	}
}


void
PanoramaView::changeEvent (QEvent* const event)
{
	QOpenGLWidget::changeEvent (event);
	if (event->type() == QEvent::LanguageChange)
	{
		setAccessibleName (Translations::tr ("360 degree panorama"));
		if (_loading)
		{
			if (_awaiting_first_frame)
				show_load_stage (Translations::tr ("Displaying panorama…"));
			else if (!_pending_image.isNull())
				show_load_stage (Translations::tr ("Uploading panorama to the graphics card…"));
			else
				update_load_progress();
		}
		update();
	}
}


void
PanoramaView::hideEvent (QHideEvent* const event)
{
	QOpenGLWidget::hideEvent (event);

	// A hidden or minimized window may never swap another frame. Keep its
	// decoded image ready for the next show, without leaving loading stuck.
	if (_loading && (!_pending_image.isNull() || _awaiting_first_frame))
	{
		if (!_pending_image.isNull())
			commit_image();

		complete_loading();
	}
}


void
PanoramaView::mousePressEvent (QMouseEvent* const event)
{
	_pointer = event->position();
	_pointer_inside = rect().contains (_pointer.toPoint());
	setFocus (Qt::MouseFocusReason);

	if (event->button() == Qt::LeftButton && has_image() && !_loading &&
		_preferences.navigation != NavigationMode::AutoPanning)
	{
		if (_preferences.navigation == NavigationMode::ClickAndDrag)
		{
			_dragging = true;
			_last_drag_position = event->position();
		}
		else
		{
			// Normalize the neutral point so it follows viewport resizing.
			_pan_origin = { _pointer.x() / width(), _pointer.y() / height() };
			_pan_button_down = true;
			start_panning();
		}

		update_cursor();
		event->accept();
	}
	else
		QOpenGLWidget::mousePressEvent (event);
}


void
PanoramaView::mouseReleaseEvent (QMouseEvent* const event)
{
	if (event->button() == Qt::LeftButton)
	{
		_dragging = false;
		_pan_button_down = false;

		if (_preferences.navigation == NavigationMode::HoldToPan)
			_pan_timer.stop();

		update_cursor();
	}

	QOpenGLWidget::mouseReleaseEvent (event);
}


void
PanoramaView::mouseMoveEvent (QMouseEvent* const event)
{
	_pointer = event->position();
	_pointer_inside = rect().contains (_pointer.toPoint());

	if (_dragging && (event->buttons() & Qt::LeftButton))
	{
		auto const delta = event->position() - _last_drag_position;
		_last_drag_position = event->position();
		auto const extents = _camera.extents (aspect_ratio());
		auto const degrees = 180.0 / std::numbers::pi;
		// Map cursor positions to camera rays for consistent sensitivity at any FOV.
		auto const old_x = 2.0 * (event->position().x() - delta.x()) / width() - 1.0;
		auto const new_x = 2.0 * event->position().x() / width() - 1.0;
		auto const old_y = 1.0 - 2.0 * (event->position().y() - delta.y()) / height();
		auto const new_y = 1.0 - 2.0 * event->position().y() / height();
		auto horizontal = (std::atan (old_x * extents.horizontal) - std::atan (new_x * extents.horizontal)) * degrees;
		auto vertical = (std::atan (old_y * extents.vertical) - std::atan (new_y * extents.vertical)) * degrees;
		auto const blend = _camera.projection_blend();

		if (blend > 0.0)
		{
			// Perspective ray angles cease to describe the view beyond 180°.
			// Blend towards angular sensitivity for stereographic navigation.
			auto const stereo = _camera.stereographic_extents (aspect_ratio());
			auto const horizontal_fov = 4.0 * std::atan (stereo.horizontal) * degrees;
			auto const vertical_fov = 4.0 * std::atan (stereo.vertical) * degrees;
			horizontal = std::lerp (horizontal, -delta.x() * horizontal_fov / width(), blend);
			vertical = std::lerp (vertical, delta.y() * vertical_fov / height(), blend);
		}

		_camera.turn (horizontal, vertical);
		changed();
	}

	if (_preferences.navigation == NavigationMode::AutoPanning ||
		(_preferences.navigation == NavigationMode::HoldToPan && _pan_button_down))
		start_panning();

	event->accept();
}


void
PanoramaView::wheelEvent (QWheelEvent* const event)
{
	if (!has_image() || _loading)
		return;

	auto const steps = !event->pixelDelta().isNull()
		? event->pixelDelta().y() / 60.0
		: event->angleDelta().y() / 120.0;
	_camera.zoom (steps);
	changed();
	event->accept();
}


void
PanoramaView::enterEvent (QEnterEvent* const event)
{
	_pointer_inside = true;
	_pointer = event->position();
	QOpenGLWidget::enterEvent (event);
}


void
PanoramaView::leaveEvent (QEvent* const event)
{
	_pointer_inside = false;
	_pan_timer.stop();
	QOpenGLWidget::leaveEvent (event);
}


void
PanoramaView::focusOutEvent (QFocusEvent* const event)
{
	stop_motion();
	QOpenGLWidget::focusOutEvent (event);
}


void
PanoramaView::dragEnterEvent (QDragEnterEvent* const event)
{
	stop_motion();

	if (!local_drop_path (event->mimeData()).isEmpty())
		event->acceptProposedAction();
}


void
PanoramaView::dropEvent (QDropEvent* const event)
{
	auto const path = local_drop_path (event->mimeData());

	if (!path.isEmpty())
	{
		load_file (path);
		event->acceptProposedAction();
	}
}


void
PanoramaView::contextMenuEvent (QContextMenuEvent* const event)
{
	stop_motion();
	_menu_open = true;
	QMenu menu (this);
	auto* const navigation = menu.addMenu (Translations::tr ("Navigation"));
	QActionGroup modes (&menu);
	// The main window owns the shortcuts; tabs add hints without registering duplicates.
	auto const add_mode = [&] (NavigationMode const mode) {
		auto* const action = navigation->addAction (navigation_mode_name (mode) + '\t'
			+ navigation_mode_shortcut (mode).toString (QKeySequence::NativeText));
		action->setCheckable (true);
		action->setChecked (_preferences.navigation == mode);
		modes.addAction (action);
		connect (action, &QAction::triggered, this, [this, mode] { set_navigation_mode (mode); });
	};
	add_mode (NavigationMode::ClickAndDrag);
	add_mode (NavigationMode::AutoPanning);
	add_mode (NavigationMode::HoldToPan);
	auto* const projection = menu.addMenu (Translations::tr ("Projection"));
	QActionGroup projections (&menu);

	for (auto const mode: { ProjectionMode::Standard, ProjectionMode::SmallPlanet })
	{
		auto* const action = projection->addAction (projection_mode_name (mode) + '\t'
			+ projection_mode_shortcut (mode).toString (QKeySequence::NativeText));
		action->setCheckable (true);
		action->setChecked (_preferences.projection == mode);
		projections.addAction (action);
		connect (action, &QAction::triggered, this, [this, mode] { set_projection_mode (mode); });
	}

	auto* const reset = menu.addAction (Translations::tr ("Reset to default FOV"), this, &PanoramaView::reset_fov);
	reset->setEnabled (has_image());
	menu.addSeparator();
	menu.addAction (Translations::tr ("Open…"), QKeySequence::Open, this, &PanoramaView::open_requested);
	auto* const save = menu.addAction (Translations::tr ("Save image…"), QKeySequence::Save, this, &PanoramaView::save_requested);
	save->setEnabled (can_save_image());
	menu.addAction (Translations::tr ("Preferences…"), this, &PanoramaView::preferences_requested);
	menu.addAction (Translations::tr ("Quit"), QKeySequence::Quit, qApp, &QApplication::quit);
	menu.exec (event->globalPos());
	_menu_open = false;
	// Require a fresh pointer movement after a menu or dialog closes.
	stop_motion();
}


void
PanoramaView::keyPressEvent (QKeyEvent* const event)
{
	if (event->key() == Qt::Key_Escape)
	{
		stop_motion();
		// Let the main window also handle leaving full screen.
		event->ignore();
	}
	else
		QOpenGLWidget::keyPressEvent (event);
}


void
PanoramaView::start_panning()
{
	if (!_pan_timer.isActive())
	{
		_pan_clock.start();
		_pan_timer.start();
	}
}


void
PanoramaView::update_panning()
{
	auto const dt = std::min (_pan_clock.restart() / 1000.0, 0.05);

	if (_preferences.navigation == NavigationMode::ClickAndDrag ||
		(_preferences.navigation == NavigationMode::HoldToPan && !_pan_button_down) || !_pointer_inside ||
		_menu_open || _loading || !has_image() || !isActiveWindow() ||
		QApplication::activeModalWidget() || QApplication::activePopupWidget())
	{
		stop_motion();
		return;
	}

	auto const hold_to_pan = _preferences.navigation == NavigationMode::HoldToPan;
	auto const response = [this, hold_to_pan] (double const value) {
		constexpr auto threshold = 0.012;
		// Short touchpad movements should produce useful speeds near the neutral point.
		auto const gain = hold_to_pan ? _preferences.hold_pan_sensitivity : 1.0;
		auto const magnitude = std::clamp (gain * (std::abs (value) - threshold) / (1.0 - threshold), 0.0, 1.0);
		return std::copysign (hold_to_pan ? magnitude : magnitude * magnitude, value);
	};
	auto const origin = hold_to_pan ? _pan_origin : QPointF (0.5, 0.5);
	auto const x = response (2.0 * (_pointer.x() / width() - origin.x()));
	auto const y = response (2.0 * (origin.y() - _pointer.y() / height()));

	if (x != 0.0 || y != 0.0)
	{
		_camera.turn (x * 75.0 * dt, y * 75.0 * dt);
		changed();
	}
	else
		_pan_timer.stop();
}


void
PanoramaView::update_cursor()
{
	if (!has_image())
		setCursor (Qt::ArrowCursor);
	else if (_dragging || _pan_button_down)
		setCursor (Qt::BlankCursor);
	else if (_preferences.navigation == NavigationMode::AutoPanning)
		setCursor (Qt::CrossCursor);
	else
		setCursor (Qt::OpenHandCursor);
}


void
PanoramaView::stop_motion()
{
	_dragging = false;
	_pan_button_down = false;
	_pan_timer.stop();
	update_cursor();
}


void
PanoramaView::changed()
{
	update();
	emit view_changed();
}


double
PanoramaView::aspect_ratio() const
{
	return static_cast<double> (width()) / std::max (1, height());
}

} // namespace mawari

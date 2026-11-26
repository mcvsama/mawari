// vim:ts=4

#ifndef MAWARI__PANORAMA_VIEW_H__INCLUDED
#define MAWARI__PANORAMA_VIEW_H__INCLUDED

// Local:
#include "camera.h"
#include "image_loader.h"

// Qt:
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QImage>
#include <QLabel>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QProgressBar>
#include <QTimer>


namespace mawari {

class PanoramaView final:
	public QOpenGLWidget,
	protected QOpenGLFunctions_3_3_Core
{
	Q_OBJECT

  public:
	explicit
	PanoramaView (QWidget* parent = nullptr);

	~PanoramaView() override;

	void
	load_file (QString const& path);

	void
	apply_preferences (Preferences const&);

	void
	set_navigation_mode (NavigationMode);

	void
	set_hold_pan_sensitivity (double);

	[[nodiscard]]
	double
	hold_pan_sensitivity() const
		{ return _preferences.hold_pan_sensitivity; }

	void
	set_projection_mode (ProjectionMode);

	void
	reset_fov();

	[[nodiscard]]
	Camera const&
	camera() const
		{ return _camera; }

	[[nodiscard]]
	NavigationMode
	navigation_mode() const
		{ return _preferences.navigation; }

	[[nodiscard]]
	bool
	has_image() const
		{ return !_image.isNull(); }

	[[nodiscard]]
	bool
	can_save_image() const
		{ return has_image() && !_loading; }

	[[nodiscard]]
	QString const&
	image_path() const
		{ return _image_path; }

	[[nodiscard]]
	QSize
	image_original_size() const
		{ return _image_original_size; }

  signals:
	void
	file_loaded (QString const& path, QSize original_size, QSize displayed_size);

	void
	load_failed (QString const& message);

	void
	loading_changed (bool loading);

	void
	view_changed();

	void
	navigation_mode_changed (NavigationMode);

	void
	projection_mode_changed (ProjectionMode);

	void
	open_requested();

	void
	save_requested();

	void
	preferences_requested();

  protected:
	void
	changeEvent (QEvent*) override;

	void
	initializeGL() override;

	void
	paintGL() override;

	void
	hideEvent (QHideEvent*) override;

	void
	mousePressEvent (QMouseEvent*) override;

	void
	mouseReleaseEvent (QMouseEvent*) override;

	void
	mouseMoveEvent (QMouseEvent*) override;

	void
	wheelEvent (QWheelEvent*) override;

	void
	enterEvent (QEnterEvent*) override;

	void
	leaveEvent (QEvent*) override;

	void
	focusOutEvent (QFocusEvent*) override;

	void
	dragEnterEvent (QDragEnterEvent*) override;

	void
	dropEvent (QDropEvent*) override;

	void
	contextMenuEvent (QContextMenuEvent*) override;

	void
	keyPressEvent (QKeyEvent*) override;

  private:
	void
	start_panning();

	void
	cleanup_gl();

	void
	finish_loading();

	void
	complete_loading (QString const& error = {});

	void
	commit_image();

	void
	update_load_progress();

	void
	show_load_stage (QString const& text);

	void
	update_panning();

	void
	update_cursor();

	void
	stop_motion();

	void
	changed();

	[[nodiscard]]
	double
	aspect_ratio() const;

  private:
	Preferences	_preferences;
	Camera		_camera;
	QImage		_image;
	QString		_image_path;
	QSize		_image_original_size;

	bool					_texture_dirty		= false;
	bool					_gl_ready			= false;
	GLuint					_texture			= 0;
	QOpenGLShaderProgram*	_program			= nullptr;
	QOpenGLVertexArrayObject	_vao;
	QString					_gl_error;
	QString					_gl_error_details;
	int						_max_texture_size	= 8192;
	QMetaObject::Connection	_context_cleanup;

	QFutureWatcher<ImageLoadResult>	_loader;
	QString							_loading_path;
	QString							_pending_path;
	bool							_loading = false;
	std::shared_ptr<ImageLoadProgress> _load_progress;
	QTimer							_progress_timer;
	QWidget*						_load_overlay = nullptr;
	QLabel*							_load_label = nullptr;
	QProgressBar*					_load_bar = nullptr;
	QImage							_pending_image;
	QSize							_loaded_original_size;
	bool							_defer_upload = false;
	bool							_awaiting_first_frame = false;

	QTimer			_pan_timer;
	QElapsedTimer	_pan_clock;
	QPointF			_pointer;
	QPointF			_pan_origin { 0.5, 0.5 };
	QPointF			_last_drag_position;
	bool			_dragging		= false;
	bool			_pan_button_down = false;
	bool			_pointer_inside	= false;
	bool			_menu_open		= false;
};

} // namespace mawari

#endif

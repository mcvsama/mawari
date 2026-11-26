// vim:ts=4

#ifndef MAWARI__IMAGE_EXPORTER_H__INCLUDED
#define MAWARI__IMAGE_EXPORTER_H__INCLUDED

// Local:
#include "camera.h"
#include "image_loader.h"


namespace mawari {

struct ImageExportView
{
	Camera	camera;
	QSize	source_size;
	double	aspect = 1.0;
};


struct ImageExportRequest
{
	ImageExportView	view;
	QSize			size;
	QString			source_path;
	QString			destination_path;
	QByteArray		format;
};


enum class ImageExportStage { Reading, Preparing, Rendering, Writing };


struct ImageExportProgress
{
	std::shared_ptr<ImageLoadProgress> reading = std::make_shared<ImageLoadProgress>();
	std::atomic<ImageExportStage>	stage { ImageExportStage::Reading };
	std::atomic<int>				completed_rows { 0 };
	std::atomic<bool>			cancelled { false };
};


struct ImageExportResult
{
	QString	error;
	bool	cancelled = false;
};


/** A detail estimate, bounded by the source pixel count, not the GPU limit. */
QSize
suggested_export_size (ImageExportView const&);


/** Keep the captured viewport's proportions when changing export width. */
QSize
export_size_for_width (int width, double aspect);


/** Reproject full-resolution source pixels; no widget or OpenGL context needed. */
QImage
render_export_image (QImage const& source, ImageExportView const&, QSize size,
	std::shared_ptr<ImageExportProgress> const&);


/** Read, reproject and atomically save an image. Safe to run after UI destruction. */
ImageExportResult
export_image (ImageExportRequest const&, std::shared_ptr<ImageExportProgress> const&);

} // namespace mawari

#endif

// vim:ts=4

#ifndef MAWARI__IMAGE_LOADER_H__INCLUDED
#define MAWARI__IMAGE_LOADER_H__INCLUDED

// Qt:
#include <QImage>
#include <QString>

// Standard:
#include <atomic>
#include <memory>


namespace mawari {

enum class ImageLoadStage { Reading, Decoding, Preparing };


/** Shared with the decoder; never contains pointers into the UI. */
struct ImageLoadProgress
{
	std::atomic<ImageLoadStage>	stage { ImageLoadStage::Reading };
	std::atomic<qint64>			bytes_read { 0 };
	std::atomic<qint64>			total_bytes { 0 };
};


struct ImageLoadResult
{
	QImage	image;
	QString	error;
	QSize	original_size;
};


ImageLoadResult
read_image (QString const& path, int texture_limit, std::shared_ptr<ImageLoadProgress> const& progress);

} // namespace mawari

#endif

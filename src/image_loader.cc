// vim:ts=4

// Local:
#include "translations.h"
#include "image_loader.h"

// Qt:
#include <QFile>
#include <QFileInfo>
#include <QImageIOHandler>
#include <QImageReader>

// Standard:
#include <algorithm>
#include <iterator>
#include <map>
#include <utility>


namespace mawari {

/** A seekable device prevents codecs from bypassing read accounting via mmap.
 *  Track distinct byte ranges: codecs can seek back and read headers again.
 *  The compressed file is streamed, without keeping a second copy in memory.
 */
class ImageReadDevice final: public QIODevice
{
  public:
	explicit
	ImageReadDevice (QString const& path, ImageLoadProgress& progress):
		_file (path),
		_progress (progress)
	{
		if (_file.open (QIODevice::ReadOnly))
		{
			_progress.total_bytes = _file.size();
			QIODevice::open (QIODevice::ReadOnly);
		}
		else
			setErrorString (_file.errorString());
	}

	qint64
	size() const override
		{ return _file.size(); }

	bool
	isSequential() const override
		{ return false; }

	bool
	seek (qint64 const position) override
		{ return _file.seek (position) && QIODevice::seek (position); }

  protected:
	qint64
	readData (char* const data, qint64 const maximum) override
	{
		auto start = _file.pos();
		auto const count = _file.read (data, maximum);

		if (count > 0)
		{
			auto end = start + count;
			auto range = _ranges.lower_bound (start);

			if (range != _ranges.begin() && std::prev (range)->second >= start)
				--range;

			while (range != _ranges.end() && range->first <= end)
			{
				start = std::min (start, range->first);
				end = std::max (end, range->second);
				_covered -= range->second - range->first;
				range = _ranges.erase (range);
			}

			_ranges.emplace (start, end);
			_covered += end - start;
			_progress.bytes_read = _covered;

			if (_covered == _progress.total_bytes.load())
				_progress.stage = ImageLoadStage::Decoding;
		}
		else if (count < 0)
			setErrorString (_file.errorString());

		return count;
	}

	qint64
	writeData (char const*, qint64) override
		{ return -1; }

  private:
	QFile					_file;
	ImageLoadProgress&		_progress;
	std::map<qint64, qint64>	_ranges;
	qint64					_covered = 0;
};


ImageLoadResult
read_image (QString const& path, int const texture_limit, std::shared_ptr<ImageLoadProgress> const& progress)
{
	ImageReadDevice file (path, *progress);
	ImageLoadResult result;

	if (!file.isOpen())
	{
		result.error = Translations::tr ("Could not open “%1”.\n%2").arg (QFileInfo (path).fileName(), file.errorString());
		return result;
	}

	QImageReader reader (&file);
	reader.setAutoTransform (true);
	result.original_size = reader.size();

	// JPEG supports scaled decoding; avoid allocating more than the GPU needs.
	if (result.original_size.isValid() &&
		(result.original_size.width() > texture_limit || result.original_size.height() > texture_limit))
	{
		reader.setScaledSize (result.original_size.scaled (texture_limit, texture_limit, Qt::KeepAspectRatio));
	}

	if (reader.transformation() & QImageIOHandler::TransformationRotate90)
		result.original_size.transpose();

	auto image = reader.read();

	if (image.isNull())
	{
		result.error = Translations::tr ("Could not open “%1”.\n%2").arg (QFileInfo (path).fileName(), reader.errorString());
		return result;
	}

	progress->stage = ImageLoadStage::Preparing;

	if (!result.original_size.isValid())
		result.original_size = image.size();

	if (image.width() > texture_limit || image.height() > texture_limit)
		image = image.scaled (texture_limit, texture_limit, Qt::KeepAspectRatio, Qt::SmoothTransformation);

	result.image = std::move (image).convertToFormat (QImage::Format_RGBA8888);

	if (result.image.isNull())
		result.error = Translations::tr ("Not enough memory to prepare this image.");

	return result;
}

} // namespace mawari

// vim:ts=4

// Local:
#include "translations.h"
#include "image_exporter.h"

// Qt:
#include <QImageWriter>
#include <QSaveFile>
#include <QThread>
#include <QThreadPool>
#include <QtConcurrentMap>

// Standard:
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <numbers>
#include <vector>


namespace mawari {

struct Ray
{
	double x, y, z;
};


struct TextureCoordinate
{
	double u, v;
};


/** The same ray mapping as the panorama shader, evaluated in double precision. */
class ExportProjection
{
  public:
	explicit
	ExportProjection (ImageExportView const& view):
		_extents (view.camera.extents (view.aspect)),
		_stereographic (view.camera.stereographic_extents (view.aspect)),
		_blend (view.camera.projection_blend()),
		_cos_pitch (std::cos (view.camera.view_pitch() * std::numbers::pi / 180.0)),
		_sin_pitch (std::sin (view.camera.view_pitch() * std::numbers::pi / 180.0)),
		_cos_yaw (std::cos (view.camera.yaw * std::numbers::pi / 180.0)),
		_sin_yaw (std::sin (view.camera.yaw * std::numbers::pi / 180.0))
	{ }

	Ray
	ray (double const x, double const y) const
	{
		Ray direction { x * _extents.horizontal, y * _extents.vertical, 1.0 };
		auto const length = std::sqrt (direction.x * direction.x + direction.y * direction.y + 1.0);
		direction = { direction.x / length, direction.y / length, 1.0 / length };

		if (_blend > 0.0)
		{
			auto const qx = x * _stereographic.horizontal;
			auto const qy = y * _stereographic.vertical;
			auto const radius_squared = qx * qx + qy * qy;
			auto const denominator = 1.0 + radius_squared;
			direction = {
				(1.0 - _blend) * direction.x + _blend * 2.0 * qx / denominator,
				(1.0 - _blend) * direction.y + _blend * 2.0 * qy / denominator,
				(1.0 - _blend) * direction.z + _blend * (1.0 - radius_squared) / denominator
			};
			auto const mixed_length = std::sqrt (direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
			direction = { direction.x / mixed_length, direction.y / mixed_length, direction.z / mixed_length };
		}

		direction = { direction.x, _cos_pitch * direction.y + _sin_pitch * direction.z,
			-_sin_pitch * direction.y + _cos_pitch * direction.z };
		return { _cos_yaw * direction.x + _sin_yaw * direction.z, direction.y,
			-_sin_yaw * direction.x + _cos_yaw * direction.z };
	}

	TextureCoordinate
	coordinate (double const x, double const y) const
	{
		auto const direction = ray (x, y);
		return { 0.5 + std::atan2 (direction.x, direction.z) / (2.0 * std::numbers::pi),
			0.5 - std::asin (std::clamp (direction.y, -1.0, 1.0)) / std::numbers::pi };
	}

  private:
	Camera::Extents _extents, _stereographic;
	double _blend, _cos_pitch, _sin_pitch, _cos_yaw, _sin_yaw;
};


QSize
export_size_for_width (int const width, double const aspect)
{
	if (width < 1 || !std::isfinite (aspect) || aspect <= 0.0)
		return {};

	auto const height = std::max (1.0, std::round (width / aspect));

	if (height > std::numeric_limits<int>::max())
		return {};

	return { width, static_cast<int> (height) };
}


QSize
suggested_export_size (ImageExportView const& view)
{
	if (!view.source_size.isValid() || !std::isfinite (view.aspect) || view.aspect <= 0.0)
		return {};

	ExportProjection const projection (view);
	// Sample local angular magnification across the frame. This also handles
	// the perspective/stereographic transition, rather than treating FOV as
	// a linear fraction of the equirectangular image's width.
	double maximum_step = 0.0;
	constexpr double delta = 0.0001;
	auto const distance = [] (Ray const a, Ray const b) {
		return std::hypot (a.x - b.x, a.y - b.y, a.z - b.z);
	};

	for (int y = 0; y <= 16; ++y)
		for (int x = 0; x <= 16; ++x)
		{
			auto const sx = x / 8.0 - 1.0;
			auto const sy = y / 8.0 - 1.0;
			auto const ray = projection.ray (sx, sy);
			maximum_step = std::max ({ maximum_step,
				2.0 * distance (ray, projection.ray (sx + delta, sy)) / delta,
				2.0 * view.aspect * distance (ray, projection.ray (sx, sy + delta)) / delta });
		}

	auto const density = std::min (view.source_size.width() / (2.0 * std::numbers::pi),
		view.source_size.height() / std::numbers::pi);
	auto const pixels = static_cast<double> (view.source_size.width()) * view.source_size.height();
	// At extreme zoom-out a tiny planet could otherwise imply an enormous
	// mostly-sky canvas. Bound the automatic choice by the source pixel count;
	// the export dialog still allows a larger custom size.
	auto const pixel_budget_width = std::sqrt (pixels * view.aspect);
	auto const width = static_cast<int> (std::clamp (std::ceil (maximum_step * density), 1.0,
		std::max (1.0, std::min (pixel_budget_width, static_cast<double> (std::numeric_limits<int>::max())))));
	auto size = export_size_for_width (width, view.aspect);

	if (size.isValid() && static_cast<double> (size.width()) * size.height() > pixels && width > 1)
		size = export_size_for_width (width - 1, view.aspect);

	return size;
}


struct Mipmap
{
	QImage image;
	uchar const* data;
	qsizetype stride;
	int width, height;
};


struct Color
{
	double r, g, b;
};


static Color
sample_bilinear (Mipmap const& level, TextureCoordinate const uv)
{
	auto const x = (uv.u - std::floor (uv.u)) * level.width - 0.5;
	auto const y = std::clamp (uv.v * level.height - 0.5, 0.0, level.height - 1.0);
	auto const ix = static_cast<int> (std::floor (x));
	auto const iy = static_cast<int> (y);
	auto const fx = x - ix;
	auto const fy = y - iy;
	auto const left = ix < 0 ? level.width - 1 : ix;
	auto const right = ix + 1 == level.width ? 0 : ix + 1;
	auto const* const a = level.data + iy * level.stride + 4 * left;
	auto const* const b = level.data + iy * level.stride + 4 * right;
	auto const* const c = level.data + std::min (iy + 1, level.height - 1) * level.stride + 4 * left;
	auto const* const d = level.data + std::min (iy + 1, level.height - 1) * level.stride + 4 * right;
	auto const component = [&] (int const i) {
		return (1.0 - fy) * ((1.0 - fx) * a[i] + fx * b[i]) + fy * ((1.0 - fx) * c[i] + fx * d[i]);
	};
	return { component (0), component (1), component (2) };
}


static Color
sample_trilinear (std::vector<Mipmap> const& levels, TextureCoordinate const uv, double const lod)
{
	auto const first = static_cast<std::size_t> (lod);
	auto const a = sample_bilinear (levels[first], uv);
	auto const fraction = lod - first;

	if (fraction < 0.001 || first + 1 == levels.size())
		return a;

	auto const b = sample_bilinear (levels[first + 1], uv);
	return { std::lerp (a.r, b.r, fraction), std::lerp (a.g, b.g, fraction), std::lerp (a.b, b.b, fraction) };
}


static QRgb
sample_pixel (std::vector<Mipmap> const& levels, TextureCoordinate const uv,
	TextureCoordinate dx, TextureCoordinate dy)
{
	// Match the shader's seam correction before choosing mip levels. Multiple
	// samples along the longer footprint preserve detail near the poles.
	dx.u -= std::round (dx.u);
	dy.u -= std::round (dy.u);
	auto const width = levels.front().width;
	auto const height = levels.front().height;
	auto const ax = dx.u * width, ay = dx.v * height;
	auto const bx = dy.u * width, by = dy.v * height;
	// Singular values of the texture-coordinate Jacobian. The two screen
	// derivatives can be almost parallel near a pole; treating their lengths
	// as independent axes would unnecessarily blur the entire polar region.
	auto const a = ax * ax + bx * bx;
	auto const b = ax * ay + bx * by;
	auto const c = ay * ay + by * by;
	auto const eigenvalue = 0.5 * (a + c + std::hypot (a - c, 2.0 * b));
	auto const major = std::sqrt (eigenvalue);
	auto const minor = major > 0.0 ? std::abs (ax * by - ay * bx) / major : 0.0;
	auto vx = b, vy = eigenvalue - a;
	auto const axis_length = std::hypot (vx, vy);

	if (axis_length > 0.0)
	{
		vx /= axis_length;
		vy /= axis_length;
	}
	else
		vx = 1.0;

	TextureCoordinate const axis { vx * major / width, vy * major / height };

	auto const taps = static_cast<int> (std::clamp (std::ceil (major / std::max (1.0, minor)), 1.0, 16.0));
	auto const lod = std::clamp (std::log2 (std::max ({ 1.0, minor, major / taps })), 0.0,
		static_cast<double> (levels.size() - 1));
	Color sum {};

	for (int i = 0; i < taps; ++i)
	{
		auto const offset = (i + 0.5) / taps - 0.5;
		auto const color = sample_trilinear (levels, { uv.u + offset * axis.u, uv.v + offset * axis.v }, lod);
		sum.r += color.r;
		sum.g += color.g;
		sum.b += color.b;
	}

	return qRgb (qRound (sum.r / taps), qRound (sum.g / taps), qRound (sum.b / taps));
}


static QRgb
sample_polar_pixel (std::vector<Mipmap> const& levels, ExportProjection const& projection,
	QSize const size, int const x, int const y)
{
	// At a pole, one pixel covers many longitudes. Integrate in camera space
	// instead of filtering a discontinuous longitude as an ordinary UV plane.
	constexpr int subdivisions = 4;
	auto const coordinate = [&] (double const px, double const py) {
		return projection.coordinate (2.0 * px / size.width() - 1.0, 1.0 - 2.0 * py / size.height());
	};
	Color sum {};

	for (int sy = 0; sy < subdivisions; ++sy)
		for (int sx = 0; sx < subdivisions; ++sx)
		{
			auto const px = x + (sx + 0.5) / subdivisions;
			auto const py = y + (sy + 0.5) / subdivisions;
			auto const uv = coordinate (px, py);
			auto const next_x = coordinate (px + 1.0 / subdivisions, py);
			auto const next_y = coordinate (px, py + 1.0 / subdivisions);
			auto const color = sample_pixel (levels, uv, { next_x.u - uv.u, next_x.v - uv.v },
				{ next_y.u - uv.u, next_y.v - uv.v });
			sum.r += qRed (color);
			sum.g += qGreen (color);
			sum.b += qBlue (color);
		}

	return qRgb (qRound (sum.r / (subdivisions * subdivisions)), qRound (sum.g / (subdivisions * subdivisions)),
		qRound (sum.b / (subdivisions * subdivisions)));
}


static bool
crosses_polar_region (TextureCoordinate const uv, TextureCoordinate const dx, TextureCoordinate const dy, int const height)
{
	auto const longitude_step = std::max (std::abs (dx.u - std::round (dx.u)), std::abs (dy.u - std::round (dy.u)));
	auto const latitude_step = std::max (std::abs (dx.v), std::abs (dy.v));
	return longitude_step > 0.05 && std::min (uv.v, 1.0 - uv.v) < 2.0 * latitude_step + 1.0 / height;
}


QImage
render_export_image (QImage const& source, ImageExportView const& view, QSize const size,
	std::shared_ptr<ImageExportProgress> const& progress)
{
	if (source.isNull() || !size.isValid() || size.isEmpty() || !std::isfinite (view.aspect) || view.aspect <= 0.0)
		return {};

	if (progress->cancelled)
		return {};

	progress->stage = ImageExportStage::Preparing;
	std::vector<Mipmap> levels;
	auto image = source.convertToFormat (QImage::Format_RGBA8888);

	while (!image.isNull())
	{
		if (progress->cancelled)
			return {};

		levels.push_back ({ image, image.constBits(), image.bytesPerLine(), image.width(), image.height() });

		if (image.size() == QSize (1, 1))
			break;

		image = image.scaled (std::max (1, image.width() / 2), std::max (1, image.height() / 2),
			Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

		if (image.isNull())
			return {};
	}

	if (levels.empty())
		return {};

	QImage output (size, QImage::Format_RGB32);

	if (output.isNull())
		return {};

	// Detach once, before the workers start. They write disjoint scanlines and
	// never call a non-const QImage method concurrently.
	auto* const pixels = output.bits();
	auto const stride = output.bytesPerLine();
	ExportProjection const projection (view);
	constexpr int rows_per_chunk = 16;
	std::vector<int> chunks;

	for (int y = 0; y < size.height(); y += rows_per_chunk)
		chunks.push_back (y);

	progress->completed_rows = 0;
	progress->stage = ImageExportStage::Rendering;
	QThreadPool pool;
	pool.setMaxThreadCount (std::clamp (QThread::idealThreadCount() - 1, 1, 8));
	QtConcurrent::blockingMap (&pool, chunks, [&] (int const start) {
		if (progress->cancelled)
			return;

		std::vector<TextureCoordinate> row (static_cast<std::size_t> (size.width()) + 1);
		std::vector<TextureCoordinate> next (row.size());
		auto const coordinates = [&] (int const y, std::vector<TextureCoordinate>& values) {
			for (int x = 0; x <= size.width(); ++x)
				values[x] = projection.coordinate (2.0 * (x + 0.5) / size.width() - 1.0,
					1.0 - 2.0 * (y + 0.5) / size.height());
		};
		coordinates (start, row);

		for (int y = start; y < std::min (start + rows_per_chunk, size.height()); ++y)
		{
			if (progress->cancelled)
				return;

			coordinates (y + 1, next);
			auto* const destination = reinterpret_cast<QRgb*> (pixels + y * stride);

			for (int x = 0; x < size.width(); ++x)
			{
				TextureCoordinate const dx { row[x + 1].u - row[x].u, row[x + 1].v - row[x].v };
				TextureCoordinate const dy { next[x].u - row[x].u, next[x].v - row[x].v };
				destination[x] = crosses_polar_region (row[x], dx, dy, source.height())
					? sample_polar_pixel (levels, projection, size, x, y)
					: sample_pixel (levels, row[x], dx, dy);
			}

			row.swap (next);
			++progress->completed_rows;
		}
	});

	return progress->cancelled ? QImage() : output;
}


/** QImageWriter can be interrupted between writes without exposing a partial file. */
class ExportOutputDevice final: public QIODevice
{
  public:
	ExportOutputDevice (QSaveFile& file, std::atomic<bool> const& cancelled):
		_file (file),
		_cancelled (cancelled)
		{ open (QIODevice::WriteOnly); }

	bool
	isSequential() const override
		{ return true; }

  protected:
	qint64
	readData (char*, qint64) override
		{ return -1; }

	qint64
	writeData (char const* const data, qint64 const count) override
	{
		if (_cancelled)
		{
			setErrorString (Translations::tr ("Image export cancelled."));
			return -1;
		}

		auto const written = _file.write (data, count);

		if (written < 0)
			setErrorString (_file.errorString());

		return written;
	}

  private:
	QSaveFile& _file;
	std::atomic<bool> const& _cancelled;
};


ImageExportResult
export_image (ImageExportRequest const& request, std::shared_ptr<ImageExportProgress> const& progress)
{
	try
	{
		if (progress->cancelled)
			return { {}, true };

		if (!request.size.isValid() || request.size.isEmpty())
			return { Translations::tr ("Invalid export dimensions.") };

		if (request.format != "png" && request.format != "jpeg")
			return { Translations::tr ("Choose PNG or JPEG as the export format.") };

		if (request.format == "jpeg" && (request.size.width() > 65500 || request.size.height() > 65500))
			return { Translations::tr ("JPEG dimensions cannot exceed 65500 pixels. Choose PNG or a smaller size.") };

		// Check the destination before doing the expensive work. QSaveFile
		// keeps an existing destination intact until the final commit.
		QSaveFile file (request.destination_path);

		if (!file.open (QIODevice::WriteOnly))
			return { Translations::tr ("Could not save the image.\n") + file.errorString() };

		auto const source = read_image (request.source_path, std::numeric_limits<int>::max(), progress->reading);

		if (progress->cancelled)
			return { {}, true };

		if (!source.error.isEmpty())
			return { source.error };

		if (source.original_size != request.view.source_size)
			return { Translations::tr ("The source panorama has changed. Reopen it before saving a view.") };

		auto const image = render_export_image (source.image, request.view, request.size, progress);

		if (progress->cancelled)
			return { {}, true };

		if (image.isNull())
			return { Translations::tr ("Not enough memory to render this image. Try a smaller export size.") };

		progress->stage = ImageExportStage::Writing;
		ExportOutputDevice device (file, progress->cancelled);
		QImageWriter writer (&device, request.format);

		if (request.format == "jpeg")
			writer.setQuality (95);

		auto const written = writer.write (image);

		if (progress->cancelled)
			return { {}, true };

		if (!written)
			return { Translations::tr ("Could not save the image.\n") + writer.errorString() };

		if (!file.commit())
			return { Translations::tr ("Could not save the image.\n") + file.errorString() };

		return {};
	}
	catch (std::bad_alloc const&)
	{
		return { Translations::tr ("Not enough memory to export this image. Try a smaller size.") };
	}
	catch (std::exception const& error)
	{
		return { Translations::tr ("Could not export the image.\n") + QString::fromUtf8 (error.what()) };
	}
}

} // namespace mawari

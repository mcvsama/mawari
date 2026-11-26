// vim:ts=4

// Local:
#include "application_icon.h"

// Qt:
#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

// Standard:
#include <iostream>


int
main (int argc, char* argv[])
{
	// The artwork contains paths only; render to QImage without a display server.
	QCoreApplication application (argc, argv);
	auto const arguments = application.arguments();
	if (arguments.size() != 3)
		return 1;

	QSvgRenderer renderer (arguments[1]);
	if (!renderer.isValid())
	{
		std::cerr << "Could not read the Mawari SVG icon.\n";
		return 1;
	}

	for (auto const size: mawari::application_icon_sizes)
	{
		auto const directory = QDir (arguments[2]).filePath (QString::number (size));
		if (!QDir().mkpath (directory))
			return 1;
		QImage image (size, size, QImage::Format_ARGB32_Premultiplied);
		image.fill (Qt::transparent);
		QPainter painter (&image);
		renderer.render (&painter);
		painter.end();
		if (!image.save (QDir (directory).filePath ("mawari.png"), "PNG"))
		{
			std::cerr << "Could not write the Mawari PNG icon at " << size << " pixels.\n";
			return 1;
		}
	}
}

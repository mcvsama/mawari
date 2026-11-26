// vim:ts=4

// Local:
#include "application_icon.h"
#include "translations.h"
#include "main_window.h"

// Qt:
#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QSurfaceFormat>
#include <QTimer>


int
main (int argc, char* argv[])
{
	QSurfaceFormat format;
	format.setRenderableType (QSurfaceFormat::OpenGL);
	format.setVersion (3, 3);
	format.setProfile (QSurfaceFormat::CoreProfile);
	format.setDepthBufferSize (0);
	format.setStencilBufferSize (0);
	format.setSwapInterval (1);
	QSurfaceFormat::setDefaultFormat (format);

	QApplication application (argc, argv);
	QApplication::setOrganizationName ("Mawari");
	QApplication::setApplicationName ("Mawari");
	QApplication::setApplicationVersion ("1.0.0");
	QApplication::setDesktopFileName ("mawari");
	QApplication::setWindowIcon (mawari::application_icon());
	mawari::Translations::instance().set_language (mawari::Preferences::load().language);
	QCommandLineParser parser;
	parser.setApplicationDescription (mawari::Translations::tr ("A lightweight OpenGL viewer for equirectangular 360° photographs."));
	parser.addHelpOption();
	parser.addVersionOption();
	parser.addPositionalArgument ("image", mawari::Translations::tr ("Panorama to open."), "[image]");
	parser.process (application);

	if (parser.positionalArguments().size() > 1)
		parser.showHelp (2);

	mawari::MainWindow window;
	window.show();

	if (!parser.positionalArguments().isEmpty())
	{
		auto const path = parser.positionalArguments().front();
		QTimer::singleShot (0, &window, [&window, path] { window.open_file (path); });
	}

	return application.exec();
}

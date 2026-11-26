// vim:ts=4

// Local:
#include "application_icon.h"
#include "main_window.h"

// Qt:
#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>


using namespace mawari;


class IconTest: public QObject
{
	Q_OBJECT

  private slots:
	void
	embedded_pixmaps_and_about()
	{
		QTemporaryDir empty_plugins;
		QVERIFY (empty_plugins.isValid());
		auto const original_paths = QCoreApplication::libraryPaths();
		auto const restore_paths = qScopeGuard ([&] { QCoreApplication::setLibraryPaths (original_paths); });
		QCoreApplication::setLibraryPaths ({ empty_plugins.path() });
		auto const icon = application_icon();

		for (auto const size: application_icon_sizes)
		{
			QVERIFY (icon.availableSizes().contains (QSize (size, size)));
			auto const image = icon.pixmap (QSize (size, size), 1.0).toImage();
			QCOMPARE (image.size(), QSize (size, size));
			QCOMPARE (image.pixelColor (0, 0).alpha(), 0);
			auto const center = image.pixelColor (size / 2, size / 2);
			QCOMPARE (center.alpha(), 255);
			QVERIFY (center.green() > center.blue());
			int visible = 0;
			for (int y = 0; y < size; ++y)
				for (int x = 0; x < size; ++x)
					if (image.pixelColor (x, y).alpha() > 128)
						++visible;
			// A non-null QPixmap can still be completely transparent.
			QVERIFY (visible > size * size / 2);
		}

		auto const hidpi = icon.pixmap (QSize (48, 48), 2.0);
		QCOMPARE (hidpi.size(), QSize (96, 96));
		QCOMPARE (hidpi.devicePixelRatio(), 2.0);
		QApplication::setWindowIcon (icon);
		MainWindow window;
		QCOMPARE (window.windowIcon().availableSizes(), icon.availableSizes());
		auto* const about = window.menuBar()->actions().last()->menu()->actions().front();
		bool inspected = false;
		QTimer::singleShot (0, &window, [&] {
			auto* const dialog = qobject_cast<QMessageBox*> (QApplication::activeModalWidget());
			QVERIFY (dialog);
			auto const close_dialog = qScopeGuard ([dialog] { dialog->accept(); });
			auto const image = dialog->iconPixmap().toImage();
			QVERIFY (!image.isNull());
			QVERIFY (image.pixelColor (image.width() / 2, image.height() / 2).alpha() > 128);
			inspected = true;
		});
		about->trigger();
		QVERIFY (inspected);
	}
};


int
main (int argc, char* argv[])
{
	QApplication application (argc, argv);
	QApplication::setOrganizationName ("MawariIconTests");
	QApplication::setApplicationName ("MawariIconTests");
	QTemporaryDir configuration;
	qputenv ("XDG_CONFIG_HOME", configuration.path().toUtf8());
	Translations::instance().set_language (Language::English);
	IconTest test;
	return QTest::qExec (&test, argc, argv);
}


#include "icon.test.moc"

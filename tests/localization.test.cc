// vim:ts=4

// Local:
#include "main_window.h"
#include "panorama_view.h"
#include "translations.h"

// Qt:
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>
#include <QXmlStreamReader>


using namespace mawari;


static QStringList
placeholders (QString const& text)
{
	QStringList result;
	auto matches = QRegularExpression ("%(?:[0-9]+|p%)").globalMatch (text);
	while (matches.hasNext())
		result.append (matches.next().captured());
	result.sort();
	return result;
}


class LocalizationTest: public QObject
{
	Q_OBJECT

  private slots:
	void
	initTestCase()
	{
		QVERIFY (_directory.isValid());
		QSettings::setDefaultFormat (QSettings::IniFormat);
		QSettings::setPath (QSettings::IniFormat, QSettings::UserScope, _directory.path());
		Translations::instance().set_language (Language::English);
	}

	void
	cleanup()
	{
		Translations::instance().set_language (Language::English);
		QSettings().clear();
		QApplication::processEvents();
	}

	void
	language_preferences()
	{
		QCOMPARE (Preferences::load().language, Language::System);
		for (auto const language: { Language::System, Language::English, Language::Polish,
			Language::Japanese, Language::Ukrainian, Language::French })
		{
			Preferences preferences;
			preferences.language = language;
			preferences.save();
			QCOMPARE (Preferences::load().language, language);
			QCOMPARE (QSettings().value ("ui/language").toString(), language_code (language));
			PreferencesDialog dialog (Preferences::load());
			auto* const combo = dialog.findChild<QComboBox*> ("language");
			QVERIFY (combo);
			QCOMPARE (combo->count(), 6);
			QCOMPARE (dialog.preferences().language, language);
			combo->setCurrentIndex (combo->findData (static_cast<int> (Language::English)));
			QCOMPARE (dialog.preferences().language, Language::English);
		}
		QSettings().setValue ("ui/language", "unsupported");
		QCOMPARE (Preferences::load().language, Language::System);
		QCOMPARE (system_language_code ({ "pl-PL" }), "pl");
		QCOMPARE (system_language_code ({ "ja-Jpan-JP" }), "ja");
		QCOMPARE (system_language_code ({ "uk_UA" }), "uk");
		QCOMPARE (system_language_code ({ "fr-CA" }), "fr");
		QCOMPARE (system_language_code ({ "de-DE", "pl-PL", "en" }), "pl");
		QCOMPARE (system_language_code ({ "en-GB", "pl" }), "en");
		QCOMPARE (system_language_code ({ "de-DE", "zh-Hans" }), "en");
		QCOMPARE (system_language_code ({}), "en");
	}

	void
	translation_catalogs()
	{
		QStringList const titles { "Preferencje", "環境設定", "Налаштування", "Préférences" };
		QStringList const cancel_labels { "Anuluj", "キャンセル", "Скасувати", "Annuler" };
		int index = 0;
		for (auto const language: { Language::Polish, Language::Japanese, Language::Ukrainian, Language::French })
		{
			auto const code = language_code (language);
			QFile file (QString (MAWARI_TRANSLATIONS_DIR "/mawari_%1.ts").arg (code));
			QVERIFY (file.open (QIODevice::ReadOnly));
			QTranslator catalog;
			QVERIFY (catalog.load (":/i18n/mawari_" + code + ".qm"));
			QXmlStreamReader xml (&file);
			int count = 0;
			while (!xml.atEnd())
			{
				xml.readNext();
				if (!xml.isStartElement() || xml.name() != QLatin1String ("message"))
					continue;
				QString source, translated;
				while (xml.readNextStartElement())
				{
					if (xml.name() == QLatin1String ("source"))
						source = xml.readElementText();
					else if (xml.name() == QLatin1String ("translation"))
					{
						QVERIFY (xml.attributes().value ("type").isEmpty());
						translated = xml.readElementText();
					}
					else
						xml.skipCurrentElement();
				}
				QVERIFY2 (!translated.isEmpty(), qPrintable (source));
				QCOMPARE (placeholders (translated), placeholders (source));
				QCOMPARE (catalog.translate ("mawari::Translations", source.toUtf8().constData()), translated);
				++count;
			}
			QVERIFY (!xml.hasError());
			QVERIFY (count >= 97);
			Translations::instance().set_language (language);
			QApplication::processEvents();
			QCOMPARE (Translations::instance().resolved_language(), code);
			PreferencesDialog dialog (Preferences {});
			QCOMPARE (dialog.windowTitle(), titles[index]);
			auto* const buttons = dialog.findChild<QDialogButtonBox*>();
			QVERIFY (buttons);
			QCOMPARE (buttons->button (QDialogButtonBox::Cancel)->text().remove ('&'), cancel_labels[index]);
			auto* const combo = dialog.findChild<QComboBox*> ("language");
			QCOMPARE (combo->itemText (1), "English");
			QCOMPARE (combo->itemText (2), "Polski");
			QCOMPARE (combo->itemText (3), "日本語");
			QCOMPARE (combo->itemText (4), "Українська");
			QCOMPARE (combo->itemText (5), "Français");
			QCOMPARE (export_image (ImageExportRequest {}, std::make_shared<ImageExportProgress>()).error,
				Translations::tr ("Invalid export dimensions."));
			++index;
		}
	}

	void
	live_language_change()
	{
		Preferences preferences;
		preferences.language = Language::English;
		preferences.save();
		MainWindow window;
		auto* const view = window.findChild<PanoramaView*>();
		QVERIFY (view);
		auto const path = _directory.filePath ("localization.png");
		QImage image (128, 64, QImage::Format_RGB32);
		image.fill (Qt::green);
		QVERIFY (image.save (path));
		QSignalSpy loaded (view, &PanoramaView::file_loaded);
		window.open_file (path);
		QTRY_COMPARE (loaded.count(), 1);
		view->set_projection_mode (ProjectionMode::SmallPlanet);
		QWheelEvent wheel ({ 100, 100 }, { 100, 100 }, {}, { 0, -720 }, Qt::NoButton, Qt::NoModifier,
			Qt::NoScrollPhase, false);
		QApplication::sendEvent (view, &wheel);
		auto const camera = view->camera();
		auto const actions = window.actions();
		auto* const preferences_action = window.menuBar()->actions()[0]->menu()->actions()[2];
		QStringList const file_labels { "Plik", "ファイル", "Файл", "Fichier", "File" };
		int index = 0;
		for (auto const language: { Language::Polish, Language::Japanese, Language::Ukrainian, Language::French, Language::English })
		{
			bool accepted = false;
			QTimer::singleShot (0, &window, [&] {
				auto* const dialog = dynamic_cast<PreferencesDialog*> (QApplication::activeModalWidget());
				QVERIFY (dialog);
				auto const close_dialog = qScopeGuard ([dialog] { if (dialog->isVisible()) dialog->reject(); });
				auto* const combo = dialog->findChild<QComboBox*> ("language");
				QVERIFY (combo);
				combo->setCurrentIndex (combo->findData (static_cast<int> (language)));
				accepted = true;
				dialog->accept();
			});
			preferences_action->trigger();
			QVERIFY (accepted);
			QApplication::processEvents();
			QCOMPARE (window.menuBar()->actions()[0]->text(), file_labels[index++]);
			QCOMPARE (Preferences::load().language, language);
			QCOMPARE (view->accessibleName(), Translations::tr ("360 degree panorama"));
			QCOMPARE (window.actions(), actions);
			QCOMPARE (view->camera().fov, camera.fov);
			QCOMPARE (view->camera().yaw, camera.yaw);
			QCOMPARE (view->camera().pitch, camera.pitch);
			QCOMPARE (view->camera().projection, camera.projection);
			QCOMPARE (view->image_path(), path);
			QCOMPARE (loaded.count(), 1);
			QVERIFY (window.statusBar()->currentMessage().contains ("128 × 64"));

			bool inspected = false;
			QTimer::singleShot (0, &window, [&] {
				auto* const menu = qobject_cast<QMenu*> (QApplication::activePopupWidget());
				QVERIFY (menu);
				auto const close_menu = qScopeGuard ([menu] { menu->close(); });
				QCOMPARE (menu->actions()[0]->text(), Translations::tr ("Navigation"));
				QCOMPARE (menu->actions()[4]->text(), Translations::tr ("Open…"));
				QCOMPARE (menu->actions()[6]->text(), Translations::tr ("Preferences…"));
				auto* const mode = menu->actions()[0]->menu()->actions()[2];
				QCOMPARE (mode->text(), navigation_mode_name (NavigationMode::HoldToPan) + '\t'
					+ navigation_mode_shortcut (NavigationMode::HoldToPan).toString (QKeySequence::NativeText));
				inspected = true;
			});
			QContextMenuEvent context (QContextMenuEvent::Mouse, { 10, 10 }, view->mapToGlobal (QPoint (10, 10)));
			QApplication::sendEvent (view, &context);
			QVERIFY (inspected);
		}

		// Selecting a different language and cancelling must leave both UI and settings intact.
		QTimer::singleShot (0, &window, [&] {
			auto* const dialog = dynamic_cast<PreferencesDialog*> (QApplication::activeModalWidget());
			QVERIFY (dialog);
			dialog->findChild<QComboBox*> ("language")->setCurrentIndex (2);
			dialog->reject();
		});
		preferences_action->trigger();
		QCOMPARE (Preferences::load().language, Language::English);
		QCOMPARE (Translations::instance().resolved_language(), "en");
		QCOMPARE (window.menuBar()->actions()[0]->text(), "File");
	}

  private:
	QTemporaryDir _directory;
};


int
main (int argc, char* argv[])
{
	QApplication application (argc, argv);
	QApplication::setOrganizationName ("MawariLocalizationTests");
	QApplication::setApplicationName ("MawariLocalizationTests");
	LocalizationTest test;
	return QTest::qExec (&test, argc, argv);
}


#include "localization.test.moc"

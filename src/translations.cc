// vim:ts=4

// Local:
#include "translations.h"

// Qt:
#include <QCoreApplication>
#include <QLocale>
#include <QResource>


static void
initialize_translation_resources()
{
	Q_INIT_RESOURCE (mawari_translations);
}


namespace mawari {

QString
language_code (Language const language)
{
	switch (language)
	{
		case Language::System: return "system";
		case Language::English: return "en";
		case Language::Polish: return "pl";
		case Language::Japanese: return "ja";
		case Language::Ukrainian: return "uk";
		case Language::French: return "fr";
	}

	return "system";
}


Language
language_from_code (QString const& code)
{
	for (auto const language: { Language::English, Language::Polish, Language::Japanese, Language::Ukrainian, Language::French })
		if (language_code (language) == code)
			return language;

	return Language::System;
}


QString
system_language_code (QStringList const& preferred_languages)
{
	for (auto const& tag: preferred_languages)
	{
		auto const code = tag.section ('-', 0, 0).section ('_', 0, 0).toLower();
		if (language_from_code (code) != Language::System)
			return code;
	}

	return "en";
}


Translations::Translations (QObject* const parent):
	QObject (parent)
{
	initialize_translation_resources();
}


Translations&
Translations::instance()
{
	static auto* const instance = new Translations (QCoreApplication::instance());
	return *instance;
}


void
Translations::set_language (Language const language)
{
	auto const code = language == Language::System ? system_language_code (QLocale::system().uiLanguages()) : language_code (language);
	if (_resolved_language == code)
		return;

	QCoreApplication::removeTranslator (&_application);
	QCoreApplication::removeTranslator (&_qt);
	_resolved_language = code;

	if (code != "en")
	{
		if (_qt.load (":/i18n/qtbase_" + code + ".qm"))
			QCoreApplication::installTranslator (&_qt);
		if (_application.load (":/i18n/mawari_" + code + ".qm"))
			QCoreApplication::installTranslator (&_application);
	}
}

} // namespace mawari

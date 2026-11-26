// vim:ts=4

#ifndef MAWARI__TRANSLATIONS_H__INCLUDED
#define MAWARI__TRANSLATIONS_H__INCLUDED

// Qt:
#include <QObject>
#include <QStringList>
#include <QTranslator>


namespace mawari {

enum class Language
{
	System,
	English,
	Polish,
	Japanese,
	Ukrainian,
	French,
};


QString
language_code (Language);


Language
language_from_code (QString const&);


QString
system_language_code (QStringList const& preferred_languages);


class Translations final: public QObject
{
	Q_OBJECT

  public:
	static Translations&
	instance();

	void
	set_language (Language);

	[[nodiscard]]
	QString const&
	resolved_language() const
		{ return _resolved_language; }

  private:
	explicit
	Translations (QObject* parent);

	QTranslator _application;
	QTranslator _qt;
	QString _resolved_language;
};

} // namespace mawari

#endif

// vim:ts=4

// Local:
#include "application_icon.h"

// Qt:
#include <QResource>


static void
initialize_icon_resources()
{
	Q_INIT_RESOURCE (mawari_icons);
}


namespace mawari {

QIcon
application_icon()
{
	initialize_icon_resources();
	QIcon icon;

	// Supply concrete pixmap sizes to window systems, without an SVG icon plugin.
	for (auto const size: application_icon_sizes)
		icon.addFile (QString (":/icons/%1/mawari.png").arg (size), QSize (size, size));

	return icon;
}

} // namespace mawari

// vim:ts=4

#ifndef MAWARI__APPLICATION_ICON_H__INCLUDED
#define MAWARI__APPLICATION_ICON_H__INCLUDED

// Qt:
#include <QIcon>

// Standard:
#include <array>


namespace mawari {

inline constexpr std::array application_icon_sizes { 16, 22, 24, 32, 48, 64, 96, 128, 256, 512 };


QIcon
application_icon();

} // namespace mawari

#endif

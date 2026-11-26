// vim:ts=4

// Local:
#include "camera.h"

// Standard:
#include <algorithm>
#include <cmath>
#include <numbers>


namespace mawari {

static double
smooth_transition (double const value, double const start, double const end)
{
	auto const x = std::clamp ((value - start) / (end - start), 0.0, 1.0);
	return x * x * (3.0 - 2.0 * x);
}


Camera::Extents
Camera::extents (double const aspect) const
{
	auto const safe_aspect = std::max (0.001, aspect);
	// The perspective component is unused after the stereographic blend reaches
	// one. Keep it finite even when the active FOV exceeds 180 degrees.
	auto const tangent = std::tan (std::min (this->fov, 179.0) * std::numbers::pi / 360.0);
	return this->axis == FovAxis::Horizontal
		? Extents { tangent, tangent / safe_aspect }
		: Extents { tangent * safe_aspect, tangent };
}


void
Camera::set_projection (ProjectionMode const projection)
{
	auto const viewing_pitch = view_pitch();
	this->projection = projection;
	this->fov = std::clamp (this->fov, 10.0, maximum_fov());
	this->pitch = viewing_pitch + automatic_tilt();
	clamp_pitch();
}


void
Camera::set_fov (double const fov)
{
	this->fov = std::clamp (fov, 10.0, maximum_fov());
	clamp_pitch();
}


Camera::Extents
Camera::stereographic_extents (double const aspect) const
{
	auto const safe_aspect = std::max (0.001, aspect);
	// Stereographic radius = tan(theta / 2), with theta = half the total FOV.
	auto const tangent = std::tan (this->fov * std::numbers::pi / 720.0);
	return this->axis == FovAxis::Horizontal
		? Extents { tangent, tangent / safe_aspect }
		: Extents { tangent * safe_aspect, tangent };
}


double
Camera::projection_blend() const
{
	return this->projection == ProjectionMode::SmallPlanet ? smooth_transition (this->fov, 110.0, 180.0) : 0.0;
}


double
Camera::view_pitch() const
{
	return std::clamp (this->pitch - automatic_tilt(), -pitch_limit(), pitch_limit());
}


double
Camera::automatic_tilt() const
{
	// Bias the zoomed-out view towards the ground by default. Manual pitch
	// can compensate for this entire offset and reach the zenith as well.
	return this->projection == ProjectionMode::SmallPlanet ? 90.0 * smooth_transition (this->fov, 110.0, 260.0) : 0.0;
}


void
Camera::clamp_pitch()
{
	auto const tilt = automatic_tilt();
	// Limit the actual viewing elevation, rather than its pre-tilt value.
	// This avoids a horizon stop on the way from small planet to small sky.
	this->pitch = std::clamp (this->pitch, tilt - pitch_limit(), tilt + pitch_limit());
}


void
Camera::turn (double const horizontal, double const vertical)
{
	this->yaw = std::remainder (this->yaw + horizontal, 360.0);
	this->pitch += vertical;
	clamp_pitch();
}


void
Camera::zoom (double const steps)
{
	auto const factor = std::pow (0.97, steps);

	if (this->projection == ProjectionMode::SmallPlanet)
	{
		// Near 360 degrees, a small FOV change can greatly change the planet's
		// size. Zoom the stereographic plane instead to keep size steps uniform.
		auto const radius = std::tan (this->fov * std::numbers::pi / 720.0);
		this->fov = std::atan (radius * factor) * 720.0 / std::numbers::pi;
	}
	else
		this->fov *= factor;

	set_fov (this->fov);
}

} // namespace mawari

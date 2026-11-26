// vim:ts=4

#ifndef MAWARI__CAMERA_H__INCLUDED
#define MAWARI__CAMERA_H__INCLUDED

// Local:
#include "preferences.h"


namespace mawari {

/** Angles are in degrees; plane half-extents depend on the projection. */
struct Camera
{
	struct Extents
	{
		double horizontal;
		double vertical;
	};

	double	yaw		= 0.0;
	double	pitch	= 0.0;
	double	fov		= 90.0;
	FovAxis	axis	= FovAxis::Horizontal;
	ProjectionMode projection = ProjectionMode::Standard;

	[[nodiscard]]
	Extents
	extents (double aspect) const;

	[[nodiscard]]
	Extents
	stereographic_extents (double aspect) const;

	[[nodiscard]]
	double
	projection_blend() const;

	[[nodiscard]]
	double
	view_pitch() const;

	[[nodiscard]]
	double
	maximum_fov() const
		{ return this->projection == ProjectionMode::SmallPlanet ? 350.0 : 150.0; }

	void
	set_projection (ProjectionMode);

	void
	set_fov (double);

	void
	turn (double horizontal, double vertical);

	void
	zoom (double steps);

  private:
	[[nodiscard]]
	double
	automatic_tilt() const;

	[[nodiscard]]
	double
	pitch_limit() const
		{ return this->projection == ProjectionMode::SmallPlanet ? 90.0 : 89.9; }

	void
	clamp_pitch();
};

} // namespace mawari

#endif

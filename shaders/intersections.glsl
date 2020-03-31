#ifndef INTERSECTIONS_GLSL
#define INTERSECTIONS_GLSL

#include <common.glsl>

bool raySphereIntersection(vec3 center, float radius, vec3 origin, vec3 direction, out float t){

	vec3 relOrigin = origin - center;

	// (quadratic formula)
	float a = dot(direction, direction);
	float b = 2.0 * dot(direction, relOrigin);
	float c = dot(relOrigin, relOrigin) - square(radius);

	float descriminant = b * b - 4.0 * a * c;
	if (descriminant < 0.0) {
		return false;
	}

	t = (-b - sqrt(descriminant)) / (2.0 * a);
	if (t >= gl_RayTminNV && t <= gl_RayTmaxNV) {
		return true;
	}

	t = (-b + sqrt(descriminant)) / (2.0 * a);
	if (t >= gl_RayTminNV && t <= gl_RayTmaxNV) {
		return true;
	}

	return false;
}

#endif // INTERSECTIONS_GLSL

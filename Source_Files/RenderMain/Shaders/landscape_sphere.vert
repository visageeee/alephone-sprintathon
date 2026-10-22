R"(

uniform mat4 landscapeInverseMatrix;
varying vec3 relDir;
uniform vec4 visibleSunEye;
varying vec3 sunWorldRay;
varying vec4 vertexColor;
varying float cosPitch;
varying float sinPitch;
varying float cosYaw;
varying float sinYaw;
uniform float yaw;
uniform float pitch;

void main(void) {
    sunWorldRay=gl_Vertex.xyz-visibleSunEye.xyz;
	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
#ifndef DISABLE_CLIP_VERTEX
	gl_ClipVertex = gl_ModelViewMatrix * gl_Vertex;
#endif
	// Transpose the inverse rotation to map the world ray into an unrolled
	// view. The full modelview already rolls the sky on screen; including
	// roll in its texture coordinates would make it slide beneath the sun.
	// Dot products keep this compatible with GLSL 1.20 (no transpose()).
	relDir = vec3(dot(landscapeInverseMatrix[0].xyz, sunWorldRay),
	              dot(landscapeInverseMatrix[1].xyz, sunWorldRay),
	              dot(landscapeInverseMatrix[2].xyz, sunWorldRay));
	vertexColor = gl_Color;
	
	cosYaw = cos(yaw);
	cosPitch = cos(-pitch);
	sinYaw = sin(yaw);
	sinPitch = sin(-pitch);
}

)"


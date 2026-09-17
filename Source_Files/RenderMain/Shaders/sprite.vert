R"(

uniform float depth;
uniform float strictDepthMode;
uniform float objectWorldZ;
varying vec3 viewDir;
varying float worldZ;
varying vec4 vertexColor;
varying float classicDepth;
void main(void) {
	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
	classicDepth = gl_Position.z / 8192.0;
#ifndef DISABLE_CLIP_VERTEX
	gl_ClipVertex = gl_ModelViewMatrix * gl_Vertex;
#endif
	vec4 v = gl_ModelViewMatrixInverse * vec4(0.0, 0.0, 0.0, 1.0);
	viewDir = (gl_Vertex - v).xyz;
	// Sprite vertices are local to a modelview translation. Restore their
	// actual level height for media-relative fog.
	worldZ = objectWorldZ + gl_Vertex.z;
	gl_TexCoord[0] = gl_TextureMatrix[0] * gl_MultiTexCoord0;
	vertexColor = gl_Color;
}

)"

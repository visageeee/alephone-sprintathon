R"(

uniform mat4 landscapeInverseMatrix;
varying vec3 relDir;
uniform vec4 visibleSunEye;
varying vec3 sunWorldRay;
varying vec4 vertexColor;
void main(void) {
    sunWorldRay=gl_Vertex.xyz-visibleSunEye.xyz;
	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
#ifndef DISABLE_CLIP_VERTEX
	gl_ClipVertex = gl_ModelViewMatrix * gl_Vertex;
#endif
	relDir = (gl_ModelViewMatrix * gl_Vertex).xyz;
	vertexColor = gl_Color;
}

)"


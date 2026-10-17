R"(
varying vec4 reflectionUV;
varying vec3 reflectionWorld;
varying vec4 reflectionColour;
varying float reflectionDistance;
void main() {
    vec4 eye=gl_ModelViewMatrix*gl_Vertex;
    gl_Position=gl_ProjectionMatrix*eye;
    gl_ClipVertex=eye;
    reflectionUV=gl_MultiTexCoord0;
    reflectionWorld=gl_Vertex.xyz;
    reflectionColour=gl_Color;
    reflectionDistance=length(eye.xyz);
}
)"

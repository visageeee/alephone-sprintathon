R"(
uniform float depth;
uniform float pulsate;
uniform float wobble;
varying vec2 maskUV;
varying float maskAlpha;
varying vec3 maskViewXY;
void main() {
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    gl_Position.z += depth * gl_Position.z / 65536.0;
#ifndef DISABLE_CLIP_VERTEX
    gl_ClipVertex = gl_ModelViewMatrix * gl_Vertex;
#endif
    maskUV = (gl_TextureMatrix[0] * gl_MultiTexCoord0).xy;
    maskViewXY = vec3(0.0);
    // Keep animated alpha cutouts aligned with the regular wall pass.
    if (abs(pulsate) + abs(wobble) > 0.0) {
        vec3 n = normalize(gl_NormalMatrix * gl_Normal);
        vec3 t = normalize(gl_NormalMatrix * gl_MultiTexCoord1.xyz);
        vec3 b = normalize(cross(n,t) * gl_MultiTexCoord1.w);
        mat3 tbn = mat3(t.x,b.x,n.x,t.y,b.y,n.y,t.z,b.z,n.z);
        vec3 viewXY = -(gl_TextureMatrix[0] * vec4(tbn * (gl_ModelViewMatrix * gl_Vertex).xyz,1.0)).xyz;
        maskViewXY = viewXY;
    }
    maskAlpha = gl_Color.a;
}
)"

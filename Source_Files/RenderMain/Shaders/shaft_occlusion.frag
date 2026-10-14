R"(
uniform sampler2D texture0;
uniform float visibility;
uniform float pulsate;
uniform float wobble;
varying vec2 maskUV;
varying float maskAlpha;
varying vec3 maskViewXY;
void main() {
    vec2 uv = maskUV;
    if (abs(pulsate) + abs(wobble) > 0.0) {
        vec3 direction = maskViewXY / max(length(maskViewXY),0.000001);
        uv += vec2(direction.y * -pulsate,direction.x * pulsate);
        uv += vec2(direction.y * -wobble * uv.y,wobble * uv.y);
    }
    // Alpha testing, blending, depth and portal clipping retain their usual
    // state. No colour lighting, fog, normal maps or scene refraction here.
    gl_FragColor = vec4(0.0,0.0,0.0,texture2D(texture0,uv).a * maskAlpha * visibility);
}
)"

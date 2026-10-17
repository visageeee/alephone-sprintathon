R"(

uniform sampler2D texture0;
uniform float fogMix;
uniform float scalex;
uniform float scaley;
uniform float offsetx;
uniform float offsety;
uniform float yaw;
uniform float pitch;
varying vec3 relDir;
varying vec4 vertexColor;
const float zoom = 1.2;
const float pitch_adjust = 0.96;
varying vec3 sunWorldRay;

uniform vec4 visibleSun;
uniform vec4 visibleSunColor;
vec3 sprintathonSunDisc(vec3 ray) {
    if(visibleSunColor.w<=0.0)return vec3(0.0);
    float alignment=dot(normalize(ray),visibleSun.xyz);
    float radius=max(1.0-visibleSun.w,0.000001);
    float feather=max(fwidth(alignment),radius*0.15);
    float disc=smoothstep(visibleSun.w-feather,visibleSun.w+feather,alignment);
    float halo=0.035*exp2(-max(0.0,1.0-alignment)/(radius*4.0));
    return visibleSunColor.rgb*visibleSunColor.w*(disc+halo);
}

void main(void) {
	vec3 facev = vec3(cos(yaw), sin(yaw), sin(pitch));
	vec3 relv  = (relDir);
	float x = relv.x / (relv.z * zoom) + atan(facev.x, facev.y);
	float y = relv.y / (relv.z * zoom) - (facev.z * pitch_adjust);
	vec4 color = texture2D(texture0, vec2(offsetx - x * scalex, offsety - y * scaley));
    color.rgb += sprintathonSunDisc(sunWorldRay);
	vec3 intensity = mix(color.rgb, gl_Fog.color.rgb, fogMix);
	gl_FragColor = vec4(intensity, 1.0);
	// Keep landscapes at a recognizable far depth so screen-space effects do
	// not mistake the sky for nearby scene geometry.
	gl_FragDepth = 0.9999;
}

)"

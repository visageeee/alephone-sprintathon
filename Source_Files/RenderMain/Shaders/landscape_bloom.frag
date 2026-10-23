R"(

uniform sampler2D texture0;
uniform float fogMix;
uniform float scalex;
uniform float scaley;
uniform float offsetx;
uniform float offsety;
uniform float yaw;
uniform float pitch;
uniform float bloomScale;
varying vec3 relDir;
varying vec4 vertexColor;
const float zoom = 1.205;
const float pitch_adjust = 0.955;
varying vec3 sunWorldRay;

uniform vec4 timeOfDayZenith;
uniform vec4 timeOfDayHorizon;
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
	vec3 relv  = normalize(relDir);
	float x = relv.x / (relv.z * zoom) + atan(facev.x, facev.y);
	float y = relv.y / (relv.z * zoom) - (facev.z * pitch_adjust);
	vec4 color = texture2D(texture0, vec2(offsetx - x * scalex, offsety - y * scaley));
    if(timeOfDayZenith.w>0.5) color.rgb=vec3(0.0);
    color.rgb += sprintathonSunDisc(sunWorldRay);
	float intensity = clamp(bloomScale, 0.0, 1.0);

#ifdef GAMMA_CORRECTED_BLENDING
	//intensity = intensity * intensity;
	color.rgb = (color.rgb - 0.01) * 1.01;
#else
	color.rgb = (color.rgb - 0.1) * 1.11;
#endif
	gl_FragColor = vec4(color.rgb * intensity * (1.0 - fogMix), 1.0);
	gl_FragDepth = 0.9999;
}

)"

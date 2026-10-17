R"(

uniform sampler2D texture0;
uniform float fogMix;
uniform float bloomScale;
varying vec3 relDir;
varying vec4 vertexColor;
varying float cosPitch;
varying float sinPitch;
varying float cosYaw;
varying float sinYaw;
uniform float offsetx; // azimuth in the sphere map
const float M_PI = 3.14156;
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
	mat3 rotateYaw = mat3(cosYaw, 0, sinYaw,
						  0, 1, 0,
						  -sinYaw, 0, cosYaw);

	mat3 rotatePitch = mat3(1, 0, 0,
							0, cosPitch, -sinPitch,
							0, sinPitch, cosPitch);

	vec3 normRelDir = rotateYaw * rotatePitch * normalize(relDir);
	
	float theta = atan(normRelDir.x, normRelDir.z) - offsetx;
	float phi = acos(normRelDir.y);

	float u = (M_PI - theta) / (2.0 * M_PI);
	float v = phi / M_PI;

	vec4 color = texture2D(texture0, vec2(u, v));
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

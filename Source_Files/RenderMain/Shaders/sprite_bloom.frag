R"(

uniform sampler2D texture0;
uniform float glow;
uniform vec4 sprintathonLightColor;
uniform float bloomScale;
uniform float bloomShift;
uniform float fogMode;
uniform float mediaFogEnabled;
uniform float mediaFogTop;
uniform float mediaFogSoftness;
varying vec3 viewDir;
varying float worldZ;
varying vec4 vertexColor;
varying float classicDepth;

float getFogFactor(float distance) {
	if (fogMode == 0.0) {
        return clamp((gl_Fog.end - distance) / (gl_Fog.end - gl_Fog.start), 0.0, 1.0);
    } else if (fogMode == 1.0) {
        return clamp(exp(-gl_Fog.density * distance), 0.0, 1.0);
    } else if (fogMode == 2.0) {
        return clamp(exp(-gl_Fog.density * gl_Fog.density * distance * distance), 0.0, 1.0);
	} else {
		return 1.0;
	}
}


uniform vec4 flameRipple; // enabled, simulation time, phase, unused
uniform vec4 flameBounds;
vec4 flameSpriteColor(vec2 uv) {
    vec2 span = max(flameBounds.zw - flameBounds.xy, vec2(0.00001));
    vec2 p = (uv - flameBounds.xy) / span;
    float t = flameRipple.y * 7.0 + flameRipple.z;
    vec2 warp = vec2(sin(p.y * 19.0 - t) + 0.45 * sin(p.y * 37.0 + t * 1.3),
                     sin(p.x * 17.0 + t * 0.8));
    vec2 q = uv + warp * span * vec2(0.024, 0.012);
    if (any(lessThan(q, flameBounds.xy)) || any(greaterThan(q, flameBounds.zw)))
        return vec4(0.0);
    vec4 c = texture2D(texture0, q);
    float bright = max(c.r, max(c.g, c.b));
    float edge = smoothstep(0.0, 0.06, min(min(p.x, p.y), min(1.0-p.x, 1.0-p.y)));
    c.a *= mix(0.35, 0.78, bright) * edge * (0.94 + 0.06 * sin(t + p.y * 23.0));
    return c;
}

void main (void) {
	vec4 color = flameRipple.x > 0.0 ? flameSpriteColor(gl_TexCoord[0].xy) : texture2D(texture0, gl_TexCoord[0].xy);
	vec3 intensity = clamp(vertexColor.rgb + sprintathonLightColor.rgb, glow, 1.0);
	//intensity = intensity * clamp(2.0 - length(viewDir)/8192.0, 0.0, 1.0);
	intensity = clamp(intensity * bloomScale + bloomShift, 0.0, 1.0);
#ifdef GAMMA_CORRECTED_BLENDING
	intensity = intensity * intensity;  // approximation of pow(intensity, 2.2)
	color.rgb = (color.rgb - 0.06) * 1.02;
#else
	color.rgb = (color.rgb - 0.2) * 1.25;
#endif
	float fogFactor = getFogFactor(length(viewDir));
	if (mediaFogEnabled > 0.0) {
		float heightFog = clamp((mediaFogTop - worldZ) / mediaFogSoftness, 0.0, 1.0);
		float heightMask = mix(1.0, heightFog, mediaFogEnabled);
		fogFactor = 1.0 - (1.0 - fogFactor) * heightMask;
	}
	gl_FragColor = vec4(mix(vec3(0.0, 0.0, 0.0), color.rgb * intensity, fogFactor), vertexColor.a * color.a);
}

)"

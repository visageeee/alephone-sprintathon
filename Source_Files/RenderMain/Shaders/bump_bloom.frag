R"(

uniform sampler2D texture0;
uniform sampler2D texture1;
uniform float pulsate;
uniform float wobble;
uniform float glow;
uniform float flare;
uniform float bloomScale;
uniform float bloomShift;
uniform float fogMode;
uniform float mediaFogEnabled;
uniform float mediaFogTop;
uniform float mediaFogSoftness;
uniform float mediaRipple;
uniform float time;
varying vec3 viewXY;
varying vec3 viewDir;
varying float worldZ;
varying vec4 vertexColor;

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

void main (void) {
	vec3 texCoords = vec3(gl_TexCoord[0].xy, 0.0);
	float rippleStrength = mediaRipple;
	float rippleBloom = 1.0;
	if (rippleStrength > 0.001) {
		float phase = time;
		vec2 base = texCoords.xy;
		vec2 fineWaves = vec2(
			sin(base.y * 18.849556 + phase * 3.0) + 0.55 * sin((base.x + base.y) * 12.566371 - phase * 2.0),
			cos(base.x * 18.849556 - phase * 2.0) + 0.50 * sin((base.x - base.y) * 12.566371 + phase));
		vec2 broadWaves = vec2(
			sin(base.y * 6.2831853 + phase),
			cos(base.x * 6.2831853 - phase));
		texCoords.xy += (broadWaves * 0.008 + fineWaves * 0.0045) * rippleStrength;
		float fresnel = pow(1.0 - abs(normalize(viewDir).z), 3.0);
		rippleBloom = 1.0 + fresnel * 0.15 * rippleStrength;
	}
	vec3 normXY = normalize(viewXY);
	texCoords += vec3(normXY.y * -pulsate, normXY.x * pulsate, 0.0);
	texCoords += vec3(normXY.y * -wobble * texCoords.y, wobble * texCoords.y, 0.0);
	vec3 viewv = normalize(viewDir);
	// iterative parallax mapping
	float scale = 0.010;
	float bias = -0.005;
	for(int i = 0; i < 4; ++i) {
		vec4 normal = texture2D(texture1, texCoords.xy);
		float h = normal.a * scale + bias;
		texCoords.x += h * viewv.x;
		texCoords.y -= h * viewv.y;
	}
	vec3 norm = (texture2D(texture1, texCoords.xy).rgb - 0.5) * 2.0;
	float diffuse = 0.5 + abs(dot(norm, viewv))*0.5;
	if (glow > 0.001) {
		diffuse = 1.0;
	}
	vec4 color = texture2D(texture0, texCoords.xy);
	vec3 intensity = clamp(vertexColor.rgb, glow, 1.0);
	intensity = clamp(intensity * bloomScale + bloomShift, 0.0, 1.0);
	intensity = clamp(intensity * rippleBloom, 0.0, 1.0);
#ifdef GAMMA_CORRECTED_BLENDING
	intensity = intensity * intensity; // approximation of pow(intensity, 2.2)
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

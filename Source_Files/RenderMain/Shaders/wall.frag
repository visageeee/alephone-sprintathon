R"(

uniform sampler2D texture0;
uniform float pulsate;
uniform float wobble;
uniform float glow;
uniform float flare;
uniform float selfLuminosity;
uniform float fogMode;
uniform float mediaFogEnabled;
uniform float mediaFogTop;
uniform float mediaFogSoftness;
uniform float mediaRipple;
uniform float mediaWetness;
uniform float time;
varying vec3 viewXY;
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

void main (void) {
	vec3 texCoords = vec3(gl_TexCoord[0].xy, 0.0);
	float rippleStrength = mediaRipple;
	float rippleHighlight = 1.0;
	vec2 mediaDetailOffset = vec2(0.0);
	float mediaTextureMix = 0.0;
	float wetTextureShade = 1.0;
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
		vec2 slope = vec2(
			0.24 * cos(base.x * 6.2831853 - phase) + 0.10 * cos((base.x + base.y) * 12.566371 - phase * 2.0),
			0.27 * cos(base.y * 6.2831853 + phase) + 0.10 * sin(base.y * 18.849556 + phase * 3.0));
		slope *= rippleStrength;
		vec3 waveNormal = normalize(vec3(-slope.x, -slope.y, 1.0));
		float waveLight = max(0.0, dot(waveNormal, normalize(vec3(0.35, -0.25, 0.90))));
		float rippleFresnel = pow(1.0 - abs(normalize(viewDir).z), 3.0);
		mediaDetailOffset = vec2(fineWaves.y, -fineWaves.x) *
			0.0125 * mediaWetness;
		mediaTextureMix = clamp((0.34 + rippleFresnel * 0.14) *
			mediaWetness, 0.0, 0.95);
		wetTextureShade = clamp(1.0 + (waveLight - 0.5) *
			0.12 * mediaWetness, 0.72, 1.28);
		rippleHighlight = clamp(1.0 + (waveLight - 0.5) * 0.24 *
			rippleStrength + rippleFresnel * 0.05 * rippleStrength,
			0.65, 1.35);
	}
	vec3 normXY = normalize(viewXY);
	texCoords += vec3(normXY.y * -pulsate, normXY.x * pulsate, 0.0);
	texCoords += vec3(normXY.y * -wobble * texCoords.y, wobble * texCoords.y, 0.0);
	float mlFactor = clamp(selfLuminosity + flare - classicDepth, 0.0, 1.0);
	// more realistic: replace classicDepth with (length(viewDir)/8192.0)
	vec3 intensity;
	if (vertexColor.r > mlFactor) {
		intensity = vertexColor.rgb + (mlFactor * 0.5); }
	else {
		intensity = (vertexColor.rgb * 0.5) + mlFactor; }
	intensity = clamp(intensity, glow, 1.0);
	intensity = clamp(intensity * rippleHighlight, glow, 1.0);
#ifdef GAMMA_CORRECTED_BLENDING
	intensity = intensity * intensity; // approximation of pow(intensity, 2.2)
#endif
	vec4 color = texture2D(texture0, texCoords.xy);
	if (mediaWetness > 0.001) {
		vec4 shiftedA = texture2D(texture0,
			texCoords.xy + mediaDetailOffset);
		vec4 shiftedB = texture2D(texture0,
			texCoords.xy + vec2(-mediaDetailOffset.y,
				mediaDetailOffset.x) * 0.73);
		vec4 shiftedC = texture2D(texture0,
			texCoords.xy - mediaDetailOffset * 0.46);
		vec4 refracted = mix(mix(shiftedA, shiftedB, 0.5),
			shiftedC, 0.28);
		color = mix(color, refracted, mediaTextureMix);
		float localLuma = dot(color.rgb, vec3(0.299, 0.587, 0.114));
		color.rgb = mix(vec3(localLuma), color.rgb,
			1.0 + 0.10 * mediaWetness) * wetTextureShade;
	}
	vec3 shadedColor = clamp(color.rgb * intensity, 0.0, 1.0);
	float fogFactor = getFogFactor(length(viewDir));
	if (mediaFogEnabled > 0.0) {
		float heightFog = clamp((mediaFogTop - worldZ) / mediaFogSoftness, 0.0, 1.0);
		float heightMask = mix(1.0, heightFog, mediaFogEnabled);
		fogFactor = 1.0 - (1.0 - fogFactor) * heightMask;
	}
	gl_FragColor = vec4(mix(gl_Fog.color.rgb, shadedColor, fogFactor), vertexColor.a * color.a);
}

)"

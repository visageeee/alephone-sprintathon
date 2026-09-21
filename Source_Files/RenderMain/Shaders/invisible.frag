R"(

uniform sampler2D texture0;
uniform sampler2DRect texture2;
uniform float visibility;
uniform float time;
uniform float pixelWidth;
uniform float pixelHeight;
uniform float fogMode;
uniform float mediaFogEnabled;
uniform float mediaFogTop;
uniform float mediaFogSoftness;
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

void main(void) {
	vec4 color = texture2D(texture0, gl_TexCoord[0].xy);
	float mask = color.a * vertexColor.a;
	if (mask < 0.02)
		discard;

	if (pixelWidth < 1.0 || pixelHeight < 1.0) {
		float classicAlpha = mask * mix(0.20, 0.55,
			clamp(visibility, 0.0, 1.0));
		gl_FragColor = vec4(vec3(0.0), classicAlpha);
		return;
	}

	// Two differently directed wave layers avoid the appearance of a flat,
	// sliding sheet.  All temporal multipliers are integers so the phase wraps
	// seamlessly when the CPU accumulator reaches two pi.
	vec2 local = gl_TexCoord[0].xy;
	vec2 broad = vec2(
		sin(local.y * 18.849556 + time * 2.0) +
			0.45 * sin((local.x + local.y) * 12.566371 - time),
		cos(local.x * 18.849556 - time * 2.0) +
			0.40 * sin((local.x - local.y) * 12.566371 + time));
	vec2 fine = vec2(
		sin(local.y * 50.265482 - time * 3.0),
		cos(local.x * 43.982297 + time * 3.0));
	float strength = mix(2.8, 7.0, clamp(visibility, 0.0, 1.0));
	vec2 offset = (broad + fine * 0.28) * strength;
	vec2 samplePoint = clamp(gl_FragCoord.xy + offset, vec2(0.5),
		vec2(pixelWidth - 0.5, pixelHeight - 0.5));
	vec3 refracted = texture2DRect(texture2, samplePoint).rgb;

	// Retain only a restrained silhouette and moving glint.  The captured
	// background already contains the correct distance fog.
	float edge = 1.0 - smoothstep(0.08, 0.72, mask);
	float glint = (broad.x + broad.y) * 0.5;
	float presence = 0.125 + 0.085 * visibility + edge * 0.085;
	// A restrained blue-grey absorption tint keeps the moving silhouette
	// readable even against backgrounds with little texture or contrast.
	vec3 cloakTint = vec3(0.865, 0.885, 0.91);
	refracted *= (1.0 - presence) * cloakTint;
	refracted += vec3(max(glint, 0.0)) * (0.018 + 0.026 * visibility);
	gl_FragColor = vec4(clamp(refracted, 0.0, 1.0), 1.0);
}

)"

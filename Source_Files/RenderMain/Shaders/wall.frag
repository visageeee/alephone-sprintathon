R"(

uniform sampler2D texture0;
uniform sampler2DRect texture2;
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
uniform float pixelWidth;
uniform float pixelHeight;
varying vec3 viewXY;
varying vec3 viewDir;
varying float worldZ;
varying vec4 vertexColor;
varying vec3 sprintathonWorldPosition;
uniform vec4 sprintathonLightPosition;
uniform vec4 sprintathonLightColor;
uniform vec4 sprintathonLightPosition2;
uniform vec4 sprintathonLightColor2;
uniform vec4 sprintathonLightPosition3;
uniform vec4 sprintathonLightColor3;
uniform vec4 sprintathonLightPosition4;
uniform vec4 sprintathonLightColor4;
uniform vec4 sprintathonLightPosition5;
uniform vec4 sprintathonLightColor5;
uniform vec4 sprintathonLightPosition6;
uniform vec4 sprintathonLightColor6;
uniform vec4 sprintathonLightPosition7;
uniform vec4 sprintathonLightColor7;
uniform vec4 sprintathonLightPosition8;
uniform vec4 sprintathonLightColor8;
uniform vec4 sprintathonLightPosition9;
uniform vec4 sprintathonLightColor9;
uniform vec4 sprintathonLightPosition10;
uniform vec4 sprintathonLightColor10;
uniform vec4 sprintathonLightPosition11;
uniform vec4 sprintathonLightColor11;
uniform vec4 sprintathonSectorEdge0;
uniform vec4 sprintathonSectorEdge1;
uniform vec4 sprintathonSectorEdge2;
uniform vec4 sprintathonSectorEdge3;
uniform vec4 sprintathonSectorEdge4;
uniform vec4 sprintathonSectorEdge5;
uniform vec4 sprintathonSectorEdge6;
uniform vec4 sprintathonSectorEdge7;
uniform vec4 sprintathonMuzzlePosition;
uniform vec4 sprintathonMuzzleColor;
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
	vec2 sceneRefractionOffset = vec2(0.0);
	if (rippleStrength > 0.001) {
		float phase = time;
		vec2 base = texCoords.xy;
		vec2 fineWaves = vec2(
			sin(base.y * 18.849556 + phase * 3.0) + 0.55 * sin((base.x + base.y) * 12.566371 - phase * 2.0),
			cos(base.x * 18.849556 - phase * 2.0) + 0.50 * sin((base.x - base.y) * 12.566371 + phase));
		vec2 broadWaves = vec2(
			sin(base.y * 6.2831853 + phase),
			cos(base.x * 6.2831853 - phase));
		sceneRefractionOffset =
			(broadWaves * 7.5 + fineWaves * 3.0) * rippleStrength;
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
    // Only floor/ceiling draws enable the edge uniforms. Blend the regular
    // sector shade before adding colored per-pixel lights.
    if (sprintathonSectorEdge0.w >= 0.0) {
        if (sprintathonSectorEdge0.w >= 0.0) {
            float edgeDistance = dot(sprintathonWorldPosition.xy, sprintathonSectorEdge0.xy) + sprintathonSectorEdge0.z;
            float edgeBlend = 0.5 * (1.0 - smoothstep(0.0, 0.18 * 1024.0, edgeDistance));
            float neighborBase = sprintathonSectorEdge0.w > mlFactor ?
                sprintathonSectorEdge0.w + mlFactor * 0.5 : sprintathonSectorEdge0.w * 0.5 + mlFactor;
            intensity = mix(intensity, vec3(clamp(neighborBase, glow, 1.0)), edgeBlend);
        }
        if (sprintathonSectorEdge1.w >= 0.0) {
            float edgeDistance = dot(sprintathonWorldPosition.xy, sprintathonSectorEdge1.xy) + sprintathonSectorEdge1.z;
            float edgeBlend = 0.5 * (1.0 - smoothstep(0.0, 0.18 * 1024.0, edgeDistance));
            float neighborBase = sprintathonSectorEdge1.w > mlFactor ?
                sprintathonSectorEdge1.w + mlFactor * 0.5 : sprintathonSectorEdge1.w * 0.5 + mlFactor;
            intensity = mix(intensity, vec3(clamp(neighborBase, glow, 1.0)), edgeBlend);
        }
        if (sprintathonSectorEdge2.w >= 0.0) {
            float edgeDistance = dot(sprintathonWorldPosition.xy, sprintathonSectorEdge2.xy) + sprintathonSectorEdge2.z;
            float edgeBlend = 0.5 * (1.0 - smoothstep(0.0, 0.18 * 1024.0, edgeDistance));
            float neighborBase = sprintathonSectorEdge2.w > mlFactor ?
                sprintathonSectorEdge2.w + mlFactor * 0.5 : sprintathonSectorEdge2.w * 0.5 + mlFactor;
            intensity = mix(intensity, vec3(clamp(neighborBase, glow, 1.0)), edgeBlend);
        }
        if (sprintathonSectorEdge3.w >= 0.0) {
            float edgeDistance = dot(sprintathonWorldPosition.xy, sprintathonSectorEdge3.xy) + sprintathonSectorEdge3.z;
            float edgeBlend = 0.5 * (1.0 - smoothstep(0.0, 0.18 * 1024.0, edgeDistance));
            float neighborBase = sprintathonSectorEdge3.w > mlFactor ?
                sprintathonSectorEdge3.w + mlFactor * 0.5 : sprintathonSectorEdge3.w * 0.5 + mlFactor;
            intensity = mix(intensity, vec3(clamp(neighborBase, glow, 1.0)), edgeBlend);
        }
        if (sprintathonSectorEdge4.w >= 0.0) {
            float edgeDistance = dot(sprintathonWorldPosition.xy, sprintathonSectorEdge4.xy) + sprintathonSectorEdge4.z;
            float edgeBlend = 0.5 * (1.0 - smoothstep(0.0, 0.18 * 1024.0, edgeDistance));
            float neighborBase = sprintathonSectorEdge4.w > mlFactor ?
                sprintathonSectorEdge4.w + mlFactor * 0.5 : sprintathonSectorEdge4.w * 0.5 + mlFactor;
            intensity = mix(intensity, vec3(clamp(neighborBase, glow, 1.0)), edgeBlend);
        }
        if (sprintathonSectorEdge5.w >= 0.0) {
            float edgeDistance = dot(sprintathonWorldPosition.xy, sprintathonSectorEdge5.xy) + sprintathonSectorEdge5.z;
            float edgeBlend = 0.5 * (1.0 - smoothstep(0.0, 0.18 * 1024.0, edgeDistance));
            float neighborBase = sprintathonSectorEdge5.w > mlFactor ?
                sprintathonSectorEdge5.w + mlFactor * 0.5 : sprintathonSectorEdge5.w * 0.5 + mlFactor;
            intensity = mix(intensity, vec3(clamp(neighborBase, glow, 1.0)), edgeBlend);
        }
        if (sprintathonSectorEdge6.w >= 0.0) {
            float edgeDistance = dot(sprintathonWorldPosition.xy, sprintathonSectorEdge6.xy) + sprintathonSectorEdge6.z;
            float edgeBlend = 0.5 * (1.0 - smoothstep(0.0, 0.18 * 1024.0, edgeDistance));
            float neighborBase = sprintathonSectorEdge6.w > mlFactor ?
                sprintathonSectorEdge6.w + mlFactor * 0.5 : sprintathonSectorEdge6.w * 0.5 + mlFactor;
            intensity = mix(intensity, vec3(clamp(neighborBase, glow, 1.0)), edgeBlend);
        }
        if (sprintathonSectorEdge7.w >= 0.0) {
            float edgeDistance = dot(sprintathonWorldPosition.xy, sprintathonSectorEdge7.xy) + sprintathonSectorEdge7.z;
            float edgeBlend = 0.5 * (1.0 - smoothstep(0.0, 0.18 * 1024.0, edgeDistance));
            float neighborBase = sprintathonSectorEdge7.w > mlFactor ?
                sprintathonSectorEdge7.w + mlFactor * 0.5 : sprintathonSectorEdge7.w * 0.5 + mlFactor;
            intensity = mix(intensity, vec3(clamp(neighborBase, glow, 1.0)), edgeBlend);
        }
    }
    if (sprintathonMuzzleColor.a > 0.0) {
        vec3 muzzleDelta = (sprintathonWorldPosition - sprintathonMuzzlePosition.xyz) /
                           max(sprintathonMuzzlePosition.w, 1.0);
        float muzzleFalloff = max(0.0, 1.0 - dot(muzzleDelta, muzzleDelta));
        intensity = clamp(intensity + sprintathonMuzzleColor.rgb *
                          (muzzleFalloff * muzzleFalloff), glow, 1.0);
    }
    // A uniform branch skips the distance math for unlit surfaces and when the option is off.
    if (sprintathonLightColor.a > 0.0) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition.xyz) / max(sprintathonLightPosition.w, 1.0);
        float lightFalloff = max(0.0, 1.0 - dot(lightDelta, lightDelta));
        intensity = clamp(intensity + sprintathonLightColor.rgb * (lightFalloff * lightFalloff), glow, 1.0);
    }
    if (sprintathonLightColor2.a > 0.0) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition2.xyz) / max(sprintathonLightPosition2.w, 1.0);
        float lightFalloff = max(0.0, 1.0 - dot(lightDelta, lightDelta));
        intensity = clamp(intensity + sprintathonLightColor2.rgb * (lightFalloff * lightFalloff / (1.0 + 16.0 * dot(lightDelta, lightDelta))), glow, 1.0);
    }
    if (sprintathonLightColor3.a > 0.0) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition3.xyz) / max(sprintathonLightPosition3.w, 1.0);
        float lightFalloff = max(0.0, 1.0 - dot(lightDelta, lightDelta));
        intensity = clamp(intensity + sprintathonLightColor3.rgb * (lightFalloff * lightFalloff / (1.0 + 16.0 * dot(lightDelta, lightDelta))), glow, 1.0);
    }
    if (sprintathonLightColor4.a > 0.0) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition4.xyz) / max(sprintathonLightPosition4.w, 1.0);
        float lightFalloff = max(0.0, 1.0 - dot(lightDelta, lightDelta));
        intensity = clamp(intensity + sprintathonLightColor4.rgb * (lightFalloff * lightFalloff / (1.0 + 16.0 * dot(lightDelta, lightDelta))), glow, 1.0);
    }
    if (sprintathonLightColor5.a > 0.0) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition5.xyz) / max(sprintathonLightPosition5.w, 1.0);
        float lightFalloff = max(0.0, 1.0 - dot(lightDelta, lightDelta));
        intensity = clamp(intensity + sprintathonLightColor5.rgb * (lightFalloff * lightFalloff / (1.0 + 16.0 * dot(lightDelta, lightDelta))), glow, 1.0);
    }
    if (sprintathonLightColor6.a > 0.0) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition6.xyz) / max(sprintathonLightPosition6.w, 1.0);
        float lightFalloff = max(0.0, 1.0 - dot(lightDelta, lightDelta));
        intensity = clamp(intensity + sprintathonLightColor6.rgb * (lightFalloff * lightFalloff / (1.0 + 16.0 * dot(lightDelta, lightDelta))), glow, 1.0);
    }
    if (sprintathonLightColor7.a > 0.0) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition7.xyz) / max(sprintathonLightPosition7.w, 1.0);
        float lightFalloff = max(0.0, 1.0 - dot(lightDelta, lightDelta));
        intensity = clamp(intensity + sprintathonLightColor7.rgb * (lightFalloff * lightFalloff / (1.0 + 16.0 * dot(lightDelta, lightDelta))), glow, 1.0);
    }
    if (sprintathonLightColor8.a > 0.0) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition8.xyz) / max(sprintathonLightPosition8.w, 1.0);
        float lightFalloff = max(0.0, 1.0 - dot(lightDelta, lightDelta));
        intensity = clamp(intensity + sprintathonLightColor8.rgb * (lightFalloff * lightFalloff / (1.0 + 16.0 * dot(lightDelta, lightDelta))), glow, 1.0);
    }
    if (sprintathonLightColor9.a > 0.0) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition9.xyz) / max(sprintathonLightPosition9.w, 1.0);
        float lightFalloff = max(0.0, 1.0 - dot(lightDelta, lightDelta));
        intensity = clamp(intensity + sprintathonLightColor9.rgb * (lightFalloff * lightFalloff / (1.0 + 16.0 * dot(lightDelta, lightDelta))), glow, 1.0);
    }
    if (sprintathonLightColor10.a > 0.0) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition10.xyz) / max(sprintathonLightPosition10.w, 1.0);
        float lightFalloff = max(0.0, 1.0 - dot(lightDelta, lightDelta));
        intensity = clamp(intensity + sprintathonLightColor10.rgb * (lightFalloff * lightFalloff / (1.0 + 16.0 * dot(lightDelta, lightDelta))), glow, 1.0);
    }
    if (sprintathonLightColor11.a > 0.0) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition11.xyz) / max(sprintathonLightPosition11.w, 1.0);
        float lightFalloff = max(0.0, 1.0 - dot(lightDelta, lightDelta));
        intensity = clamp(intensity + sprintathonLightColor11.rgb * (lightFalloff * lightFalloff / (1.0 + 16.0 * dot(lightDelta, lightDelta))), glow, 1.0);
    }
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
	vec3 finalColor = mix(gl_Fog.color.rgb, shadedColor, fogFactor);
	if (rippleStrength > 0.001) {
		vec2 screenCenter = vec2(pixelWidth, pixelHeight) * 0.5;
		vec2 refractedCoord = screenCenter +
			(gl_FragCoord.xy - screenCenter) * 0.975 +
			sceneRefractionOffset;
		vec3 refractedScene = texture2DRect(texture2,
			refractedCoord).rgb;
		float liquidAlpha = clamp(vertexColor.a, 0.0, 1.0);
		gl_FragColor = vec4(mix(refractedScene, finalColor, liquidAlpha), 1.0);
	} else {
		gl_FragColor = vec4(finalColor, vertexColor.a * color.a);
	}
}

)"

R"(

uniform sampler2D texture0;
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

uniform vec4 projectileBlurVector;
uniform vec4 projectileBlurBounds;

vec4 projectileSpriteColor(vec2 uv) {
    if (projectileBlurVector.z <= 0.0) return texture2D(texture0, uv);
    vec3 rgb = vec3(0.0);
    float alpha = 0.0;
    float weights = 0.0;
    // Integrate the sprite along its motion instead of drawing distinct copies.
    // Alpha-weighted color avoids dark rims around transparent sprite pixels.
    float samples = clamp(projectileBlurVector.w, 16.0, 48.0);
    for (int i = 0; i < 48; ++i) {
        if (float(i) >= samples) break;
        float t = (float(i) + 0.5) / samples;
        float weight = 1.0 - t;
        vec2 point = uv + projectileBlurVector.xy * t;
        weights += weight;
        if (point.x >= projectileBlurBounds.x && point.y >= projectileBlurBounds.y &&
            point.x <= projectileBlurBounds.z && point.y <= projectileBlurBounds.w) {
            vec4 sampleColor = texture2D(texture0, point);
            rgb += sampleColor.rgb * sampleColor.a * weight;
            alpha += sampleColor.a * weight;
        }
    }
    return vec4(rgb / max(alpha, 0.00001), min(1.0, 1.4 * alpha / weights));
}

void main (void) {
	vec4 color = projectileSpriteColor(gl_TexCoord[0].xy);
	float avg = (color.r + color.g + color.b) / 3.0;
	float fogFactor = getFogFactor(length(viewDir));
	if (mediaFogEnabled > 0.0) {
		float heightFog = clamp((mediaFogTop - worldZ) / mediaFogSoftness, 0.0, 1.0);
		float heightMask = mix(1.0, heightFog, mediaFogEnabled);
		fogFactor = 1.0 - (1.0 - fogFactor) * heightMask;
	}
	gl_FragColor = vec4(mix(gl_Fog.color.rgb, vertexColor.rgb * avg, fogFactor), vertexColor.a * color.a);
}

)"

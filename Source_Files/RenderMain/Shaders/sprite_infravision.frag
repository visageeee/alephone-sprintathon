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


uniform vec4 flameRipple; // enabled, simulation time, phase, unused
uniform vec4 flameBounds;
uniform sampler2DRect texture2;
uniform float pixelWidth;
uniform float pixelHeight;
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
	vec4 color = flameRipple.x > 0.0 ? flameSpriteColor(gl_TexCoord[0].xy) : projectileSpriteColor(gl_TexCoord[0].xy);
	float avg = (color.r + color.g + color.b) / 3.0;
	float fogFactor = getFogFactor(length(viewDir));
	if (mediaFogEnabled > 0.0) {
		float heightFog = clamp((mediaFogTop - worldZ) / mediaFogSoftness, 0.0, 1.0);
		float heightMask = mix(1.0, heightFog, mediaFogEnabled);
		fogFactor = 1.0 - (1.0 - fogFactor) * heightMask;
	}
	gl_FragColor = vec4(mix(gl_Fog.color.rgb, vertexColor.rgb * avg, fogFactor), vertexColor.a * color.a);
    if (flameRipple.w > 0.0 && color.a > 0.001) {
        vec2 p = (gl_TexCoord[0].xy-flameBounds.xy) /
            max(flameBounds.zw-flameBounds.xy, vec2(0.00001));
        float t = flameRipple.y*7.0 + flameRipple.z;
        vec2 ripple = vec2(sin(p.x*23.0-t) + 0.4*sin(p.y*31.0+t*1.3),
                           cos(p.y*19.0-t*0.8));
        vec2 sampleAt = clamp(gl_FragCoord.xy + ripple*3.5*(pixelHeight/720.0),
                             vec2(0.5), vec2(pixelWidth,pixelHeight)-vec2(0.5));
        vec3 behind = texture2DRect(texture2, sampleAt).rgb;
        float coverage = smoothstep(0.0,0.22,color.a);
        gl_FragColor.rgb = mix(behind,gl_FragColor.rgb,gl_FragColor.a);
        gl_FragColor.a = coverage;
    }

}

)"

R"(

uniform sampler2D texture0;
uniform sampler2D skyBakeMap;
uniform vec4 skyBakeLayers;
vec2 sampleSkyBake(vec2 uv) {
    if(skyBakeLayers.w>=0.999)return texture2D(skyBakeMap,uv).rg;
    vec2 low=texture2D(skyBakeMap,vec2(uv.x,(uv.y+skyBakeLayers.x)*skyBakeLayers.w)).rg;
    vec2 high=texture2D(skyBakeMap,vec2(uv.x,(uv.y+skyBakeLayers.y)*skyBakeLayers.w)).rg;
    return mix(low,high,skyBakeLayers.z);
}
uniform vec4 skyBakeSun;
uniform vec4 skyBakeAmbient;
uniform float skyBakeEnabled;
uniform float skyBakeSectorStrength;
uniform vec4 liquidSpecLight, liquidSpecEye;
uniform sampler2D liquidSkyMap;
uniform vec4 liquidSkyParams;
uniform float liquidSkyMode;
uniform vec4 liquidFlow;
uniform vec4 waterSceneryPosition0,waterSceneryColor0;
uniform vec4 waterSceneryPosition1,waterSceneryColor1;
uniform float liquidSceneAvailable;
uniform vec4 skyBakeU, skyBakeV;
uniform sampler2D texture1;
uniform sampler2DRect texture2;
uniform float pulsate;
uniform float wobble;
uniform float glow;
uniform float sprintathonDistantSurfaceDetail;
uniform float flare;
uniform float selfLuminosity;
uniform float fogMode;
uniform float sprintathonShaftSource;
uniform float sprintathonSurfaceLightCount;
uniform vec4 sprintathonLiquidLightSettings;
uniform sampler2DRect texture3;
uniform float sprintathonAllSceneryCount;
uniform float sprintathonSceneryFirst;
uniform float mediaFogEnabled;
uniform float mediaFogTop;
uniform float mediaFogSoftness;
uniform float mediaRipple;
uniform vec4 liquidEdge0;
uniform vec4 liquidEdge1;
uniform vec4 liquidEdge2;
uniform vec4 liquidEdge3;
uniform vec4 liquidEdge4;
uniform vec4 liquidEdge5;
uniform vec4 liquidEdge6;
uniform vec4 liquidEdge7;
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
uniform vec4 sprintathonLightPosition12;
uniform vec4 sprintathonLightColor12;
uniform vec4 sprintathonLightPosition13;
uniform vec4 sprintathonLightColor13;
uniform vec4 sprintathonLightPosition14;
uniform vec4 sprintathonLightColor14;
uniform vec4 sprintathonLightPosition15;
uniform vec4 sprintathonLightColor15;
uniform vec4 sprintathonLightPosition16;
uniform vec4 sprintathonLightColor16;
uniform vec4 sprintathonLightPosition17;
uniform vec4 sprintathonLightColor17;
uniform vec4 sprintathonLightPosition18;
uniform vec4 sprintathonLightColor18;
uniform vec4 sprintathonLightPosition19;
uniform vec4 sprintathonLightColor19;
uniform vec4 sprintathonLightPosition20;
uniform vec4 sprintathonLightColor20;
uniform vec4 sprintathonLightPosition21;
uniform vec4 sprintathonLightColor21;
uniform vec4 sprintathonConePool0;
uniform vec4 sprintathonConeColor0;
uniform vec4 sprintathonConePool1;
uniform vec4 sprintathonConeColor1;
uniform vec4 sprintathonConePool2;
uniform vec4 sprintathonConeColor2;
uniform vec4 sprintathonConePool3;
uniform vec4 sprintathonConeColor3;
uniform float sprintathonSectorBlendWidth;
uniform vec4 sprintathonWallBlendAxis;
uniform vec4 sprintathonSectorEdge0;
uniform vec4 sprintathonSectorEdge1;
uniform vec4 sprintathonSectorEdge2;
uniform vec4 sprintathonSectorEdge3;
uniform vec4 sprintathonSectorEdge4;
uniform vec4 sprintathonSectorEdge5;
uniform vec4 sprintathonSectorEdge6;
uniform vec4 sprintathonSectorEdge7;
uniform vec4 sprintathonSectorSpan0;
uniform vec4 sprintathonSectorSpan1;
uniform vec4 sprintathonSectorSpan2;
uniform vec4 sprintathonSectorSpan3;
uniform vec4 sprintathonSectorSpan4;
uniform vec4 sprintathonSectorSpan5;
uniform vec4 sprintathonSectorSpan6;
uniform vec4 sprintathonSectorSpan7;
uniform vec4 sprintathonMuzzlePosition;
uniform vec4 sprintathonMuzzleColor;

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

// Accumulate coloured caustic light separately from texture-multiplied shading.
vec3 sprintathonCausticRadiance;
float sprintathonLiquidCaustic(float tag, vec3 sourcePosition) {
    float phase = tag - (tag >= 10.0 ? 10.0 : 2.0);
    vec3 relativePosition = sprintathonWorldPosition - sourcePosition;
    float heightAboveLiquid = max(relativePosition.z / 1024.0, 0.0);
    float patternScale = mix(0.60, 1.80,
        smoothstep(0.0, 4.0, heightAboveLiquid));
    vec3 p = relativePosition / (768.0 * patternScale);
    vec2 q = p.xy + p.z * vec2(0.37, -0.29);
    // Two crossing waves instead of four nested sine evaluations.
    float a = sin(q.x * 5.1 + q.y * 2.7 + phase * 2.0);
    float b = sin(q.x * -3.2 + q.y * 5.8 - phase);
    float ridge = 1.0 - smoothstep(0.06, 0.38, abs(a + b));
    // Lava is intrinsically emissive; balance its caustics against shaded liquids.
    float sourceGain = tag >= 10.0 ? 0.5 : 1.0;
    return sourceGain * 0.85 * ridge * ridge;
}

void sprintathonApplySurfaceLights(inout vec3 intensity) {
    sprintathonCausticRadiance = vec3(0.0);
    if (sprintathonSurfaceLightCount <= 0.0) return;
    if (sprintathonLightColor2.a > 0.0 && any(lessThan(intensity, vec3(1.0)))) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition2.xyz) * sprintathonLightPosition2.w;
        float lightDistanceSquared = dot(lightDelta, lightDelta);
        if (lightDistanceSquared < 1.0) {
            float lightFalloff = 1.0 - lightDistanceSquared;
            float attenuation = lightFalloff * lightFalloff / (1.0 + 16.0 * lightDistanceSquared);
            vec3 contribution = sprintathonLightColor2.rgb * attenuation;
            float steadyGain = 1.0;
            if (sprintathonLightColor2.a >= 2.0) {
                steadyGain = sprintathonLightColor2.a >= 10.0 ? 1.0 : sprintathonLiquidLightSettings.x;
                float peak = max(contribution.r, max(contribution.g, contribution.b)) * sprintathonLiquidLightSettings.y;
                if (peak > 0.002) {
                    float visibility = smoothstep(0.002, 0.01, peak);
                    sprintathonCausticRadiance += contribution * sprintathonLiquidLightSettings.y * visibility *
                        sprintathonLiquidCaustic(sprintathonLightColor2.a, sprintathonLightPosition2.xyz);
                }
            }
            intensity = clamp(intensity + contribution * steadyGain, glow, 1.0);
        }
    }
    if (sprintathonLightColor3.a > 0.0 && any(lessThan(intensity, vec3(1.0)))) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition3.xyz) * sprintathonLightPosition3.w;
        float lightDistanceSquared = dot(lightDelta, lightDelta);
        if (lightDistanceSquared < 1.0) {
            float lightFalloff = 1.0 - lightDistanceSquared;
            float attenuation = lightFalloff * lightFalloff / (1.0 + 16.0 * lightDistanceSquared);
            vec3 contribution = sprintathonLightColor3.rgb * attenuation;
            float steadyGain = 1.0;
            if (sprintathonLightColor3.a >= 2.0) {
                steadyGain = sprintathonLightColor3.a >= 10.0 ? 1.0 : sprintathonLiquidLightSettings.x;
                float peak = max(contribution.r, max(contribution.g, contribution.b)) * sprintathonLiquidLightSettings.y;
                if (peak > 0.002) {
                    float visibility = smoothstep(0.002, 0.01, peak);
                    sprintathonCausticRadiance += contribution * sprintathonLiquidLightSettings.y * visibility *
                        sprintathonLiquidCaustic(sprintathonLightColor3.a, sprintathonLightPosition3.xyz);
                }
            }
            intensity = clamp(intensity + contribution * steadyGain, glow, 1.0);
        }
    }
    if (sprintathonLightColor4.a > 0.0 && any(lessThan(intensity, vec3(1.0)))) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition4.xyz) * sprintathonLightPosition4.w;
        float lightDistanceSquared = dot(lightDelta, lightDelta);
        if (lightDistanceSquared < 1.0) {
            float lightFalloff = 1.0 - lightDistanceSquared;
            float attenuation = lightFalloff * lightFalloff / (1.0 + 16.0 * lightDistanceSquared);
            vec3 contribution = sprintathonLightColor4.rgb * attenuation;
            float steadyGain = 1.0;
            if (sprintathonLightColor4.a >= 2.0) {
                steadyGain = sprintathonLightColor4.a >= 10.0 ? 1.0 : sprintathonLiquidLightSettings.x;
                float peak = max(contribution.r, max(contribution.g, contribution.b)) * sprintathonLiquidLightSettings.y;
                if (peak > 0.002) {
                    float visibility = smoothstep(0.002, 0.01, peak);
                    sprintathonCausticRadiance += contribution * sprintathonLiquidLightSettings.y * visibility *
                        sprintathonLiquidCaustic(sprintathonLightColor4.a, sprintathonLightPosition4.xyz);
                }
            }
            intensity = clamp(intensity + contribution * steadyGain, glow, 1.0);
        }
    }
    if (sprintathonLightColor5.a > 0.0 && any(lessThan(intensity, vec3(1.0)))) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition5.xyz) * sprintathonLightPosition5.w;
        float lightDistanceSquared = dot(lightDelta, lightDelta);
        if (lightDistanceSquared < 1.0) {
            float lightFalloff = 1.0 - lightDistanceSquared;
            float attenuation = lightFalloff * lightFalloff / (1.0 + 16.0 * lightDistanceSquared);
            vec3 contribution = sprintathonLightColor5.rgb * attenuation;
            float steadyGain = 1.0;
            if (sprintathonLightColor5.a >= 2.0) {
                steadyGain = sprintathonLightColor5.a >= 10.0 ? 1.0 : sprintathonLiquidLightSettings.x;
                float peak = max(contribution.r, max(contribution.g, contribution.b)) * sprintathonLiquidLightSettings.y;
                if (peak > 0.002) {
                    float visibility = smoothstep(0.002, 0.01, peak);
                    sprintathonCausticRadiance += contribution * sprintathonLiquidLightSettings.y * visibility *
                        sprintathonLiquidCaustic(sprintathonLightColor5.a, sprintathonLightPosition5.xyz);
                }
            }
            intensity = clamp(intensity + contribution * steadyGain, glow, 1.0);
        }
    }
    if (sprintathonSurfaceLightCount <= 4.0) return;
    if (sprintathonLightColor6.a > 0.0 && any(lessThan(intensity, vec3(1.0)))) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition6.xyz) * sprintathonLightPosition6.w;
        float lightDistanceSquared = dot(lightDelta, lightDelta);
        if (lightDistanceSquared < 1.0) {
            float lightFalloff = 1.0 - lightDistanceSquared;
            float attenuation = lightFalloff * lightFalloff / (1.0 + 16.0 * lightDistanceSquared);
            vec3 contribution = sprintathonLightColor6.rgb * attenuation;
            float steadyGain = 1.0;
            if (sprintathonLightColor6.a >= 2.0) {
                steadyGain = sprintathonLightColor6.a >= 10.0 ? 1.0 : sprintathonLiquidLightSettings.x;
                float peak = max(contribution.r, max(contribution.g, contribution.b)) * sprintathonLiquidLightSettings.y;
                if (peak > 0.002) {
                    float visibility = smoothstep(0.002, 0.01, peak);
                    sprintathonCausticRadiance += contribution * sprintathonLiquidLightSettings.y * visibility *
                        sprintathonLiquidCaustic(sprintathonLightColor6.a, sprintathonLightPosition6.xyz);
                }
            }
            intensity = clamp(intensity + contribution * steadyGain, glow, 1.0);
        }
    }
    if (sprintathonLightColor7.a > 0.0 && any(lessThan(intensity, vec3(1.0)))) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition7.xyz) * sprintathonLightPosition7.w;
        float lightDistanceSquared = dot(lightDelta, lightDelta);
        if (lightDistanceSquared < 1.0) {
)"
R"(            float lightFalloff = 1.0 - lightDistanceSquared;
)"
R"(            float attenuation = lightFalloff * lightFalloff / (1.0 + 16.0 * lightDistanceSquared);
            vec3 contribution = sprintathonLightColor7.rgb * attenuation;
            float steadyGain = 1.0;
            if (sprintathonLightColor7.a >= 2.0) {
                steadyGain = sprintathonLightColor7.a >= 10.0 ? 1.0 : sprintathonLiquidLightSettings.x;
                float peak = max(contribution.r, max(contribution.g, contribution.b)) * sprintathonLiquidLightSettings.y;
                if (peak > 0.002) {
                    float visibility = smoothstep(0.002, 0.01, peak);
                    sprintathonCausticRadiance += contribution * sprintathonLiquidLightSettings.y * visibility *
                        sprintathonLiquidCaustic(sprintathonLightColor7.a, sprintathonLightPosition7.xyz);
                }
            }
            intensity = clamp(intensity + contribution * steadyGain, glow, 1.0);
        }
    }
    if (sprintathonLightColor8.a > 0.0 && any(lessThan(intensity, vec3(1.0)))) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition8.xyz) * sprintathonLightPosition8.w;
        float lightDistanceSquared = dot(lightDelta, lightDelta);
        if (lightDistanceSquared < 1.0) {
            float lightFalloff = 1.0 - lightDistanceSquared;
            float attenuation = lightFalloff * lightFalloff / (1.0 + 16.0 * lightDistanceSquared);
            vec3 contribution = sprintathonLightColor8.rgb * attenuation;
            float steadyGain = 1.0;
            if (sprintathonLightColor8.a >= 2.0) {
                steadyGain = sprintathonLightColor8.a >= 10.0 ? 1.0 : sprintathonLiquidLightSettings.x;
                float peak = max(contribution.r, max(contribution.g, contribution.b)) * sprintathonLiquidLightSettings.y;
                if (peak > 0.002) {
                    float visibility = smoothstep(0.002, 0.01, peak);
                    sprintathonCausticRadiance += contribution * sprintathonLiquidLightSettings.y * visibility *
                        sprintathonLiquidCaustic(sprintathonLightColor8.a, sprintathonLightPosition8.xyz);
                }
            }
            intensity = clamp(intensity + contribution * steadyGain, glow, 1.0);
        }
    }

    if (sprintathonLightColor9.a > 0.0 && any(lessThan(intensity, vec3(1.0)))) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition9.xyz) * sprintathonLightPosition9.w;
        float lightDistanceSquared = dot(lightDelta, lightDelta);
        if (lightDistanceSquared < 1.0) {
            float lightFalloff = 1.0 - lightDistanceSquared;
)"
R"(            float attenuation = lightFalloff * lightFalloff / (1.0 + 16.0 * lightDistanceSquared);
            vec3 contribution = sprintathonLightColor9.rgb * attenuation;
            float steadyGain = 1.0;
            if (sprintathonLightColor9.a >= 2.0) {
                steadyGain = sprintathonLightColor9.a >= 10.0 ? 1.0 : sprintathonLiquidLightSettings.x;
                float peak = max(contribution.r, max(contribution.g, contribution.b)) * sprintathonLiquidLightSettings.y;
                if (peak > 0.002) {
                    float visibility = smoothstep(0.002, 0.01, peak);
                    sprintathonCausticRadiance += contribution * sprintathonLiquidLightSettings.y * visibility *
                        sprintathonLiquidCaustic(sprintathonLightColor9.a, sprintathonLightPosition9.xyz);
                }
            }
            intensity = clamp(intensity + contribution * steadyGain, glow, 1.0);
        }
    }
    if (sprintathonSurfaceLightCount <= 8.0) return;
    if (sprintathonLightColor10.a > 0.0 && any(lessThan(intensity, vec3(1.0)))) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition10.xyz) * sprintathonLightPosition10.w;
        float lightDistanceSquared = dot(lightDelta, lightDelta);
        if (lightDistanceSquared < 1.0) {
            float lightFalloff = 1.0 - lightDistanceSquared;
            float attenuation = lightFalloff * lightFalloff / (1.0 + 16.0 * lightDistanceSquared);
            vec3 contribution = sprintathonLightColor10.rgb * attenuation;
            float steadyGain = 1.0;
            if (sprintathonLightColor10.a >= 2.0) {
                steadyGain = sprintathonLightColor10.a >= 10.0 ? 1.0 : sprintathonLiquidLightSettings.x;
                float peak = max(contribution.r, max(contribution.g, contribution.b)) * sprintathonLiquidLightSettings.y;
                if (peak > 0.002) {
                    float visibility = smoothstep(0.002, 0.01, peak);
                    sprintathonCausticRadiance += contribution * sprintathonLiquidLightSettings.y * visibility *
                        sprintathonLiquidCaustic(sprintathonLightColor10.a, sprintathonLightPosition10.xyz);
                }
            }
            intensity = clamp(intensity + contribution * steadyGain, glow, 1.0);
        }
    }
    if (sprintathonLightColor11.a > 0.0 && any(lessThan(intensity, vec3(1.0)))) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition11.xyz) * sprintathonLightPosition11.w;
        float lightDistanceSquared = dot(lightDelta, lightDelta);
        if (lightDistanceSquared < 1.0) {
            float lightFalloff = 1.0 - lightDistanceSquared;
            float attenuation = lightFalloff * lightFalloff / (1.0 + 16.0 * lightDistanceSquared);
            vec3 contribution = sprintathonLightColor11.rgb * attenuation;
            float steadyGain = 1.0;
            if (sprintathonLightColor11.a >= 2.0) {
                steadyGain = sprintathonLightColor11.a >= 10.0 ? 1.0 : sprintathonLiquidLightSettings.x;
                float peak = max(contribution.r, max(contribution.g, contribution.b)) * sprintathonLiquidLightSettings.y;
                if (peak > 0.002) {
                    float visibility = smoothstep(0.002, 0.01, peak);
                    sprintathonCausticRadiance += contribution * sprintathonLiquidLightSettings.y * visibility *
                        sprintathonLiquidCaustic(sprintathonLightColor11.a, sprintathonLightPosition11.xyz);
                }
            }
            intensity = clamp(intensity + contribution * steadyGain, glow, 1.0);
        }
    }
    if (sprintathonLightColor12.a > 0.0 && any(lessThan(intensity, vec3(1.0)))) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition12.xyz) * sprintathonLightPosition12.w;
        float lightDistanceSquared = dot(lightDelta, lightDelta);
        if (lightDistanceSquared < 1.0) {
            float lightFalloff = 1.0 - lightDistanceSquared;
            float attenuation = lightFalloff * lightFalloff / (1.0 + 16.0 * lightDistanceSquared);
            vec3 contribution = sprintathonLightColor12.rgb * attenuation;
            float steadyGain = 1.0;
            if (sprintathonLightColor12.a >= 2.0) {
                steadyGain = sprintathonLightColor12.a >= 10.0 ? 1.0 : sprintathonLiquidLightSettings.x;
                float peak = max(contribution.r, max(contribution.g, contribution.b)) * sprintathonLiquidLightSettings.y;
                if (peak > 0.002) {
                    float visibility = smoothstep(0.002, 0.01, peak);
                    sprintathonCausticRadiance += contribution * sprintathonLiquidLightSettings.y * visibility *
                        sprintathonLiquidCaustic(sprintathonLightColor12.a, sprintathonLightPosition12.xyz);
                }
            }
            intensity = clamp(intensity + contribution * steadyGain, glow, 1.0);
        }
    }
    if (sprintathonLightColor13.a > 0.0 && any(lessThan(intensity, vec3(1.0)))) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition13.xyz) * sprintathonLightPosition13.w;
        float lightDistanceSquared = dot(lightDelta, lightDelta);
        if (lightDistanceSquared < 1.0) {
            float lightFalloff = 1.0 - lightDistanceSquared;
            float attenuation = lightFalloff * lightFalloff / (1.0 + 16.0 * lightDistanceSquared);
            vec3 contribution = sprintathonLightColor13.rgb * attenuation;
            float steadyGain = 1.0;
            if (sprintathonLightColor13.a >= 2.0) {
                steadyGain = sprintathonLightColor13.a >= 10.0 ? 1.0 : sprintathonLiquidLightSettings.x;
                float peak = max(contribution.r, max(contribution.g, contribution.b)) * sprintathonLiquidLightSettings.y;
                if (peak > 0.002) {
                    float visibility = smoothstep(0.002, 0.01, peak);
                    sprintathonCausticRadiance += contribution * sprintathonLiquidLightSettings.y * visibility *
                        sprintathonLiquidCaustic(sprintathonLightColor13.a, sprintathonLightPosition13.xyz);
                }
            }
            intensity = clamp(intensity + contribution * steadyGain, glow, 1.0);
        }
    }
    if (sprintathonSurfaceLightCount <= 12.0) return;
    if (sprintathonLightColor14.a > 0.0 && any(lessThan(intensity, vec3(1.0)))) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition14.xyz) * sprintathonLightPosition14.w;
        float lightDistanceSquared = dot(lightDelta, lightDelta);
        if (lightDistanceSquared < 1.0) {
            float lightFalloff = 1.0 - lightDistanceSquared;
            float attenuation = lightFalloff * lightFalloff / (1.0 + 16.0 * lightDistanceSquared);
            vec3 contribution = sprintathonLightColor14.rgb * attenuation;
            float steadyGain = 1.0;
            if (sprintathonLightColor14.a >= 2.0) {
                steadyGain = sprintathonLightColor14.a >= 10.0 ? 1.0 : sprintathonLiquidLightSettings.x;
                float peak = max(contribution.r, max(contribution.g, contribution.b)) * sprintathonLiquidLightSettings.y;
                if (peak > 0.002) {
                    float visibility = smoothstep(0.002, 0.01, peak);
                    sprintathonCausticRadiance += contribution * sprintathonLiquidLightSettings.y * visibility *
                        sprintathonLiquidCaustic(sprintathonLightColor14.a, sprintathonLightPosition14.xyz);
                }
            }
            intensity = clamp(intensity + contribution * steadyGain, glow, 1.0);
        }
    }
    if (sprintathonLightColor15.a > 0.0 && any(lessThan(intensity, vec3(1.0)))) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition15.xyz) * sprintathonLightPosition15.w;
        float lightDistanceSquared = dot(lightDelta, lightDelta);
        if (lightDistanceSquared < 1.0) {
            float lightFalloff = 1.0 - lightDistanceSquared;
            float attenuation = lightFalloff * lightFalloff / (1.0 + 16.0 * lightDistanceSquared);
            vec3 contribution = sprintathonLightColor15.rgb * attenuation;
            float steadyGain = 1.0;
            if (sprintathonLightColor15.a >= 2.0) {
                steadyGain = sprintathonLightColor15.a >= 10.0 ? 1.0 : sprintathonLiquidLightSettings.x;
                float peak = max(contribution.r, max(contribution.g, contribution.b)) * sprintathonLiquidLightSettings.y;
                if (peak > 0.002) {
                    float visibility = smoothstep(0.002, 0.01, peak);
                    sprintathonCausticRadiance += contribution * sprintathonLiquidLightSettings.y * visibility *
                        sprintathonLiquidCaustic(sprintathonLightColor15.a, sprintathonLightPosition15.xyz);
                }
            }
            intensity = clamp(intensity + contribution * steadyGain, glow, 1.0);
        }
    }
    if (sprintathonLightColor16.a > 0.0 && any(lessThan(intensity, vec3(1.0)))) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition16.xyz) * sprintathonLightPosition16.w;
        float lightDistanceSquared = dot(lightDelta, lightDelta);
        if (lightDistanceSquared < 1.0) {
            float lightFalloff = 1.0 - lightDistanceSquared;
            float attenuation = lightFalloff * lightFalloff / (1.0 + 16.0 * lightDistanceSquared);
            vec3 contribution = sprintathonLightColor16.rgb * attenuation;
            float steadyGain = 1.0;
            if (sprintathonLightColor16.a >= 2.0) {
                steadyGain = sprintathonLightColor16.a >= 10.0 ? 1.0 : sprintathonLiquidLightSettings.x;
                float peak = max(contribution.r, max(contribution.g, contribution.b)) * sprintathonLiquidLightSettings.y;
                if (peak > 0.002) {
                    float visibility = smoothstep(0.002, 0.01, peak);
                    sprintathonCausticRadiance += contribution * sprintathonLiquidLightSettings.y * visibility *
                        sprintathonLiquidCaustic(sprintathonLightColor16.a, sprintathonLightPosition16.xyz);
                }
            }
            intensity = clamp(intensity + contribution * steadyGain, glow, 1.0);
        }
    }
    if (sprintathonLightColor17.a > 0.0 && any(lessThan(intensity, vec3(1.0)))) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition17.xyz) * sprintathonLightPosition17.w;
        float lightDistanceSquared = dot(lightDelta, lightDelta);
        if (lightDistanceSquared < 1.0) {
            float lightFalloff = 1.0 - lightDistanceSquared;
            float attenuation = lightFalloff * lightFalloff / (1.0 + 16.0 * lightDistanceSquared);
            vec3 contribution = sprintathonLightColor17.rgb * attenuation;
            float steadyGain = 1.0;
            if (sprintathonLightColor17.a >= 2.0) {
                steadyGain = sprintathonLightColor17.a >= 10.0 ? 1.0 : sprintathonLiquidLightSettings.x;
                float peak = max(contribution.r, max(contribution.g, contribution.b)) * sprintathonLiquidLightSettings.y;
                if (peak > 0.002) {
                    float visibility = smoothstep(0.002, 0.01, peak);
                    sprintathonCausticRadiance += contribution * sprintathonLiquidLightSettings.y * visibility *
                        sprintathonLiquidCaustic(sprintathonLightColor17.a, sprintathonLightPosition17.xyz);
                }
            }
            intensity = clamp(intensity + contribution * steadyGain, glow, 1.0);
        }
    }
    if (sprintathonSurfaceLightCount <= 16.0) return;
    if (sprintathonLightColor18.a > 0.0 && any(lessThan(intensity, vec3(1.0)))) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition18.xyz) * sprintathonLightPosition18.w;
        float lightDistanceSquared = dot(lightDelta, lightDelta);
        if (lightDistanceSquared < 1.0) {
            float lightFalloff = 1.0 - lightDistanceSquared;
)"
R"(            float attenuation = lightFalloff * lightFalloff / (1.0 + 16.0 * lightDistanceSquared);
            vec3 contribution = sprintathonLightColor18.rgb * attenuation;
            float steadyGain = 1.0;
            if (sprintathonLightColor18.a >= 2.0) {
                steadyGain = sprintathonLightColor18.a >= 10.0 ? 1.0 : sprintathonLiquidLightSettings.x;
                float peak = max(contribution.r, max(contribution.g, contribution.b)) * sprintathonLiquidLightSettings.y;
                if (peak > 0.002) {
                    float visibility = smoothstep(0.002, 0.01, peak);
                    sprintathonCausticRadiance += contribution * sprintathonLiquidLightSettings.y * visibility *
                        sprintathonLiquidCaustic(sprintathonLightColor18.a, sprintathonLightPosition18.xyz);
                }
            }
            intensity = clamp(intensity + contribution * steadyGain, glow, 1.0);
        }
    }
    if (sprintathonLightColor19.a > 0.0 && any(lessThan(intensity, vec3(1.0)))) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition19.xyz) * sprintathonLightPosition19.w;
        float lightDistanceSquared = dot(lightDelta, lightDelta);
        if (lightDistanceSquared < 1.0) {
            float lightFalloff = 1.0 - lightDistanceSquared;
            float attenuation = lightFalloff * lightFalloff / (1.0 + 16.0 * lightDistanceSquared);
            vec3 contribution = sprintathonLightColor19.rgb * attenuation;
            float steadyGain = 1.0;
            if (sprintathonLightColor19.a >= 2.0) {
                steadyGain = sprintathonLightColor19.a >= 10.0 ? 1.0 : sprintathonLiquidLightSettings.x;
                float peak = max(contribution.r, max(contribution.g, contribution.b)) * sprintathonLiquidLightSettings.y;
                if (peak > 0.002) {
                    float visibility = smoothstep(0.002, 0.01, peak);
                    sprintathonCausticRadiance += contribution * sprintathonLiquidLightSettings.y * visibility *
                        sprintathonLiquidCaustic(sprintathonLightColor19.a, sprintathonLightPosition19.xyz);
                }
            }
            intensity = clamp(intensity + contribution * steadyGain, glow, 1.0);
        }
    }
    if (sprintathonLightColor20.a > 0.0 && any(lessThan(intensity, vec3(1.0)))) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition20.xyz) * sprintathonLightPosition20.w;
        float lightDistanceSquared = dot(lightDelta, lightDelta);
        if (lightDistanceSquared < 1.0) {
            float lightFalloff = 1.0 - lightDistanceSquared;
            float attenuation = lightFalloff * lightFalloff / (1.0 + 16.0 * lightDistanceSquared);
            vec3 contribution = sprintathonLightColor20.rgb * attenuation;
            float steadyGain = 1.0;
            if (sprintathonLightColor20.a >= 2.0) {
                steadyGain = sprintathonLightColor20.a >= 10.0 ? 1.0 : sprintathonLiquidLightSettings.x;
                float peak = max(contribution.r, max(contribution.g, contribution.b)) * sprintathonLiquidLightSettings.y;
                if (peak > 0.002) {
                    float visibility = smoothstep(0.002, 0.01, peak);
                    sprintathonCausticRadiance += contribution * sprintathonLiquidLightSettings.y * visibility *
                        sprintathonLiquidCaustic(sprintathonLightColor20.a, sprintathonLightPosition20.xyz);
                }
            }
            intensity = clamp(intensity + contribution * steadyGain, glow, 1.0);
        }
    }
    if (sprintathonLightColor21.a > 0.0 && any(lessThan(intensity, vec3(1.0)))) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition21.xyz) * sprintathonLightPosition21.w;
        float lightDistanceSquared = dot(lightDelta, lightDelta);
        if (lightDistanceSquared < 1.0) {
            float lightFalloff = 1.0 - lightDistanceSquared;
            float attenuation = lightFalloff * lightFalloff / (1.0 + 16.0 * lightDistanceSquared);
            vec3 contribution = sprintathonLightColor21.rgb * attenuation;
            float steadyGain = 1.0;
            if (sprintathonLightColor21.a >= 2.0) {
                steadyGain = sprintathonLightColor21.a >= 10.0 ? 1.0 : sprintathonLiquidLightSettings.x;
                float peak = max(contribution.r, max(contribution.g, contribution.b)) * sprintathonLiquidLightSettings.y;
                if (peak > 0.002) {
                    float visibility = smoothstep(0.002, 0.01, peak);
                    sprintathonCausticRadiance += contribution * sprintathonLiquidLightSettings.y * visibility *
                        sprintathonLiquidCaustic(sprintathonLightColor21.a, sprintathonLightPosition21.xyz);
                }
            }
            intensity = clamp(intensity + contribution * steadyGain, glow, 1.0);
        }
    }
}

void sprintathonApplyAllScenery(inout vec3 intensity) {
    for (float i = sprintathonSceneryFirst; i < sprintathonAllSceneryCount; i += 1.0) {
        if (all(greaterThanEqual(intensity, vec3(1.0)))) break;
        vec2 coordinate = vec2(mod(i, 128.0) * 2.0 + 0.5, floor(i / 128.0) + 0.5);
        vec4 position = texture2DRect(texture3, coordinate);
        vec3 delta = (sprintathonWorldPosition - position.xyz) * position.w;
        float distanceSquared = dot(delta, delta);
        if (distanceSquared < 1.0) {
            vec3 color = texture2DRect(texture3, coordinate + vec2(1.0, 0.0)).rgb;
            float falloff = 1.0 - distanceSquared;
            intensity = clamp(intensity + color *
                (falloff * falloff / (1.0 + 16.0 * distanceSquared)), glow, 1.0);
        }
    }
}

)"
R"(
// Procedural flowing normals: layered wave gradients, local wave evolution,
// and specular filtering. Independently implemented for the liquid renderer.

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

// Texture-based surface normals. RG and BA contain independent seamless
// slope fields. Rotated scales and balanced motion hide their repeat pattern.
// Reuse the existing world-space coloured-light uniforms. Liquid emitters
// are excluded so their fluorescence does not reflect back into itself.
vec3 sprintathonWaterLight(vec4 source,vec4 colour,vec3 normal,vec3 eye,float alpha2) {
    // Negative alpha encodes an extended scenery source radius.
    bool areaSource=colour.a<0.0;
    if(colour.a==0.0 || colour.a>=2.0 || source.w<=0.0)return vec3(0.0);
    vec3 delta=source.xyz-sprintathonWorldPosition;
    float distance2=dot(delta,delta);
    float range2=distance2*source.w*source.w;
    if(delta.z<=0.0 || range2>=1.0 || distance2<0.001)return vec3(0.0);
    if(areaSource) {
        // Closest point on a spherical fixture approximation to the reflected
        // view ray. Source size broadens the highlight without roughening water.
        vec3 ray=reflect(-eye,normal);
        vec3 offset=ray*max(dot(delta,ray),0.0)-delta;
        delta+=offset*min((-colour.a)/max(length(offset),0.001),1.0);
    }
    vec3 light=delta*inversesqrt(max(dot(delta,delta),0.001));
    vec3 halfway=normalize(eye+light);
    float nl=max(dot(normal,light),0.0);
    float nv=max(dot(normal,eye),0.001);
    float nh=max(dot(normal,halfway),0.0);
    float nh2=nh*nh;
    // A modest source width prevents point-like pinpricks at close range.
    float width=max(alpha2,0.0025);
    float denominator=nh2*(width-1.0)+1.0;
    float distribution=width/(3.14159265*denominator*denominator);
    float k=sqrt(width)*0.5;
    float geometry=nl/(4.0*(nv*(1.0-k)+k)*(nl*(1.0-k)+k));
    float f=1.0-clamp(dot(eye,halfway),0.0,1.0);
    float fresnel=0.02+0.98*f*f*f*f*f;
    float falloff=(1.0-range2)*(1.0-range2)/(1.0+(areaSource ? 2.0 : 16.0)*range2);
    return max(colour.rgb,vec3(0.0))*(distribution*geometry*fresnel*falloff);
}

vec2 sprintathonFlowingSlopes(vec2 position,float phase,float distanceToEye) {
    vec2 uv=position*(0.24/1024.0);
    // Match the visible ripple cadence more closely; phase already carries
    // the per-liquid speed and bullet-time scaling. Keep it continuous.
    float t=phase*0.032;
    mat2 rotate=mat2(0.8,0.6,-0.6,0.8);
    mat2 back=mat2(0.8,-0.6,0.6,0.8);
    vec2 a=texture2D(liquidSkyMap,uv+vec2(t,t*0.37)).rg*2.0-1.0;
    vec2 b=texture2D(liquidSkyMap,rotate*uv*1.61+vec2(-t*0.83,t*0.29)+vec2(0.31,0.67)).ba*2.0-1.0;
    vec2 slopes=a*0.19+(back*b)*0.13;
    // Fade each fine scale to zero before skipping its texture fetch. World
    // distance, rather than polygon size, keeps adjacent liquid faces seamless.
    float smallWeight=1.0-smoothstep(12.0*1024.0,24.0*1024.0,distanceToEye);
    float microWeight=1.0-smoothstep(6.0*1024.0,12.0*1024.0,distanceToEye);
    if(smallWeight>0.0) {
        vec2 c=texture2D(liquidSkyMap,back*uv*3.17+vec2(t*0.41,-t*0.73)+vec2(0.73,0.19)).rg*2.0-1.0;
        slopes+=(rotate*c)*(0.075*smallWeight);
    }
    if(microWeight>0.0) {
        vec2 e=texture2D(liquidSkyMap,uv*6.73+vec2(-t*0.33,-t*0.19)+vec2(0.13,0.43)).ba*2.0-1.0;
        slopes+=e*(0.035*microWeight);
    }
    return slopes;
}

void main (void) {
    float viewDistance = length(viewDir);
    // Fade small surface details consistently across polygon boundaries.
    float surfaceDetail = sprintathonDistantSurfaceDetail > 0.5 ?
)"
R"(        1.0 - smoothstep(16.0 * 1024.0, 32.0 * 1024.0, viewDistance) : 1.0;
	vec3 texCoords = vec3(gl_TexCoord[0].xy, 0.0);
	float surfaceWetness = mediaWetness * surfaceDetail;
	float rippleStrength = mediaRipple * surfaceDetail;
	float rippleHighlight = 1.0;
	vec2 mediaDetailOffset = vec2(0.0);
	float mediaTextureMix = 0.0;
	float wetTextureShade = 1.0;
	vec2 sceneRefractionOffset = vec2(0.0);
    vec2 liquidSlopes=vec2(0.0);
    if(liquidSpecLight.w>0.0 && rippleStrength>0.001) {
        liquidSlopes=sprintathonFlowingSlopes(sprintathonWorldPosition.xy+liquidFlow.xy,liquidSpecEye.w,viewDistance)
            * clamp(rippleStrength,0.0,1.5);
        // Reflection and refraction share this continuous wave field.
    }
	if (rippleStrength > 0.001) {
		float phase = time;
		vec2 base = sprintathonWorldPosition.xy / 1024.0;
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
			0.0125 * surfaceWetness;
		mediaTextureMix = clamp((0.34 + rippleFresnel * 0.14) *
			surfaceWetness, 0.0, 0.95);
		wetTextureShade = clamp(1.0 + (waveLight - 0.5) *
			0.12 * surfaceWetness, 0.72, 1.28);
		rippleHighlight = clamp(1.0 + (waveLight - 0.5) * 0.24 *
			rippleStrength + rippleFresnel * 0.05 * rippleStrength,
			0.65, 1.35);
	}
    if(liquidSpecLight.w>0.0 && rippleStrength>0.001) {
        // Retain the established liquid texture animation above. Only the
        // optical distortion follows the new reflection normal field.
        vec3 viewSlope=gl_NormalMatrix*vec3(liquidSlopes,0.0);
        sceneRefractionOffset=viewSlope.xy*22.0;
        // Let the specular model provide the highlight rather than layering
        // the old fixed-direction wave lighting on top of it.
        rippleHighlight=1.0;
        wetTextureShade=1.0;
    }
	vec3 normXY = normalize(viewXY);
	texCoords += vec3(normXY.y * -pulsate, normXY.x * pulsate, 0.0);
	texCoords += vec3(normXY.y * -wobble * texCoords.y, wobble * texCoords.y, 0.0);
	float playerLight = clamp(selfLuminosity + flare - (viewDistance/8192.0), 0.0, 1.0);
    // Keep the sector contribution independent of the player light when baking.
    float mlFactor = skyBakeEnabled > 0.5 ? 0.0 : playerLight;
	vec3 intensity;
	if (vertexColor.r > mlFactor) {
		intensity = vertexColor.rgb + (mlFactor * 0.5); }
	else {
		intensity = (vertexColor.rgb * 0.5) + mlFactor; }
	vec3 viewv = normalize(viewDir);
    // Far surfaces skip the four height-map samples entirely. Scale the
    // displacement to zero first, including bloom and shaft-source alpha.
    if (surfaceDetail > 0.0) {
        float scale = 0.010;
        float bias = -0.005;
        for (int i = 0; i < 4; ++i) {
            vec4 normal = texture2D(texture1, texCoords.xy);
            float h = (normal.a * scale + bias) * surfaceDetail;
            texCoords.x += h * viewv.x;
            texCoords.y -= h * viewv.y;
        }
    }
	// Opaque geometry only masks the sky in the shaft-source framebuffer.
	// Keep texture alpha (including cutouts); skip lighting, blending and fog.
	if (sprintathonShaftSource > 0.5) {
		float alpha = vertexColor.a * texture2D(texture0, texCoords.xy).a;
		gl_FragColor = vec4(0.0, 0.0, 0.0, alpha);
		return;
	}
    float diffuse = 0.5 + abs(viewv.z) * 0.5;
    if (surfaceDetail > 0.0) {
        vec3 norm = (texture2D(texture1, texCoords.xy).rgb - 0.5) * 2.0;
        float detailedDiffuse = 0.5 + abs(dot(norm, viewv)) * 0.5;
        diffuse = mix(diffuse, detailedDiffuse, surfaceDetail);
    }
	if (glow > 0.001) {
		diffuse = 1.0;
	}
	vec4 color = texture2D(texture0, texCoords.xy);
	if (surfaceWetness > 0.001) {
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
			1.0 + 0.10 * surfaceWetness) * wetTextureShade;
	}
	intensity = clamp(intensity * diffuse, glow, 1.0);
    // Each side contributes half of the shade difference at their shared edge.
    // Select one edge per fragment; blending them in sequence creates dark or
    // bright rectangular patches at corners and in small polygons.
    if (surfaceDetail > 0.0) {
    vec2 sectorPosition = sprintathonWorldPosition.xy;
    if (sprintathonWallBlendAxis.w > 0.5)
        sectorPosition = vec2(dot(sprintathonWorldPosition.xy, sprintathonWallBlendAxis.xy)
                              + sprintathonWallBlendAxis.z, sprintathonWorldPosition.z);
    float sectorBlendWeight = 0.0;
    float sectorShadeSum = 0.0;
    if (sprintathonSectorEdge0.w >= 0.0) {
        float edgeDistance = dot(sectorPosition, sprintathonSectorEdge0.xy) + sprintathonSectorEdge0.z;
        vec2 edgeVector = sprintathonSectorSpan0.zw - sprintathonSectorSpan0.xy;
        float edgeLength2 = dot(edgeVector, edgeVector);
        float alongEdge = dot(sectorPosition - sprintathonSectorSpan0.xy, edgeVector);
        if (edgeDistance >= 0.0 && edgeDistance < sprintathonSectorBlendWidth + 0.0625 && edgeLength2 > 1.0) {
            float t = clamp(alongEdge / edgeLength2, 0.0, 1.0);
            vec2 nearestPoint = sprintathonSectorSpan0.xy + t * edgeVector;
            vec2 segmentDelta = sectorPosition - nearestPoint;
            float segmentDistanceSquared = dot(segmentDelta, segmentDelta);
            if (segmentDistanceSquared < (sprintathonSectorBlendWidth) * (sprintathonSectorBlendWidth)) {
                float segmentDistance = sqrt(segmentDistanceSquared);
                float edgeBlend = 0.5 * (1.0 - smoothstep(0.0, sprintathonSectorBlendWidth, segmentDistance));
                if (edgeBlend > 0.0) {
                    float neighborBase = sprintathonSectorEdge0.w > mlFactor ?
                        sprintathonSectorEdge0.w + mlFactor * 0.5 : sprintathonSectorEdge0.w * 0.5 + mlFactor;
                    sectorShadeSum += clamp(neighborBase * diffuse, glow, 1.0) * edgeBlend;
                    sectorBlendWeight += edgeBlend;
                }
            }
        }
    }
    if (sprintathonSectorEdge1.w >= 0.0) {
        float edgeDistance = dot(sectorPosition, sprintathonSectorEdge1.xy) + sprintathonSectorEdge1.z;
        vec2 edgeVector = sprintathonSectorSpan1.zw - sprintathonSectorSpan1.xy;
        float edgeLength2 = dot(edgeVector, edgeVector);
        float alongEdge = dot(sectorPosition - sprintathonSectorSpan1.xy, edgeVector);
        if (edgeDistance >= 0.0 && edgeDistance < sprintathonSectorBlendWidth + 0.0625 && edgeLength2 > 1.0) {
            float t = clamp(alongEdge / edgeLength2, 0.0, 1.0);
            vec2 nearestPoint = sprintathonSectorSpan1.xy + t * edgeVector;
            vec2 segmentDelta = sectorPosition - nearestPoint;
            float segmentDistanceSquared = dot(segmentDelta, segmentDelta);
            if (segmentDistanceSquared < (sprintathonSectorBlendWidth) * (sprintathonSectorBlendWidth)) {
)"
R"(                float segmentDistance = sqrt(segmentDistanceSquared);
                float edgeBlend = 0.5 * (1.0 - smoothstep(0.0, sprintathonSectorBlendWidth, segmentDistance));
                if (edgeBlend > 0.0) {
                    float neighborBase = sprintathonSectorEdge1.w > mlFactor ?
                        sprintathonSectorEdge1.w + mlFactor * 0.5 : sprintathonSectorEdge1.w * 0.5 + mlFactor;
                    sectorShadeSum += clamp(neighborBase * diffuse, glow, 1.0) * edgeBlend;
                    sectorBlendWeight += edgeBlend;
                }
            }
        }
    }
    if (sprintathonSectorEdge2.w >= 0.0) {
        float edgeDistance = dot(sectorPosition, sprintathonSectorEdge2.xy) + sprintathonSectorEdge2.z;
        vec2 edgeVector = sprintathonSectorSpan2.zw - sprintathonSectorSpan2.xy;
        float edgeLength2 = dot(edgeVector, edgeVector);
        float alongEdge = dot(sectorPosition - sprintathonSectorSpan2.xy, edgeVector);
)"
R"(        if (edgeDistance >= 0.0 && edgeDistance < sprintathonSectorBlendWidth + 0.0625 && edgeLength2 > 1.0) {
            float t = clamp(alongEdge / edgeLength2, 0.0, 1.0);
            vec2 nearestPoint = sprintathonSectorSpan2.xy + t * edgeVector;
            vec2 segmentDelta = sectorPosition - nearestPoint;
            float segmentDistanceSquared = dot(segmentDelta, segmentDelta);
            if (segmentDistanceSquared < (sprintathonSectorBlendWidth) * (sprintathonSectorBlendWidth)) {
                float segmentDistance = sqrt(segmentDistanceSquared);
                float edgeBlend = 0.5 * (1.0 - smoothstep(0.0, sprintathonSectorBlendWidth, segmentDistance));
                if (edgeBlend > 0.0) {
                    float neighborBase = sprintathonSectorEdge2.w > mlFactor ?
                        sprintathonSectorEdge2.w + mlFactor * 0.5 : sprintathonSectorEdge2.w * 0.5 + mlFactor;
                    sectorShadeSum += clamp(neighborBase * diffuse, glow, 1.0) * edgeBlend;
                    sectorBlendWeight += edgeBlend;
                }
            }
        }
    }
    if (sprintathonSectorEdge3.w >= 0.0) {
        float edgeDistance = dot(sectorPosition, sprintathonSectorEdge3.xy) + sprintathonSectorEdge3.z;
        vec2 edgeVector = sprintathonSectorSpan3.zw - sprintathonSectorSpan3.xy;
        float edgeLength2 = dot(edgeVector, edgeVector);
        float alongEdge = dot(sectorPosition - sprintathonSectorSpan3.xy, edgeVector);
        if (edgeDistance >= 0.0 && edgeDistance < sprintathonSectorBlendWidth + 0.0625 && edgeLength2 > 1.0) {
            float t = clamp(alongEdge / edgeLength2, 0.0, 1.0);
            vec2 nearestPoint = sprintathonSectorSpan3.xy + t * edgeVector;
            vec2 segmentDelta = sectorPosition - nearestPoint;
            float segmentDistanceSquared = dot(segmentDelta, segmentDelta);
            if (segmentDistanceSquared < (sprintathonSectorBlendWidth) * (sprintathonSectorBlendWidth)) {
                float segmentDistance = sqrt(segmentDistanceSquared);
                float edgeBlend = 0.5 * (1.0 - smoothstep(0.0, sprintathonSectorBlendWidth, segmentDistance));
                if (edgeBlend > 0.0) {
                    float neighborBase = sprintathonSectorEdge3.w > mlFactor ?
                        sprintathonSectorEdge3.w + mlFactor * 0.5 : sprintathonSectorEdge3.w * 0.5 + mlFactor;
                    sectorShadeSum += clamp(neighborBase * diffuse, glow, 1.0) * edgeBlend;
                    sectorBlendWeight += edgeBlend;
                }
            }
        }
    }
    if (sprintathonSectorEdge4.w >= 0.0) {
        float edgeDistance = dot(sectorPosition, sprintathonSectorEdge4.xy) + sprintathonSectorEdge4.z;
        vec2 edgeVector = sprintathonSectorSpan4.zw - sprintathonSectorSpan4.xy;
        float edgeLength2 = dot(edgeVector, edgeVector);
        float alongEdge = dot(sectorPosition - sprintathonSectorSpan4.xy, edgeVector);
        if (edgeDistance >= 0.0 && edgeDistance < sprintathonSectorBlendWidth + 0.0625 && edgeLength2 > 1.0) {
            float t = clamp(alongEdge / edgeLength2, 0.0, 1.0);

            vec2 nearestPoint = sprintathonSectorSpan4.xy + t * edgeVector;
            vec2 segmentDelta = sectorPosition - nearestPoint;
            float segmentDistanceSquared = dot(segmentDelta, segmentDelta);
            if (segmentDistanceSquared < (sprintathonSectorBlendWidth) * (sprintathonSectorBlendWidth)) {
                float segmentDistance = sqrt(segmentDistanceSquared);
                float edgeBlend = 0.5 * (1.0 - smoothstep(0.0, sprintathonSectorBlendWidth, segmentDistance));
                if (edgeBlend > 0.0) {
                    float neighborBase = sprintathonSectorEdge4.w > mlFactor ?
                        sprintathonSectorEdge4.w + mlFactor * 0.5 : sprintathonSectorEdge4.w * 0.5 + mlFactor;
                    sectorShadeSum += clamp(neighborBase * diffuse, glow, 1.0) * edgeBlend;
                    sectorBlendWeight += edgeBlend;
                }
            }
        }
    }
    if (sprintathonSectorEdge5.w >= 0.0) {
        float edgeDistance = dot(sectorPosition, sprintathonSectorEdge5.xy) + sprintathonSectorEdge5.z;
        vec2 edgeVector = sprintathonSectorSpan5.zw - sprintathonSectorSpan5.xy;
        float edgeLength2 = dot(edgeVector, edgeVector);
        float alongEdge = dot(sectorPosition - sprintathonSectorSpan5.xy, edgeVector);
        if (edgeDistance >= 0.0 && edgeDistance < sprintathonSectorBlendWidth + 0.0625 && edgeLength2 > 1.0) {
            float t = clamp(alongEdge / edgeLength2, 0.0, 1.0);
            vec2 nearestPoint = sprintathonSectorSpan5.xy + t * edgeVector;
            vec2 segmentDelta = sectorPosition - nearestPoint;
            float segmentDistanceSquared = dot(segmentDelta, segmentDelta);
            if (segmentDistanceSquared < (sprintathonSectorBlendWidth) * (sprintathonSectorBlendWidth)) {
                float segmentDistance = sqrt(segmentDistanceSquared);
                float edgeBlend = 0.5 * (1.0 - smoothstep(0.0, sprintathonSectorBlendWidth, segmentDistance));
                if (edgeBlend > 0.0) {
                    float neighborBase = sprintathonSectorEdge5.w > mlFactor ?
                        sprintathonSectorEdge5.w + mlFactor * 0.5 : sprintathonSectorEdge5.w * 0.5 + mlFactor;
                    sectorShadeSum += clamp(neighborBase * diffuse, glow, 1.0) * edgeBlend;
                    sectorBlendWeight += edgeBlend;
                }
            }
        }
    }
    if (sprintathonSectorEdge6.w >= 0.0) {
        float edgeDistance = dot(sectorPosition, sprintathonSectorEdge6.xy) + sprintathonSectorEdge6.z;
        vec2 edgeVector = sprintathonSectorSpan6.zw - sprintathonSectorSpan6.xy;
        float edgeLength2 = dot(edgeVector, edgeVector);
        float alongEdge = dot(sectorPosition - sprintathonSectorSpan6.xy, edgeVector);
        if (edgeDistance >= 0.0 && edgeDistance < sprintathonSectorBlendWidth + 0.0625 && edgeLength2 > 1.0) {
            float t = clamp(alongEdge / edgeLength2, 0.0, 1.0);
            vec2 nearestPoint = sprintathonSectorSpan6.xy + t * edgeVector;
            vec2 segmentDelta = sectorPosition - nearestPoint;
            float segmentDistanceSquared = dot(segmentDelta, segmentDelta);
            if (segmentDistanceSquared < (sprintathonSectorBlendWidth) * (sprintathonSectorBlendWidth)) {
                float segmentDistance = sqrt(segmentDistanceSquared);
                float edgeBlend = 0.5 * (1.0 - smoothstep(0.0, sprintathonSectorBlendWidth, segmentDistance));
                if (edgeBlend > 0.0) {
                    float neighborBase = sprintathonSectorEdge6.w > mlFactor ?
                        sprintathonSectorEdge6.w + mlFactor * 0.5 : sprintathonSectorEdge6.w * 0.5 + mlFactor;
                    sectorShadeSum += clamp(neighborBase * diffuse, glow, 1.0) * edgeBlend;
)"
R"(                    sectorBlendWeight += edgeBlend;
                }
            }
        }
    }
    if (sprintathonSectorEdge7.w >= 0.0) {
        float edgeDistance = dot(sectorPosition, sprintathonSectorEdge7.xy) + sprintathonSectorEdge7.z;
        vec2 edgeVector = sprintathonSectorSpan7.zw - sprintathonSectorSpan7.xy;
        float edgeLength2 = dot(edgeVector, edgeVector);
        float alongEdge = dot(sectorPosition - sprintathonSectorSpan7.xy, edgeVector);
        if (edgeDistance >= 0.0 && edgeDistance < sprintathonSectorBlendWidth + 0.0625 && edgeLength2 > 1.0) {
            float t = clamp(alongEdge / edgeLength2, 0.0, 1.0);
            vec2 nearestPoint = sprintathonSectorSpan7.xy + t * edgeVector;
            vec2 segmentDelta = sectorPosition - nearestPoint;
            float segmentDistanceSquared = dot(segmentDelta, segmentDelta);
            if (segmentDistanceSquared < (sprintathonSectorBlendWidth) * (sprintathonSectorBlendWidth)) {
)"
R"(                float segmentDistance = sqrt(segmentDistanceSquared);
                float edgeBlend = 0.5 * (1.0 - smoothstep(0.0, sprintathonSectorBlendWidth, segmentDistance));
                if (edgeBlend > 0.0) {
                    float neighborBase = sprintathonSectorEdge7.w > mlFactor ?
                        sprintathonSectorEdge7.w + mlFactor * 0.5 : sprintathonSectorEdge7.w * 0.5 + mlFactor;
                    sectorShadeSum += clamp(neighborBase * diffuse, glow, 1.0) * edgeBlend;
                    sectorBlendWeight += edgeBlend;
                }
            }
        }
    }
    if (sectorBlendWeight > 0.0)
        intensity = mix(intensity, vec3(sectorShadeSum / sectorBlendWeight),
                        min(sectorBlendWeight, 0.5) * surfaceDetail);
    }
    // Combine live sector brightness with static sky illumination. Surface UVs
    // derive from world coordinates, independent of texture scrolling.
    if (skyBakeEnabled > 0.5) {
        vec4 wp = vec4(sprintathonWorldPosition, 1.0);
        vec2 baked = sampleSkyBake(vec2(dot(wp, skyBakeU), dot(wp, skyBakeV)));
        vec3 combined = vec3(skyBakeAmbient.a) + baked.r * skyBakeSun.rgb + baked.g * skyBakeAmbient.rgb
            + intensity * clamp(skyBakeSectorStrength, 0.0, 1.0);
        // Apply the existing player/muzzle light once, after combining world light.
        intensity = combined;
        if (playerLight > 0.0)
            intensity = combined.r > playerLight ? combined + vec3(playerLight * 0.5)
                                                : combined * 0.5 + vec3(playerLight);
        intensity = clamp(intensity, glow, 1.0);
    }
    if (sprintathonConeColor0.a > 0.0) {
        vec2 poolDelta = (sprintathonWorldPosition.xy - sprintathonConePool0.xy) * sprintathonConePool0.w;
        float poolDistance2 = dot(poolDelta, poolDelta);
        if (poolDistance2 < 1.0) {
            float falloff = 1.0 - smoothstep(0.0, 1.0, sqrt(poolDistance2));
            intensity = clamp(intensity + sprintathonConeColor0.rgb * falloff, glow, 1.0);
        }
    }
    if (sprintathonConeColor1.a > 0.0) {
        vec2 poolDelta = (sprintathonWorldPosition.xy - sprintathonConePool1.xy) * sprintathonConePool1.w;
        float poolDistance2 = dot(poolDelta, poolDelta);
        if (poolDistance2 < 1.0) {
            float falloff = 1.0 - smoothstep(0.0, 1.0, sqrt(poolDistance2));
            intensity = clamp(intensity + sprintathonConeColor1.rgb * falloff, glow, 1.0);
        }
    }
    if (sprintathonConeColor2.a > 0.0) {
        vec2 poolDelta = (sprintathonWorldPosition.xy - sprintathonConePool2.xy) * sprintathonConePool2.w;
        float poolDistance2 = dot(poolDelta, poolDelta);
        if (poolDistance2 < 1.0) {
            float falloff = 1.0 - smoothstep(0.0, 1.0, sqrt(poolDistance2));
            intensity = clamp(intensity + sprintathonConeColor2.rgb * falloff, glow, 1.0);
        }
    }
    if (sprintathonConeColor3.a > 0.0) {
        vec2 poolDelta = (sprintathonWorldPosition.xy - sprintathonConePool3.xy) * sprintathonConePool3.w;
        float poolDistance2 = dot(poolDelta, poolDelta);
        if (poolDistance2 < 1.0) {
            float falloff = 1.0 - smoothstep(0.0, 1.0, sqrt(poolDistance2));
            intensity = clamp(intensity + sprintathonConeColor3.rgb * falloff, glow, 1.0);
        }
    }
    if (sprintathonMuzzleColor.a > 0.0 && any(lessThan(intensity, vec3(1.0)))) {
        vec3 muzzleDelta = (sprintathonWorldPosition - sprintathonMuzzlePosition.xyz) * sprintathonMuzzlePosition.w;
        float lightDistanceSquared = dot(muzzleDelta, muzzleDelta);
        if (lightDistanceSquared < 1.0) {
            float muzzleFalloff = 1.0 - lightDistanceSquared;
            intensity = clamp(intensity + sprintathonMuzzleColor.rgb *
                              (muzzleFalloff * muzzleFalloff), glow, 1.0);
        }
    }
    // A uniform branch skips the distance math for unlit surfaces and when the option is off.
    if (sprintathonLightColor.a > 0.0 && any(lessThan(intensity, vec3(1.0)))) {
        vec3 lightDelta = (sprintathonWorldPosition - sprintathonLightPosition.xyz) * sprintathonLightPosition.w;
        float lightDistanceSquared = dot(lightDelta, lightDelta);
        if (lightDistanceSquared < 1.0) {
            float lightFalloff = 1.0 - lightDistanceSquared;
            intensity = clamp(intensity + sprintathonLightColor.rgb * (lightFalloff * lightFalloff), glow, 1.0);
        }
    }
    sprintathonApplySurfaceLights(intensity);
    sprintathonApplyAllScenery(intensity);
	intensity = clamp(intensity * rippleHighlight, glow, 1.0);
#ifdef GAMMA_CORRECTED_BLENDING
	intensity = intensity * intensity; // approximation of pow(intensity, 2.2)
#endif
	vec3 shadedColor = clamp(color.rgb * intensity, 0.0, 1.0);
	float fogFactor = getFogFactor(viewDistance);
	if (mediaFogEnabled > 0.0) {
		float heightFog = clamp((mediaFogTop - worldZ) / mediaFogSoftness, 0.0, 1.0);
		float heightMask = mix(1.0, heightFog, mediaFogEnabled);
		fogFactor = 1.0 - (1.0 - fogFactor) * heightMask;
	}
    vec3 liquidReflection=vec3(0.0), liquidDirect=vec3(0.0);
    float liquidReflectWeight=0.0;
    if (liquidSpecLight.w > 0.0) {
        vec3 eyeDelta = liquidSpecEye.xyz - sprintathonWorldPosition;
        vec3 eye = eyeDelta / max(length(eyeDelta),0.0001);
        if (eye.z > 0.0) {
            vec2 slopes=liquidSlopes;
            vec3 normal=normalize(vec3(-slopes,1.0));
            vec3 halfVector=normalize(eye+liquidSpecLight.xyz);
            float nv=max(dot(normal,eye),0.001);
            float nl=max(dot(normal,liquidSpecLight.xyz),0.0);
            float nh=max(dot(normal,halfVector),0.0);
            // Approximate fog-softened sunlight with a wider normalized lobe.
            // Reuse the surface fog amount, including the media-height mask.
            float fogSpread=smoothstep(0.0,0.85,1.0-clamp(fogFactor,0.0,1.0));
            // Keep roughness stable; the texture normals provide the detail.
            float sunRoughness=mix(0.024,0.070,fogSpread);
            vec3 ndx=dFdx(normal), ndy=dFdy(normal);
            float alpha2=clamp(sunRoughness*sunRoughness+
                0.5*(dot(ndx,ndx)+dot(ndy,ndy)),0.000324,0.25);
            float denominator=nh*nh*(alpha2-1.0)+1.0;
            float distribution=alpha2/(3.14159265*denominator*denominator);
            float k=sqrt(alpha2)*0.5;
            float visibility=1.0/(4.0*(nv*(1.0-k)+k)*(nl*(1.0-k)+k));
            float glint=distribution*visibility*nl;
            float grazing=1.0-clamp(nv,0.0,1.0);
            float fresnel=0.02+0.98*pow(grazing,5.0);
            float specFresnel=0.02+0.98*pow(1.0-clamp(dot(eye,halfVector),0.0,1.0),5.0);
            float exposure = 1.0;
            float skyExposure = 1.0;
            vec3 tint = vec3(1.0);
            if (skyBakeEnabled > 0.5) {
                vec4 wp = vec4(sprintathonWorldPosition,1.0);
                vec2 skyVisibility = sampleSkyBake(vec2(dot(wp,skyBakeU),dot(wp,skyBakeV)));
                float sunlight = skyVisibility.r;
                skyExposure = clamp(skyVisibility.g / 0.3,0.0,1.0);
                exposure = clamp(sunlight/max(0.8*liquidSpecLight.z,0.001),0.0,1.0);
                tint = skyBakeSun.rgb;
            }
            if(visibleSunColor.w>0.0) tint=visibleSunColor.rgb*visibleSunColor.w;
            vec3 reflectionColor=shadedColor;
            float reflectionCoverage=0.0;
            if(liquidSkyMode > 0.5) {
                vec3 waveNormal = normalize(vec3(-slopes,1.0));
                vec3 reflected = reflect(-eye,waveNormal);
                // One stable landscape-derived colour; no sky image or disc.
                reflectionColor=liquidSkyParams.rgb;
                reflectionCoverage=skyExposure*smoothstep(0.0,0.05,reflected.z);
            }
            // Water reflects little head-on and strongly near grazing angles.
            // Reflected radiance is controlled by sky visibility, not by the
            // diffuse sector brightness of the surface receiving it.
            float sheenWeight=fresnel*liquidSpecLight.w*reflectionCoverage;
            liquidReflection=reflectionColor;
            liquidReflectWeight=clamp(sheenWeight,0.0,0.98);
            // Keep the widened reflection soft and give low slider values
            // finer control. Landscape sheen retains its existing response.
            // Avoid a hard radiance cap, which flattens bright crests into
            // uniform patches. Fade the most extreme grazing-angle boost.
            float grazingSoftness=mix(0.55,1.0,smoothstep(0.0,0.25,nv));
            vec3 sunRadiance=max(tint,vec3(0.0))*(glint*specFresnel*1.5*exposure*grazingSoftness);
            float sunGain=liquidSpecLight.w*min(liquidSpecLight.w,1.0);
            vec3 specular=(sunRadiance/(vec3(1.0)+sunRadiance))*sunGain
                * mix(0.75,0.55,fogSpread)*step(0.001,liquidSpecLight.z);
            // One selected projectile plus four existing surface-light slots.
            vec3 localRadiance=sprintathonWaterLight(sprintathonLightPosition,sprintathonLightColor,normal,eye,alpha2)
                +(sprintathonSurfaceLightCount>0.0 ? sprintathonWaterLight(sprintathonLightPosition2,sprintathonLightColor2,normal,eye,alpha2) : vec3(0.0))
                +(sprintathonSurfaceLightCount>1.0 ? sprintathonWaterLight(sprintathonLightPosition3,sprintathonLightColor3,normal,eye,alpha2) : vec3(0.0))
                +(sprintathonSurfaceLightCount>2.0 ? sprintathonWaterLight(sprintathonLightPosition4,sprintathonLightColor4,normal,eye,alpha2) : vec3(0.0))
                +(sprintathonSurfaceLightCount>3.0 ? sprintathonWaterLight(sprintathonLightPosition5,sprintathonLightColor5,normal,eye,alpha2) : vec3(0.0));
            localRadiance+=sprintathonWaterLight(waterSceneryPosition0,waterSceneryColor0,normal,eye,alpha2)
                +sprintathonWaterLight(waterSceneryPosition1,waterSceneryColor1,normal,eye,alpha2);
            liquidDirect=specular+localRadiance/(vec3(1.0)+localRadiance)*sunGain*0.75;
        }
    }
    // Preserve surface detail through luminance, without tinting orange light
    // green on green textures. One shared headroom factor preserves RGB ratios.
    vec3 caustic = sprintathonCausticRadiance * dot(color.rgb, vec3(0.299, 0.587, 0.114));
    vec3 headroom = (vec3(1.0) - shadedColor) / max(caustic, vec3(0.00001));
    shadedColor += caustic * clamp(min(headroom.r, min(headroom.g, headroom.b)), 0.0, 1.0);
	vec3 finalColor = mix(gl_Fog.color.rgb, shadedColor, fogFactor);
    vec3 reflectedFog=mix(gl_Fog.color.rgb,liquidReflection,fogFactor);
    vec3 directFog=liquidDirect*fogFactor;
	if (rippleStrength > 0.001 || (liquidSpecLight.w>0.0 && liquidSceneAvailable>0.5)) {
)"
R"(        // Screen-space distance keeps the guard narrow at every viewing angle.
        float edgePixels = 100000.0;
        if (liquidEdge0.w > 0.0) {
            float d = dot(sprintathonWorldPosition.xy, liquidEdge0.xy) + liquidEdge0.z;
            edgePixels = min(edgePixels, abs(d)/max(fwidth(d),0.0001));
        }
        if (liquidEdge1.w > 0.0) {
            float d = dot(sprintathonWorldPosition.xy, liquidEdge1.xy) + liquidEdge1.z;
            edgePixels = min(edgePixels, abs(d)/max(fwidth(d),0.0001));
        }
        if (liquidEdge2.w > 0.0) {
            float d = dot(sprintathonWorldPosition.xy, liquidEdge2.xy) + liquidEdge2.z;
            edgePixels = min(edgePixels, abs(d)/max(fwidth(d),0.0001));
        }
        if (liquidEdge3.w > 0.0) {
            float d = dot(sprintathonWorldPosition.xy, liquidEdge3.xy) + liquidEdge3.z;
            edgePixels = min(edgePixels, abs(d)/max(fwidth(d),0.0001));
        }
        if (liquidEdge4.w > 0.0) {
            float d = dot(sprintathonWorldPosition.xy, liquidEdge4.xy) + liquidEdge4.z;
            edgePixels = min(edgePixels, abs(d)/max(fwidth(d),0.0001));
        }
        if (liquidEdge5.w > 0.0) {
            float d = dot(sprintathonWorldPosition.xy, liquidEdge5.xy) + liquidEdge5.z;
            edgePixels = min(edgePixels, abs(d)/max(fwidth(d),0.0001));
        }
        if (liquidEdge6.w > 0.0) {
            float d = dot(sprintathonWorldPosition.xy, liquidEdge6.xy) + liquidEdge6.z;
            edgePixels = min(edgePixels, abs(d)/max(fwidth(d),0.0001));
        }
        if (liquidEdge7.w > 0.0) {
            float d = dot(sprintathonWorldPosition.xy, liquidEdge7.xy) + liquidEdge7.z;
            edgePixels = min(edgePixels, abs(d)/max(fwidth(d),0.0001));
        }
        float offsetLength = length(sceneRefractionOffset);
        sceneRefractionOffset *= smoothstep(0.0, offsetLength*4.0+2.0, edgePixels);
        vec2 refractedCoord = clamp(gl_FragCoord.xy + sceneRefractionOffset,
            vec2(0.5), vec2(pixelWidth, pixelHeight) - vec2(0.5));
		vec3 refractedScene = texture2DRect(texture2,
			refractedCoord).rgb;
		float liquidAlpha = clamp(vertexColor.a, 0.0, 1.0);
        // First compose transmission. Opacity controls liquid tint/body only.
        vec3 transmitted=mix(refractedScene,finalColor,liquidAlpha);
        // Reflection belongs to the interface, independent of its transparency.
        vec3 water=mix(transmitted,reflectedFog,liquidReflectWeight)+directFog;
        gl_FragColor=vec4(clamp(water,0.0,1.0),1.0);
	} else {
        vec3 surface=mix(finalColor,reflectedFog,liquidReflectWeight)+directFog;
        gl_FragColor = vec4(clamp(surface,0.0,1.0), vertexColor.a * color.a);
	}
}

)"

R"(

uniform sampler2D texture0;
uniform samplerCube sprintathonCubeSky;
uniform bool sprintathonCubeSkyEnabled;
uniform float fogMix;
varying vec3 relDir;
varying vec4 vertexColor;
varying float cosPitch;
varying float sinPitch;
varying float cosYaw;
varying float sinYaw;
uniform float offsetx; // azimuth in the sphere map
const float M_PI = 3.14156;
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


uniform bool sprintathonStarsEnabled;
uniform float sprintathonStarsTime;
vec3 sprintathonStarHash(vec3 p) {
    p=fract(p*vec3(0.1031,0.1030,0.0973));
    p+=dot(p,p.yxz+33.33);
    return fract((p.xxy+p.yxx)*p.zyx);
}

// Continuous direction-space noise: no cube-face or longitude boundaries.
float sprintathonGalaxyNoise(vec3 p) {
    vec3 cell=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);
    float a=mix(sprintathonStarHash(cell).x,sprintathonStarHash(cell+vec3(1,0,0)).x,f.x);
    float b=mix(sprintathonStarHash(cell+vec3(0,1,0)).x,sprintathonStarHash(cell+vec3(1,1,0)).x,f.x);
    float c=mix(sprintathonStarHash(cell+vec3(0,0,1)).x,sprintathonStarHash(cell+vec3(1,0,1)).x,f.x);
    float d=mix(sprintathonStarHash(cell+vec3(0,1,1)).x,sprintathonStarHash(cell+vec3(1,1,1)).x,f.x);
    return mix(mix(a,b,f.y),mix(c,d,f.y),f.z);
}
vec3 sprintathonGalaxy(vec3 ray) {
    vec3 normal=normalize(vec3(0.36,0.63,0.69));
    float latitude=dot(ray,normal);
    float broad=exp(-latitude*latitude/0.018);
    float cloud=sprintathonGalaxyNoise(ray*8.0+vec3(57.0));
    float detail=sprintathonGalaxyNoise(ray*29.0+vec3(113.0));
    float centre=latitude+(cloud-0.5)*0.085;
    float dust=exp(-centre*centre/0.00065)*smoothstep(0.25,0.75,detail);
    float structure=mix(0.25,1.0,smoothstep(0.18,0.85,cloud*0.75+detail*0.25));
    float core=exp(-centre*centre/0.0045);
    vec3 tint=mix(vec3(0.47,0.58,0.78),vec3(0.79,0.75,0.67),cloud);
    return tint*(broad*0.036+core*0.023)*structure*(1.0-0.82*dust);
}

vec3 sprintathonStars(vec3 worldRay) {
    vec3 ray=normalize(worldRay);
    vec3 p=ray*100.0;
    vec3 base=floor(p);
    vec3 neighbour=mix(vec3(-1.0),vec3(1.0),step(vec3(0.5),fract(p)));
    float footprint=max(length(dFdx(p)),length(dFdy(p)));
    float visibility=1.0-smoothstep(0.75,1.25,footprint);
    vec3 result=vec3(0.0007,0.001,0.002)+sprintathonGalaxy(ray);
    for(int x=0;x<2;++x)for(int y=0;y<2;++y)for(int z=0;z<2;++z) {
    vec3 cell=base+vec3(float(x),float(y),float(z))*neighbour;
    vec3 random=sprintathonStarHash(cell+vec3(127.0));
    // Include nearest neighbours so stars cross cell boundaries continuously.
    vec3 centre=cell+random;
    vec3 delta=centre-p;
    float radial=dot(delta,ray);
    float tangent2=max(0.0,dot(delta,delta)-radial*radial);
    vec3 properties=sprintathonStarHash(cell+vec3(311.0,97.0,53.0));
    float radius=mix(0.045,0.075,properties.x);
    if(properties.z<0.88 || abs(radial)>0.25 || tangent2>0.24) continue;
    float variance=radius*radius+footprint*footprint/12.0;
    float core=exp(-0.5*tangent2/variance)*radius*radius/variance;
    float shell=exp(-0.5*radial*radial/(radius*radius));
    float population=step(0.88,properties.z);
    float brightness=mix(0.65,3.0,pow(properties.y,5.0));
    float phase=6.2831853*random.z;
    float twinkle=1.0+0.045*sin(sprintathonStarsTime*mix(0.65,1.8,random.x)+phase)
                       +0.025*sin(sprintathonStarsTime*mix(0.3,0.7,random.y)+phase*1.7);
    vec3 tint=mix(vec3(0.72,0.84,1.0),vec3(1.0,0.88,0.72),properties.x);
    result+=tint*core*shell*population*visibility*brightness*twinkle;
    }
    return result;
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
    if(sprintathonCubeSkyEnabled) color=textureCube(sprintathonCubeSky, normalize(sunWorldRay.yzx));
    if(sprintathonStarsEnabled) color=vec4(sprintathonStars(sunWorldRay),1.0);
    if(timeOfDayZenith.w>0.5 && !sprintathonCubeSkyEnabled && !sprintathonStarsEnabled) color.rgb=mix(timeOfDayHorizon.rgb,timeOfDayZenith.rgb, pow(clamp(normalize(sunWorldRay).z,0.0,1.0),0.55));
    color.rgb += sprintathonSunDisc(sunWorldRay);
	vec3 intensity = mix(color.rgb, gl_Fog.color.rgb, fogMix);
	gl_FragColor = vec4(intensity, 1.0);
	gl_FragDepth = 0.9999;
}

)"

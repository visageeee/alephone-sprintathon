R"FFOG(
uniform vec4 flashlightParams;
uniform vec4 flashlightProjection; // lamp x, lamp y, radius, lamp z
uniform vec4 flashlightDepth;
uniform sampler2D flashlightShadowMap;
uniform mat4 flashlightShadowMatrix;
uniform vec4 flashlightShadowParams;
uniform float vfFlashlightIntensity;

float flashlightFogVisibility(vec3 eye) {
    if(flashlightShadowParams.x<=0.0)return 1.0;
    vec4 clip=flashlightShadowMatrix*vec4(eye,1.0);
    vec3 p=clip.xyz/max(clip.w,0.00001)*0.5+0.5;
    if(clip.w<=0.0 || p.z<=0.0 || p.z>=1.0 ||
       p.x<=0.0 || p.x>=1.0 || p.y<=0.0 || p.y>=1.0)return 1.0;
    // No screen derivatives inside a divergent ray-march loop. Four taps
    // smooth the depth-map edge, sharing the existing surface shadow map.
    float visible=0.0;
    for(int y=0;y<2;++y)for(int x=0;x<2;++x){
        vec2 uv=p.xy+(vec2(float(x),float(y))-0.5)*flashlightShadowParams.y;
        visible+=step(p.z-0.000015,texture2D(flashlightShadowMap,uv).r);
    }
    return mix(1.0,visible*0.25,flashlightShadowParams.x);
}
// Straight cone from the below-camera lamp: radius increases linearly with distance.
float sprintathonFlashlightBeamRadius(float along) {
    return max(flashlightProjection.z+max(along,0.0)*flashlightParams.y,0.0001);
}
vec3 sprintathonFlashlightFog(vec3 eye) {
    if(vfFlashlightIntensity<=0.0 || flashlightParams.x<=0.0)return vec3(0.0);
    vec3 lamp=vec3(flashlightProjection.xy,flashlightProjection.w);
    vec3 delta=eye-lamp;
    vec3 axis=normalize(vec3(flashlightDepth.zw,-1.0));
    float along=dot(delta,axis);
    float distance=length(delta);
    float radius=length(delta-axis*along)/sprintathonFlashlightBeamRadius(along);
    if(along<=0.0 || radius>=1.0 || distance>=flashlightParams.z)return vec3(0.0);
    // A smooth scattering profile avoids projecting concentric surface rings
    // into the fog. Density and transmittance remain owned by the fog integrator.
    float cone=(1.0-smoothstep(0.75,1.0,radius))*(0.65+0.35*exp(-radius*radius*2.5));
    float attenuation=(1.0-smoothstep(flashlightParams.z*0.65,flashlightParams.z,distance)) /
        (1.0+pow(distance/max(flashlightParams.z*0.25,1.0),2.0));
    return vec3(1.0,0.96,0.88)*flashlightParams.x*vfFlashlightIntensity*
        cone*attenuation*flashlightFogVisibility(eye);
}
// Stratified spatial jitter breaks coherent rings without time-varying noise.
// Only flashlight lighting gets extra samples; the existing density/noise and
// ambient-fog integration are unchanged.
vec3 sprintathonFlashlightFogSegment(vec3 ray,float start,float finish,float jitter) {
    if(vfFlashlightIntensity<=0.0 || flashlightParams.x<=0.0 || finish<=start)return vec3(0.0);
    vec3 light=vec3(0.0);
    for(int sampleIndex=0;sampleIndex<4;++sampleIndex) {
        float fraction=(float(sampleIndex)+jitter)*0.25;
        light+=sprintathonFlashlightFog(ray*mix(start,finish,fraction));
    }
    return light*0.25;
}
)FFOG"

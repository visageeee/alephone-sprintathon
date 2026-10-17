R"(
uniform sampler2D texture0;
uniform sampler2D liquidSkyMap;
uniform float time,pulsate,fogMode;
uniform float mediaFogEnabled,mediaFogTop,mediaFogSoftness;
uniform vec4 liquidSpecEye;
varying vec4 reflectionUV;
varying vec3 reflectionWorld;
varying vec4 reflectionColour;
varying float reflectionDistance;
void main() {
    vec2 uv=reflectionUV.xy/max(reflectionUV.w,0.00001);
    // World-anchored waves use the same phase clock as this liquid's ripples.
    vec2 position=reflectionWorld.xy/1024.0;
    vec2 noise=texture2D(liquidSkyMap,position*0.72).rg*2.0-1.0;
    float bands=sin(dot(position,vec2(7.0,4.0))+time+noise.x*1.4);
    float detail=sin(dot(position,vec2(-11.0,9.0))-time*2.0+noise.y);
    // Sprite U runs vertically; V runs horizontally.
    uv+=vec2(0.010*detail,0.026*bands+0.012*detail)*pulsate;
    // Projective q encodes the mirrored source's height above this pool.
    float height=max(0.0,(liquidSpecEye.z-reflectionWorld.z)*
        (1.0/max(reflectionUV.w,0.00001)-1.0))/1024.0;
    float reflectionFade=exp(-height*1.6);
    reflectionFade*=1.0-smoothstep(2.0,4.0,height);
    // Discard outside the sprite rather than smearing its border pixels.
    if(any(lessThan(uv,vec2(0.0))) || any(greaterThan(uv,vec2(1.0))))discard;
    vec4 texel=texture2D(texture0,(gl_TextureMatrix[0]*vec4(uv,0.0,1.0)).xy);
    vec3 eye=normalize(liquidSpecEye.xyz-reflectionWorld);
    float grazing=1.0-clamp(eye.z,0.0,1.0);
    float fresnel=0.08+0.42*grazing*grazing;
    float fog=1.0;
    if(fogMode==0.0)fog=clamp((gl_Fog.end-reflectionDistance)/max(gl_Fog.end-gl_Fog.start,0.001),0.0,1.0);
    else if(fogMode==1.0)fog=exp(-gl_Fog.density*reflectionDistance);
    else if(fogMode==2.0)fog=exp(-gl_Fog.density*gl_Fog.density*reflectionDistance*reflectionDistance);
    if(mediaFogEnabled>0.0) {
        float mask=clamp((mediaFogTop-reflectionWorld.z)/max(mediaFogSoftness,0.001),0.0,1.0);
        fog=1.0-(1.0-fog)*mix(1.0,mask,mediaFogEnabled);
    }
    gl_FragColor=vec4(texel.rgb*reflectionColour.rgb,texel.a*reflectionColour.a*fresnel*fog*reflectionFade);
}
)"

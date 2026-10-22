R"GLSL(
// Use the rasterized surface's perspective-correct eye position. Window depth
// includes renderer depth offsets and depends on the active render viewport.
varying vec3 flashlightEye;
uniform vec4 flashlightParams; // brightness, tan(half angle), range, offset multiplier
uniform vec4 flashlightProjection; // p00, p11, p02, p12
uniform vec4 flashlightDepth; // p22, p23, lagged beam direction slopes
uniform vec4 flashlightViewport;
uniform sampler2D flashlightShadowMap;
uniform mat4 flashlightShadowMatrix;
uniform vec4 flashlightShadowParams; // strength, inverse map size, distance softness, maximum radius in texels
float sprintathonFlashlightVisibility(vec3 eye) {
    if(flashlightShadowParams.x<=0.0)return 1.0;
    vec4 clip=flashlightShadowMatrix*vec4(eye,1.0);
    vec3 coord=clip.xyz/max(clip.w,0.00001)*0.5+0.5;
    if(clip.w<=0.0 || coord.z<=0.0 || coord.z>=1.0 ||
       coord.x<=0.0 || coord.x>=1.0 || coord.y<=0.0 || coord.y>=1.0)return 1.0;
    // Receiver-plane correction keeps the wider filter from shadowing a
    // sloped surface with its own neighbouring depth samples.
    vec3 dx=dFdx(coord),dy=dFdy(coord);
    float determinant=dx.x*dy.y-dx.y*dy.x;
    vec2 slope=vec2(0.0);
    if(abs(determinant)>0.000000000001)
        slope=vec2(dx.z*dy.y-dy.z*dx.y,dx.x*dy.z-dy.x*dx.z)/determinant;
    float bias=0.000015;
    float texel=flashlightShadowParams.y;
    // Disabled (or zero maximum) uses a small, fixed antialiasing filter.
    if(flashlightShadowParams.z<0.5 || flashlightShadowParams.w<=0.0){
        float visible=0.0;
        for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
            vec2 offset=vec2(float(x),float(y))*texel*0.75;
            vec2 uv=coord.xy+offset;
            if(uv.x<=0.0 || uv.x>=1.0 || uv.y<=0.0 || uv.y>=1.0){visible+=1.0;continue;}
            visible+=step(coord.z+dot(slope,offset)-bias,texture2D(flashlightShadowMap,uv).r);
        }
        return mix(1.0,visible/9.0,flashlightShadowParams.x);
    }
    float maxRadius=max(0.75,flashlightShadowParams.w)*texel;
    float nearZ=8.0,farZ=flashlightParams.z;
    float receiverDistance=clip.w;
    // Estimate blocker distance in linear light-space units. Window depth
    // differences alone would make softness depend strongly on distance to lamp.
    float blockerSum=0.0,blockerCount=0.0;
    for(int i=0;i<17;++i){
        float angle=float(i)*2.39996323;
        vec2 offset=vec2(cos(angle),sin(angle))*sqrt(float(i)/16.0)*maxRadius;
        vec2 uv=coord.xy+offset;
        if(uv.x<=0.0 || uv.x>=1.0 || uv.y<=0.0 || uv.y>=1.0)continue;
        float depth=texture2D(flashlightShadowMap,uv).r;
        if(depth<coord.z+dot(slope,offset)-bias){
            blockerSum+=nearZ*farZ/max(farZ-depth*(farZ-nearZ),0.0001);
            blockerCount+=1.0;
        }
    }
    if(blockerCount<0.5)return 1.0;
    float blockerDistance=blockerSum/blockerCount;
    // A fixed aperture gives linear world-space penumbra growth with the
    // caster/receiver gap; projection converts that width into shadow-map UVs.
    float tangent=max(1.0,flashlightParams.y+0.25);
    float gap=max(0.0,receiverDistance-blockerDistance);
    float aperture=48.0;
    float radius=aperture*gap/
        max(2.0*tangent*blockerDistance*receiverDistance,1.0);
    radius=clamp(radius,0.75*texel,maxRadius);
    float visibility=0.0;
    // Fixed disk pattern avoids both square-grid bands and temporal noise.
    for(int i=0;i<32;++i){
        float angle=float(i)*2.39996323;
        vec2 offset=vec2(cos(angle),sin(angle))*sqrt((float(i)+0.5)/32.0)*radius;
        vec2 uv=coord.xy+offset;
        if(uv.x<=0.0 || uv.x>=1.0 || uv.y<=0.0 || uv.y>=1.0){visibility+=1.0;continue;}
        visibility+=step(coord.z+dot(slope,offset)-bias,texture2D(flashlightShadowMap,uv).r);
    }
    return mix(1.0,visibility/32.0,flashlightShadowParams.x);
}
// Shared by surface lighting and the screen-space shadow pass.
vec3 sprintathonFlashlightOrigin(float distanceZ) {
    return vec3(64.0,-48.0,-32.0)*flashlightParams.w*(flashlightShadowParams.x>0.0?1.0:smoothstep(64.0,384.0,distanceZ));
}
vec3 sprintathonFlashlight(float surfaceAngle) {
    if (flashlightParams.x <= 0.0) return vec3(0.0);
    vec3 eye = flashlightEye;
    float distanceZ = max(-eye.z,0.0);
    // A small offset gives nearby walls parallax. Fade it near the camera
    // to limit corner leaks without a shadow pass.
    vec3 origin=sprintathonFlashlightOrigin(distanceZ);
    vec3 delta=eye-origin;
    vec3 axis=normalize(vec3(flashlightDepth.zw,-1.0));
    float along=dot(delta,axis);
    vec3 n = cross(dFdx(eye),dFdy(eye));
    float normalLength = length(n);
    float incidence = normalLength > 0.000001 ? abs(dot(n/normalLength,normalize(-delta))) : 1.0;
    incidence = mix(1.0,0.2+0.8*incidence,surfaceAngle);
    float radius = length(delta-axis*along) /
        max(48.0+max(along,0.0)*flashlightParams.y,0.0001);
    float edge = 1.0-smoothstep(0.85,1.0,radius);
    float rim = exp(-pow((radius-0.73)/0.09,2.0));
    float centre = 0.52*exp(-radius*radius*2.5);
    float rings = 0.035*cos(radius*46.0)*smoothstep(0.15,0.35,radius);
    float profile = max(0.0,centre+0.42*rim+0.12+rings)*edge*step(0.0,along);
    float distance = length(delta);
    float attenuation = (1.0-smoothstep(flashlightParams.z*0.65,flashlightParams.z,distance)) /
        (1.0+pow(distance/max(flashlightParams.z*0.25,1.0),2.0));
    return vec3(1.0,0.96,0.88)*flashlightParams.x*profile*attenuation*incidence*sprintathonFlashlightVisibility(eye);
}
)GLSL"

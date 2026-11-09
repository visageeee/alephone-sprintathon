R"GLSL(
// Use the rasterized surface's perspective-correct eye position. Window depth
// includes renderer depth offsets and depends on the active render viewport.
varying vec3 flashlightEye;
uniform vec4 flashlightParams; // brightness, tan(half angle), range, offset multiplier
uniform vec4 flashlightProjection; // lamp x, lamp y, radius, lamp z
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
vec3 sprintathonFlashlightOrigin() {
    // Projection-positioned lamp, shared with fog and the shadow pass.
    return vec3(flashlightProjection.xy,flashlightProjection.w);
}
// Straight cone from the below-camera lamp: radius increases linearly with distance.
float sprintathonFlashlightBeamRadius(float along) {
    return max(flashlightProjection.z+max(along,0.0)*flashlightParams.y,0.0001);
}
vec3 sprintathonFlashlight(float surfaceAngle) {
    if (flashlightParams.x <= 0.0) return vec3(0.0);
    vec3 eye = flashlightEye;
    vec3 origin=sprintathonFlashlightOrigin();
    vec3 delta=eye-origin;
    vec3 axis=normalize(vec3(flashlightDepth.zw,-1.0));
    float along=dot(delta,axis);
    vec3 n = cross(dFdx(eye),dFdy(eye));
    float normalLength = length(n);
    float incidence = normalLength > 0.000001 ? abs(dot(n/normalLength,normalize(-delta))) : 1.0;
    incidence = mix(1.0,0.2+0.8*incidence,surfaceAngle);
    float radius = length(delta-axis*along) /
        sprintathonFlashlightBeamRadius(along);
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
// Perturb the lighting incidence only. The visibility and shadow calculation
// in sprintathonFlashlight continues to use the geometric surface position.
vec3 sprintathonFlashlightMapped(float surfaceAngle, vec2 uv, vec3 encodedNormal, float detailWeight,
                                 float contrast, float specularWeight, out vec3 specular) {
    specular = vec3(0.0);
    vec3 lighting = sprintathonFlashlight(surfaceAngle);
    if (flashlightParams.x <= 0.0 || surfaceAngle <= 0.0) return lighting;
    vec3 eye = flashlightEye;
    // Compute derivatives before per-fragment branches.
    vec3 dx = dFdx(eye), dy = dFdy(eye);
    vec2 ux = dFdx(uv), uy = dFdy(uv);
    // The engine's fallback bump texture is exactly (128,128,255).
    if (detailWeight <= 0.0 || (abs(encodedNormal.r-128.0/255.0)<0.0001 &&
        abs(encodedNormal.g-128.0/255.0)<0.0001 && encodedNormal.b>0.9999)) return lighting;
    float determinant = ux.x*uy.y-ux.y*uy.x;
    vec3 geometric = cross(dx,dy);
    float geometricLength = length(geometric);
    // A fixed determinant cutoff incorrectly rejects valid UV bases when a
    // texture covers only a small fraction of the screen. Test the angle
    // between derivatives instead; the result is independent of UV scale.
    float uvScale = length(ux)*length(uy);
    if (uvScale<1.0e-20 || abs(determinant)<uvScale*0.00001 || geometricLength<0.000001) return lighting;
    geometric /= geometricLength;
    vec3 tangent = (dx*uy.y-dy*ux.y)/determinant;
    vec3 bitangent = (dy*ux.x-dx*uy.x)/determinant;
    tangent -= geometric*dot(geometric,tangent);
    float tangentLength=length(tangent), bitangentLength=length(bitangent);
    if(tangentLength<0.000001 || bitangentLength<0.000001) return lighting;
    tangent /= tangentLength;
    float handedness = dot(cross(geometric,tangent),bitangent)<0.0 ? -1.0 : 1.0;
    bitangent = cross(geometric,tangent)*handedness;
    vec3 sampled = encodedNormal*2.0-1.0;
    vec3 mapped = tangent*sampled.x+bitangent*sampled.y+geometric*sampled.z;
    float mappedLength = length(mapped);
    if(mappedLength<0.000001) return lighting;
    vec3 direction = normalize(sprintathonFlashlightOrigin()-eye);
    float oldIncidence = abs(dot(geometric,direction));
    float newIncidence = abs(dot(mapped/mappedLength,direction));
    float oldFactor = mix(1.0,0.2+0.8*oldIncidence,surfaceAngle);
    float newFactor = mix(1.0,0.2+0.8*newIncidence,surfaceAngle);
    // Generated height-from-colour normals can have a subtle effect with a
    // near-camera flashlight. Expand only their directional response, leaving
    // geometric visibility, shadow bias and other lights unchanged. Authored
    // normals pass contrast=1 and retain their original response.
    float mappedRatio = newFactor/oldFactor;
    float response = max(0.0,1.0+(mappedRatio-1.0)*contrast);
    if(specularWeight>0.0) {
        // A broad, restrained dielectric highlight, in eye space. Reuse the
        // flashlight radiance and geometric shadowing already computed above;
        // perturbed normals never alter shadow visibility or shadow bias.
        vec3 normal = mapped/mappedLength;
        normal *= dot(geometric,direction)<0.0 ? -1.0 : 1.0;
        vec3 viewDirection = normalize(-eye);
        vec3 halfSum = direction+viewDirection;
        float halfLength=length(halfSum);
        if(halfLength>0.000001) {
            float facing=max(dot(normal,direction),0.0);
            float highlight=pow(max(dot(normal,halfSum/halfLength),0.0),20.0)*facing;
            specular=lighting*(specularWeight*highlight*clamp(detailWeight,0.0,1.0)/oldFactor);
        }
    }
    return mix(lighting,lighting*response,clamp(detailWeight,0.0,1.0));
}
)GLSL"

R"FBOUNCE(
uniform sampler2DRect flashlightBounceMap;
uniform mat4 flashlightBounceView;
uniform vec4 flashlightBounceProjection;
uniform vec2 flashlightBounceSize;
uniform float flashlightBounceEnabled;
// Quadratic reconstruction kernel: continuous across texel boundaries,
// with a compact 3x3 footprint and no negative weights or ringing.
float sprintathonBounceWeight(float distance){
    float x=abs(distance);
    return x<0.5?0.75-x*x:0.5*pow(max(1.5-x,0.0),2.0);
}
vec3 sprintathonBounceLight(vec3 world){
    if(flashlightBounceEnabled<0.5)return vec3(0.0);
    vec3 p=(flashlightBounceView*vec4(world,1.0)).xyz;
    // Evaluate derivatives before per-fragment rejection.
    vec3 normal=cross(dFdx(p),dFdy(p));
    normal/=max(length(normal),0.00001);
    if(p.z>=-8.0)return vec3(0.0);
    vec2 uv=((p.xy/-p.z)*flashlightBounceProjection.xy-flashlightBounceProjection.zw)*0.5+0.5;
    if(uv.x<=0.0 || uv.y<=0.0 || uv.x>=1.0 || uv.y>=1.0)return vec3(0.0);
    vec2 pixel=uv*flashlightBounceSize,base=floor(pixel)+0.5;
    vec3 total=vec3(0.0);float weights=0.0;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
        vec2 q=base+vec2(float(x),float(y));
        if(q.x<0.5 || q.y<0.5 || q.x>flashlightBounceSize.x-0.5 || q.y>flashlightBounceSize.y-0.5)continue;
        vec4 light=texture2DRect(flashlightBounceMap,q);
        if(light.a<=0.0)continue;
        vec3 ray=vec3((q/flashlightBounceSize*2.0-1.0+flashlightBounceProjection.zw)/flashlightBounceProjection.xy,-1.0);
        // Compare against the receiver plane, not constant camera depth:
        // steep floors must remain smooth without mixing foreground walls.
        float denominator=dot(normal,ray);
        if(abs(denominator)<0.0001)continue;
        float expected=dot(normal,p)/denominator;
        float error=abs(light.a-expected);
        float depthWeight=1.0-smoothstep(8.0,24.0,error);
        vec2 delta=q-pixel;
        float weight=sprintathonBounceWeight(delta.x)*sprintathonBounceWeight(delta.y)*depthWeight;
        total+=light.rgb*weight;weights+=weight;
    }
    // Filtering only irradiance preserves the original material's sharpness.
    return weights>0.00001?total/weights:vec3(0.0);
}
)FBOUNCE"

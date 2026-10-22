R"(

varying vec3 flashlightEye;
uniform float depth;
varying vec3 viewXY;
varying vec3 viewDir;
varying float worldZ;
varying vec4 vertexColor;
varying vec3 sprintathonWorldPosition;
varying float classicDepth;

#if defined(SPRINTATHON_VERTEX_LIGHTS) && !defined(SPRINTATHON_NO_DYNAMIC_LIGHTS)
varying vec3 sprintathonVertexLighting;
uniform vec4 sprintathonLightPosition, sprintathonLightColor;
uniform vec4 sprintathonMuzzlePosition, sprintathonMuzzleColor;
uniform vec4 sprintathonLiquidLightSettings;
uniform float sprintathonSurfaceLightCount;
uniform vec4 sprintathonLightPosition2, sprintathonLightColor2, sprintathonLightPlane2;
uniform vec4 sprintathonLightPosition3, sprintathonLightColor3, sprintathonLightPlane3;
uniform vec4 sprintathonLightPosition4, sprintathonLightColor4, sprintathonLightPlane4;
uniform vec4 sprintathonLightPosition5, sprintathonLightColor5, sprintathonLightPlane5;

vec3 sprintathonVertexSource(vec4 position,vec4 color,vec4 plane) {
    if(color.a<=0.0)return vec3(0.0);
    vec3 delta=(gl_Vertex.xyz-position.xyz)*position.w;
    float distance2=dot(delta,delta);
    float falloff=max(0.0,1.0-distance2);
    float facing=smoothstep(-32.0,96.0,dot(gl_Vertex.xyz,plane.xyz)+plane.w);
    float gain=color.a>=2.0 && color.a<10.0?sprintathonLiquidLightSettings.x:1.0;
    // Preserve attenuation, but evaluate it per vertex rather than per pixel.
    return color.rgb*(falloff*falloff/(1.0+16.0*distance2)*facing*gain);
}
vec3 sprintathonVertexFlash(vec4 position,vec4 color) {
    if(color.a<=0.0)return vec3(0.0);
    vec3 delta=(gl_Vertex.xyz-position.xyz)*position.w;
    float falloff=max(0.0,1.0-dot(delta,delta));
    return color.rgb*(falloff*falloff);
}
#endif

void main(void) {
    flashlightEye = (gl_ModelViewMatrix * gl_Vertex).xyz;
	gl_Position  = gl_ModelViewProjectionMatrix * gl_Vertex;
	gl_Position.z = gl_Position.z + depth*gl_Position.z/65536.0;
	classicDepth = gl_Position.z / 8192.0;
#ifndef DISABLE_CLIP_VERTEX
	gl_ClipVertex = gl_ModelViewMatrix * gl_Vertex;
#endif
	gl_TexCoord[0] = gl_TextureMatrix[0] * gl_MultiTexCoord0;
	/* SETUP TBN MATRIX in normal matrix coords, gl_MultiTexCoord1 = tangent vector */
	vec3 n = normalize(gl_NormalMatrix * gl_Normal);
	vec3 t = normalize(gl_NormalMatrix * gl_MultiTexCoord1.xyz);
	vec3 b = normalize(cross(n, t) * gl_MultiTexCoord1.w);
	/* (column wise) */
	mat3 tbnMatrix = mat3(t.x, b.x, n.x, t.y, b.y, n.y, t.z, b.z, n.z);

	/* SETUP VIEW DIRECTION in unprojected local coords */
	viewDir = tbnMatrix * (gl_ModelViewMatrix * gl_Vertex).xyz;
	viewXY = -(gl_TextureMatrix[0] * vec4(viewDir.xyz, 1.0)).xyz;
	viewDir = -viewDir;
	worldZ = gl_Vertex.z;
	vertexColor = gl_Color;
	sprintathonWorldPosition = gl_Vertex.xyz;
#if defined(SPRINTATHON_VERTEX_LIGHTS) && !defined(SPRINTATHON_NO_DYNAMIC_LIGHTS)
    sprintathonVertexLighting=sprintathonVertexFlash(sprintathonLightPosition,sprintathonLightColor)+
        sprintathonVertexFlash(sprintathonMuzzlePosition,sprintathonMuzzleColor);
    if(sprintathonSurfaceLightCount>0.0)sprintathonVertexLighting+=sprintathonVertexSource(sprintathonLightPosition2,sprintathonLightColor2,sprintathonLightPlane2);
    if(sprintathonSurfaceLightCount>1.0)sprintathonVertexLighting+=sprintathonVertexSource(sprintathonLightPosition3,sprintathonLightColor3,sprintathonLightPlane3);
    if(sprintathonSurfaceLightCount>2.0)sprintathonVertexLighting+=sprintathonVertexSource(sprintathonLightPosition4,sprintathonLightColor4,sprintathonLightPlane4);
    if(sprintathonSurfaceLightCount>3.0)sprintathonVertexLighting+=sprintathonVertexSource(sprintathonLightPosition5,sprintathonLightColor5,sprintathonLightPlane5);
#endif
}

)"

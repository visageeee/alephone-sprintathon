/*
 OGL_SHADER.CPP
 
 Copyright (C) 2009 by Clemens Unterkofler and the Aleph One developers
 
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 3 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 This license is contained in the file "COPYING",
 which is included with this source code; it is available online at
 http://www.gnu.org/licenses/gpl.html
 
 Implements OpenGL vertex/fragment shader class
 */
#include <algorithm>
#include <iostream>

#include "OGL_Shader.h"
#include "FileHandler.h"
#include "OGL_Setup.h"
#include "InfoTree.h"
#include "Logging.h"

#ifdef HAVE_OPENGL

// gl_clipvertex puts Radeons into software mode on Mac
#if (defined(__APPLE__) && defined(__MACH__))
static bool DisableClipVertex()
{
    const GLubyte* renderer = glGetString(GL_RENDERER);
    return (renderer && strncmp(reinterpret_cast<const char*>(renderer), "AMD", 3) == 0);
}
#else
static bool DisableClipVertex()
{
    return false;
}
#endif


static std::map<std::string, std::string> defaultVertexPrograms;
static std::map<std::string, std::string> defaultFragmentPrograms;
void initDefaultPrograms();

std::vector<Shader> Shader::_shaders;

const char* Shader::_uniform_names[NUMBER_OF_UNIFORM_LOCATIONS] = 
{
	"texture0",
	"texture1",
	"texture2",
	"texture3",
	"time",
	"pulsate",
	"wobble",
	"flare",
	"bloomScale",
	"bloomShift",
	"repeat",
	"offsetx",
	"offsety",
	"pass",
	"fogMix",
	"visibility",
    "transferFadeOut",
	"depth",
	"strictDepthMode",
	"glow",
	"landscapeInverseMatrix",
	"scalex",
	"scaley",
	"yaw",
	"pitch",
	"sunAzimuth",
	"sunElevation",
	"selfLuminosity",
	"gammaAdjust",
	"logicalWidth",
	"logicalHeight",
	"pixelWidth",
	"pixelHeight",
	"fogMode",
	"mediaFogEnabled",
	"mediaFogTop",
	"mediaFogSoftness",
	"objectWorldZ",
	"mediaRipple",
	"mediaWetness",
	"sprintathonLightPosition",
	"sprintathonLightColor"
};

const char* Shader::_shader_names[NUMBER_OF_SHADER_TYPES] = 
{
	"error",
    "blur",
	"underwater_ripple",
	"ambient_occlusion",
	"ambient_occlusion_composite",
	"fog_haze",
	"landscape_light_shafts",
	"anamorphic_lens_flare",
	"bloom",
	"landscape",
	"landscape_bloom",
	"landscape_infravision",
	"sprite",
	"sprite_bloom",
	"sprite_infravision",
	"sprite_shadow",
	"invincible",
	"invincible_bloom",
	"invisible",
	"invisible_bloom",
	"wall",
	"wall_bloom",
	"wall_infravision",
	"bump",
	"bump_bloom",
	"gamma",
	"landscape_sphere",
	"landscape_sphere_bloom",
	"landscape_sphere_infravision"
};


class Shader_MML_Parser {
public:
	static void reset();
	static void parse(const InfoTree& root);
};

void Shader_MML_Parser::reset()
{
	Shader::_shaders.clear();
}

void Shader_MML_Parser::parse(const InfoTree& root)
{
	std::string name;
	if (!root.read_attr("name", name))
		return;
	
	for (int i = 0; i < Shader::NUMBER_OF_SHADER_TYPES; ++i) {
		if (name == Shader::_shader_names[i]) {
			initDefaultPrograms();
			Shader::loadAll();
			
			FileSpecifier vert, frag;
			root.read_path("vert", vert);
			root.read_path("frag", frag);
			int16 passes;
			root.read_attr("passes", passes);
			
			Shader::_shaders[i] = Shader(name, vert, frag, passes);
			break;
		}
	}
}

void reset_mml_opengl_shader()
{
	Shader_MML_Parser::reset();
}

void parse_mml_opengl_shader(const InfoTree& root)
{
	Shader_MML_Parser::parse(root);
}

void parseFile(FileSpecifier& fileSpec, std::string& s) {

	s.clear();

	if (fileSpec == FileSpecifier() || !fileSpec.Exists()) {
		return;
	}

	OpenedFile file;
	if (!fileSpec.Open(file))
	{
		fprintf(stderr, "%s not found\n", fileSpec.GetPath());
		return;
	}

	int32 length;
	file.GetLength(length);

	s.resize(length);
	file.Read(length, &s[0]);
}


GLhandleARB parseShader(const GLcharARB* str, GLenum shaderType) {

	GLint status;
	GLhandleARB shader = glCreateShaderObjectARB(shaderType);

	std::vector<const GLcharARB*> source;

        if (DisableClipVertex()) {
            source.push_back("#define DISABLE_CLIP_VERTEX\n");
        }
	if (Wanting_sRGB)
	{
		source.push_back("#define GAMMA_CORRECTED_BLENDING\n");
	}
	if (Bloom_sRGB)
	{
		source.push_back("#define BLOOM_SRGB_FRAMEBUFFER\n");
	}
	source.push_back(str);

	glShaderSourceARB(shader, source.size(), &source[0], NULL);

	glCompileShaderARB(shader);
	glGetObjectParameterivARB(shader, GL_OBJECT_COMPILE_STATUS_ARB, &status);

	if(status) {
		return shader;
	} else {
        GLint infoLen = 0;
        glGetShaderiv((GLuint)(size_t)shader, GL_INFO_LOG_LENGTH, &infoLen);
        
        if(infoLen > 1)
        {
            char* infoLog = (char*) malloc(sizeof(char) * infoLen);
            glGetShaderInfoLog((GLuint)(size_t)shader, infoLen, NULL, infoLog);
            logError("Error compiling shader:\n%s\n", infoLog);
            free(infoLog);
        }
        
		glDeleteObjectARB(shader);
		return 0;
	}
}

void Shader::loadAll() {
	initDefaultPrograms();
	if (!_shaders.size()) 
	{
		_shaders.reserve(NUMBER_OF_SHADER_TYPES);
		for (int i = 0; i < NUMBER_OF_SHADER_TYPES; ++i) 
		{
			_shaders.push_back(Shader(_shader_names[i]));
		}
	}
}

void Shader::unloadAll() {
	for (int i = 0; i < _shaders.size(); ++i) 
	{
		_shaders[i].unload();
	}
}

Shader::Shader(const std::string& name) : _programObj(0), _passes(-1), _loaded(false) {
    initDefaultPrograms();
    if (defaultVertexPrograms.count(name) > 0) {
	    _vert = defaultVertexPrograms[name];
    }
    if (defaultFragmentPrograms.count(name) > 0) {
	    _frag = defaultFragmentPrograms[name];
    }
}    

Shader::Shader(const std::string& name, FileSpecifier& vert, FileSpecifier& frag, int16& passes) : _programObj(0), _passes(passes), _loaded(false) {
	initDefaultPrograms();
	
	parseFile(vert,  _vert);
	if (_vert.empty() && defaultVertexPrograms.count(name) > 0) 
	{
		_vert = defaultVertexPrograms[name];
	}
	
	parseFile(frag, _frag);
	if (_frag.empty() && defaultFragmentPrograms.count(name) > 0) 
	{
		_frag = defaultFragmentPrograms[name];
	}
}

void Shader::init() {

	std::fill_n(_uniform_locations, static_cast<int>(NUMBER_OF_UNIFORM_LOCATIONS), -1);
	std::fill_n(_cached_floats, static_cast<int>(NUMBER_OF_UNIFORM_LOCATIONS), 0.0);

	_loaded = true;

	_programObj = glCreateProgramObjectARB();

	assert(!_vert.empty());
	GLhandleARB vertexShader = parseShader(_vert.c_str(), GL_VERTEX_SHADER_ARB);
    if(!vertexShader) {
        _vert = defaultVertexPrograms["error"];
        vertexShader = parseShader(_vert.c_str(), GL_VERTEX_SHADER_ARB);
    }
	
	glAttachObjectARB(_programObj, vertexShader);
	glDeleteObjectARB(vertexShader);

	assert(!_frag.empty());
	GLhandleARB fragmentShader = parseShader(_frag.c_str(), GL_FRAGMENT_SHADER_ARB);
	if(!fragmentShader) {
        _frag = defaultFragmentPrograms["error"];
        fragmentShader = parseShader(_frag.c_str(), GL_FRAGMENT_SHADER_ARB);
    }
    
	glAttachObjectARB(_programObj, fragmentShader);
	glDeleteObjectARB(fragmentShader);
	
	glLinkProgramARB(_programObj);
    
    GLint linked;
    glGetProgramiv((GLuint)(size_t)_programObj, GL_LINK_STATUS, &linked);
    if(!linked)
    {
      GLint infoLen = 0;
      glGetProgramiv((GLuint)(size_t)_programObj, GL_INFO_LOG_LENGTH, &infoLen);
      if(infoLen > 1)
      {
        char* infoLog = (char*) malloc(sizeof(char) * infoLen);
        glGetProgramInfoLog((GLuint)(size_t)_programObj, infoLen, NULL, infoLog);
        logError("Error linking program:\n%s\n", infoLog);
        free(infoLog);
      }
      glDeleteProgram((GLuint)(size_t)_programObj);
    }

	assert(_programObj);

	glUseProgramObjectARB(_programObj);

	glUniform1iARB(getUniformLocation(U_Texture0), 0);
	glUniform1iARB(getUniformLocation(U_Texture1), 1);
	glUniform1iARB(getUniformLocation(U_Texture2), 2);
	glUniform1iARB(getUniformLocation(U_Texture3), 3);	

	glUseProgramObjectARB(0);

//	assert(glGetError() == GL_NO_ERROR);
}

void Shader::setFloat(UniformName name, float f) {

	if (_cached_floats[name] != f) {
		_cached_floats[name] = f;
		glUniform1fARB(getUniformLocation(name), f);
	}
}

void Shader::setVector4(UniformName name, float x, float y, float z, float w) {
    glUniform4fARB(getUniformLocation(name), x, y, z, w);
}

void Shader::setMatrix4(UniformName name, float *f) {

	glUniformMatrix4fvARB(getUniformLocation(name), 1, false, f);
}

Shader::~Shader() {
	unload();
}

void Shader::enable() {
	if(!_loaded) { init(); }
	glUseProgramObjectARB(_programObj);
}

void Shader::disable() {
	glUseProgramObjectARB(0);
}

void Shader::unload() {
	if(_programObj) {
		glDeleteObjectARB(_programObj);
		_programObj = 0;
		_loaded = false;
	}
}

int16 Shader::passes() {
	return _passes;
}

void initDefaultPrograms() {
    if (defaultVertexPrograms.size() > 0)
        return;
    
    
    defaultVertexPrograms["error"] = ""
    "varying vec4 vertexColor;\n"
    "void main(void) {\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
    "    vertexColor = vec4(1.0, 1.0, 0.0, 1.0);\n"
    "}\n";
    defaultFragmentPrograms["error"] = ""
    "float round(float n){ \n"
    "   float nSign = 1.0; \n"
    "   if ( n < 0.0 ) { nSign = -1.0; }; \n"
    "   return nSign * floor(abs(n)+0.5); \n"
    "} \n"
    "void main (void) {\n"
    "    gl_FragColor = vec4(1.0, 1.0, 0.0, 1.0);\n"
    "    float checkerSize = 8.0;\n"
    "    float phase = 0.0;\n"
    "    if( mod(round(gl_FragCoord.y / checkerSize), 2.0) == 0.0) {\n"
    "       phase = checkerSize;\n"
    "    }\n"
    "    if (mod(round((gl_FragCoord.x + phase) / checkerSize), 2.0)==0.0) {\n"
    "       gl_FragColor.a = 0.5;\n"
    "    }\n"
    "}\n";
    
	defaultVertexPrograms["gamma"] = ""
	"varying vec4 vertexColor;\n"
	"void main(void) {\n"
	"	gl_TexCoord[0] = gl_MultiTexCoord0;\n"
	"	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
	"	vertexColor = gl_Color;\n"
	"}\n";
	defaultFragmentPrograms["gamma"] = ""
	"uniform sampler2DRect texture0;\n"
	"uniform float gammaAdjust;\n"
	"void main (void) {\n"
	"	vec4 color0 = texture2DRect(texture0, gl_TexCoord[0].xy);\n"
	"	gl_FragColor = vec4(pow(color0.r, gammaAdjust), pow(color0.g, gammaAdjust), pow(color0.b, gammaAdjust), 1.0);\n"
	"}\n";
	
    defaultVertexPrograms["blur"] = ""
        "varying vec4 vertexColor;\n"
        "void main(void) {\n"
        "	gl_TexCoord[0] = gl_MultiTexCoord0;\n"
        "	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
        "	vertexColor = gl_Color;\n"
        "}\n";
    defaultFragmentPrograms["blur"] = ""
        "uniform sampler2DRect texture0;\n"
        "uniform float offsetx;\n"
        "uniform float offsety;\n"
        "uniform float pass;\n"
        "varying vec4 vertexColor;\n"
        "const float f0 = 0.14012035;\n"
        "const float f1 = 0.24122258;\n"
        "const float o1 = 1.45387071;\n"
        "const float f2 = 0.13265595;\n"
        "const float o2 = 3.39370426;\n"
        "const float f3 = 0.04518872;\n"
        "const float o3 = 5.33659787;\n"
        "#ifdef BLOOM_SRGB_FRAMEBUFFER\n"
        "vec3 s2l(vec3 srgb) { return srgb; }\n"
        "vec3 l2s(vec3 linear) { return linear; }\n"
        "#else\n"
        "vec3 s2l(vec3 srgb) { return srgb * srgb; }\n"
        "vec3 l2s(vec3 linear) { return sqrt(linear); }\n"
        "#endif\n"
        "void main (void) {\n"
        "	vec2 s = vec2(offsetx, offsety);\n"
        "	// Thanks to Renaud Bedard - http://theinstructionlimit.com/?p=43\n"
        "	vec3 c = s2l(texture2DRect(texture0, gl_TexCoord[0].xy).rgb);\n"
        "	vec3 t = f0 * c;\n"
        "	t += f1 * s2l(texture2DRect(texture0, gl_TexCoord[0].xy - o1*s).rgb);\n"
        "	t += f1 * s2l(texture2DRect(texture0, gl_TexCoord[0].xy + o1*s).rgb);\n"
        "	t += f2 * s2l(texture2DRect(texture0, gl_TexCoord[0].xy - o2*s).rgb);\n"
        "	t += f2 * s2l(texture2DRect(texture0, gl_TexCoord[0].xy + o2*s).rgb);\n"
        "	t += f3 * s2l(texture2DRect(texture0, gl_TexCoord[0].xy - o3*s).rgb);\n"
        "	t += f3 * s2l(texture2DRect(texture0, gl_TexCoord[0].xy + o3*s).rgb);\n"
        "	gl_FragColor = vec4(l2s(t), 1.0) * vertexColor;\n"
        "}\n";    
    
	defaultVertexPrograms["underwater_ripple"] = ""
		"void main(void) {\n"
		"\tgl_TexCoord[0] = gl_MultiTexCoord0;\n"
		"\tgl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
		"}\n";
	defaultFragmentPrograms["underwater_ripple"] = ""
		"uniform sampler2DRect texture0;\n"
		"uniform float time;\n"
		"uniform float pixelWidth;\n"
		"uniform float pixelHeight;\n"
		"void main(void) {\n"
		"\tvec2 p = gl_TexCoord[0].xy;\n"
		"\tvec2 uv = p / vec2(pixelWidth, pixelHeight);\n"
		"\tfloat edge = smoothstep(0.0, 0.08, uv.x) *\n"
		"\t\tsmoothstep(0.0, 0.08, 1.0 - uv.x) *\n"
		"\t\tsmoothstep(0.0, 0.08, uv.y) *\n"
		"\t\tsmoothstep(0.0, 0.08, 1.0 - uv.y);\n"
		"\tvec2 wave = vec2(\n"
		"\t\tsin(uv.y * 31.0 + time * 2.0) + 0.45 * sin(uv.y * 73.0 - time),\n"
		"\t\tcos(uv.x * 27.0 - time * 2.0) + 0.40 * sin((uv.x + uv.y) * 49.0 + time));\n"
		"\tvec2 warped = p + wave * vec2(2.6, 1.9) * edge;\n"
		"\twarped = clamp(warped, vec2(0.5),\n"
		"\t\tvec2(pixelWidth - 0.5, pixelHeight - 0.5));\n"
		"\tgl_FragColor = texture2DRect(texture0, warped);\n"
		"}\n";

	defaultVertexPrograms["ambient_occlusion"] = defaultVertexPrograms["underwater_ripple"];
defaultFragmentPrograms["ambient_occlusion"] = R"(
uniform sampler2DRect texture2;
uniform float pixelWidth;
uniform float pixelHeight;
uniform float scalex;
uniform float scaley;

float viewDepth(float windowDepth) {
	float ndcDepth = windowDepth * 2.0 - 1.0;
	return abs(scaley / (ndcDepth + scalex));
}

float creaseOcclusion(vec2 p, float centerDepth, vec2 offset) {
	vec2 minimumPixel = vec2(0.5);
	vec2 maximumPixel = vec2(pixelWidth - 0.5, pixelHeight - 0.5);
	float positiveDepth = texture2DRect(texture2,
		clamp(p + offset, minimumPixel, maximumPixel)).r;
	float negativeDepth = texture2DRect(texture2,
		clamp(p - offset, minimumPixel, maximumPixel)).r;
	// Landscapes render at the clear/far depth. They neither receive AO nor
	// contribute as occluders, preventing dark outlines against the sky.
	if (positiveDepth >= 0.9995 || negativeDepth >= 0.9995)
		return 0.0;
	float centerDistance = viewDepth(centerDepth);
	float inverseCenterDistance = 1.0 / max(centerDistance, 0.0001);
	float positiveDelta = (centerDistance - viewDepth(positiveDepth)) *
		inverseCenterDistance;
	float negativeDelta = (centerDistance - viewDepth(negativeDepth)) *
		inverseCenterDistance;

	// Opposite samples on a flat surface have equal and opposite depth slopes,
	// so their signed sum cancels. At a concave junction that sum becomes
	// positive even when perspective makes one of the two samples slightly
	// farther away. This is a depth-curvature test rather than a contact shadow.
	float concavity = max(positiveDelta + negativeDelta, 0.0);
	float crease = smoothstep(0.0007, 0.018, concavity);

	// Reject the large depth steps produced by sprites and surfaces crossing in
	// front of one another, even if a second edge happens to be nearby.
	float continuousSurface = 1.0 - smoothstep(0.055, 0.13,
		max(abs(positiveDelta), abs(negativeDelta)));
	return crease * continuousSurface;
}

void main(void) {
	// This pass renders into a half-size viewport while sampling full-size
	// depth. Derive the source coordinate per fragment to avoid the diagonal
	// interpolation seam seen with compatibility-profile quad coordinates.
	vec2 p = gl_FragCoord.xy * 2.0;
	float centerDepth = texture2DRect(texture2, p).r;
	if (centerDepth >= 0.9995) {
		gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}

	// Grow the screen-space radius near the camera so the sampled area remains
	// approximately stable in world space instead of collapsing at close range.
	float centerDistance = viewDepth(centerDepth);
	float radiusScale = clamp(2048.0 / max(centerDistance, 1.0), 0.70, 3.25);
	float ao = 0.0;
	ao += creaseOcclusion(p, centerDepth, vec2(4.0, 0.0) * radiusScale);
	ao += creaseOcclusion(p, centerDepth, vec2(0.0, 4.0) * radiusScale);
	ao += creaseOcclusion(p, centerDepth, vec2(3.0, 3.0) * radiusScale);
	ao += creaseOcclusion(p, centerDepth, vec2(3.0, -3.0) * radiusScale);
	// Four opposing pairs cover horizontal, vertical, and diagonal junctions.
	// The half-resolution mask makes these eight reads substantially cheaper.
	ao = clamp(ao / 1.10, 0.0, 1.0);
	gl_FragColor = vec4(ao, ao, ao, 1.0);
}
)";

	defaultVertexPrograms["ambient_occlusion_composite"] =
		defaultVertexPrograms["underwater_ripple"];
	defaultFragmentPrograms["ambient_occlusion_composite"] = R"(
uniform sampler2DRect texture0;
uniform sampler2DRect texture1;
uniform sampler2DRect texture2;
uniform float pixelWidth;
uniform float pixelHeight;
uniform float scalex;
uniform float scaley;
uniform float logicalWidth;
uniform float logicalHeight;
uniform float bloomScale;
uniform float fogMode;

float viewDepth(float windowDepth) {
	float ndcDepth = windowDepth * 2.0 - 1.0;
	return abs(scaley / (ndcDepth + scalex));
}

float fogVisibility(float distance) {
	if (fogMode == 0.0)
		return clamp((gl_Fog.end - distance) /
			(gl_Fog.end - gl_Fog.start), 0.0, 1.0);
	if (fogMode == 1.0)
		return clamp(exp(-gl_Fog.density * distance), 0.0, 1.0);
	if (fogMode == 2.0)
		return clamp(exp(-gl_Fog.density * gl_Fog.density *
			distance * distance), 0.0, 1.0);
	return 1.0;
}

vec2 aoBlurTap(vec2 p, vec2 maskPoint, vec2 offset,
	float centerDistance, float spatialWeight) {
	vec2 depthPoint = clamp(p + offset * 2.0, vec2(0.5),
		vec2(pixelWidth - 0.5, pixelHeight - 0.5));
	float sampleDepth = texture2DRect(texture2, depthPoint).r;
	if (sampleDepth >= 0.9995)
		return vec2(0.0);
	float relativeDifference = abs(viewDepth(sampleDepth) -
		centerDistance) / max(centerDistance, 0.0001);
	float weight = spatialWeight * (1.0 - smoothstep(0.018, 0.095,
		relativeDifference));
	return vec2(texture2DRect(texture1, maskPoint + offset).r * weight,
		weight);
}

float blurredAO(vec2 p, float centerDistance) {
	vec2 maskPoint = p * 0.5;
	vec2 accumulated = vec2(
		texture2DRect(texture1, maskPoint).r * 7.0, 7.0);
	accumulated += aoBlurTap(p, maskPoint, vec2( 0.65,  0.0), centerDistance, 2.0);
	accumulated += aoBlurTap(p, maskPoint, vec2(-0.65,  0.0), centerDistance, 2.0);
	accumulated += aoBlurTap(p, maskPoint, vec2( 0.0,  0.65), centerDistance, 2.0);
	accumulated += aoBlurTap(p, maskPoint, vec2( 0.0, -0.65), centerDistance, 2.0);
	accumulated += aoBlurTap(p, maskPoint, vec2( 0.90,  0.90), centerDistance, 1.0);
	accumulated += aoBlurTap(p, maskPoint, vec2(-0.90,  0.90), centerDistance, 1.0);
	accumulated += aoBlurTap(p, maskPoint, vec2( 0.90, -0.90), centerDistance, 1.0);
	accumulated += aoBlurTap(p, maskPoint, vec2(-0.90, -0.90), centerDistance, 1.0);
	return accumulated.x / max(accumulated.y, 0.0001);
}

void main(void) {
	// Screen-space coordinates are exact here and cannot disagree across the
	// two triangles used to draw the fullscreen compatibility quad.
	vec2 p = gl_FragCoord.xy;
	vec4 scene = texture2DRect(texture0, p);
	float centerDepth = texture2DRect(texture2, p).r;
	if (centerDepth >= 0.9995) {
		gl_FragColor = scene;
		return;
	}

	float axialDistance = viewDepth(centerDepth);
	vec2 ndc = p / vec2(pixelWidth, pixelHeight) * 2.0 - 1.0;
	float radialDistance = axialDistance * sqrt(1.0 +
		(ndc.x * ndc.x) / max(logicalWidth * logicalWidth, 0.0001) +
		(ndc.y * ndc.y) / max(logicalHeight * logicalHeight, 0.0001));
	float visibility = fogVisibility(radialDistance);
	// AO disappears before the underlying material is fully swallowed by fog.
	float fogFade = visibility * visibility;
	float ao = blurredAO(p, axialDistance);
	float shade = 1.0 - ao * 0.78 * bloomScale * fogFade;
	gl_FragColor = vec4(scene.rgb * shade, scene.a);
}
)";

	defaultVertexPrograms["fog_haze"] =
		defaultVertexPrograms["underwater_ripple"];
defaultFragmentPrograms["fog_haze"] = R"(
uniform sampler2DRect texture0;
uniform sampler2DRect texture2;
uniform float pixelWidth;
uniform float pixelHeight;
uniform float scalex;
uniform float scaley;
uniform float time;
uniform float fogMode;
uniform float bloomScale;
uniform float bloomShift;
uniform float mediaRipple;
uniform float mediaWetness;

float viewDepth(float windowDepth) {
	float ndcDepth = windowDepth * 2.0 - 1.0;
	return abs(scaley / (ndcDepth + scalex));
}

float fogVisibility(float distance) {
	if (fogMode == 0.0)
		return clamp((gl_Fog.end - distance) /
			(gl_Fog.end - gl_Fog.start), 0.0, 1.0);
	if (fogMode == 1.0)
		return clamp(exp(-gl_Fog.density * distance), 0.0, 1.0);
	if (fogMode == 2.0)
		return clamp(exp(-gl_Fog.density * gl_Fog.density *
			distance * distance), 0.0, 1.0);
	return 1.0;
}

float fogCloudHash(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

float periodicFogNoise(vec2 p, float period) {
	vec2 cell = floor(p);
	vec2 blend = fract(p);
	blend = blend * blend * (3.0 - 2.0 * blend);
	float lowerLeft = fogCloudHash(mod(cell, period));
	float lowerRight = fogCloudHash(mod(cell + vec2(1.0, 0.0), period));
	float upperLeft = fogCloudHash(mod(cell + vec2(0.0, 1.0), period));
	float upperRight = fogCloudHash(mod(cell + vec2(1.0, 1.0), period));
	return mix(mix(lowerLeft, lowerRight, blend.x),
		mix(upperLeft, upperRight, blend.x), blend.y);
}

void main(void) {
	vec2 p = gl_TexCoord[0].xy;
	vec4 scene = texture2DRect(texture0, p);
	float centerDepth = texture2DRect(texture2, p).r;
	// Clear/far depth is the landscape. Keep skies perfectly stable.
	if (centerDepth >= 0.9995 || fogMode < 0.0) {
		gl_FragColor = scene;
		return;
	}

	float sceneDistance = viewDepth(centerDepth);
	float visibility = fogVisibility(sceneDistance);
	float fogAmount = 1.0 - visibility;
	// Begin well inside the fog rather than making the whole view swim.
	float haze = smoothstep(0.30, 0.78, fogAmount) * bloomScale *
		mediaRipple;
	// Two broad, incommensurate waves avoid a visibly repeating scroll.
	vec2 uv = p / vec2(pixelWidth, pixelHeight);
	vec2 wave = vec2(
		sin(uv.y * 39.0 + time * 2.0) +
			0.38 * sin((uv.x + uv.y) * 83.0 - time),
		cos(uv.x * 43.0 - time * 2.0) +
			0.34 * sin((uv.x - uv.y) * 71.0 + time));
	// Integer time multipliers make every component meet exactly when the
	// CPU-side phase wraps at two pi, avoiding a periodic animation jump.
	vec2 warped = clamp(p + wave * vec2(1.15, 0.85) * haze,
		vec2(0.5), vec2(pixelWidth - 0.5, pixelHeight - 0.5));

	// Do not pull foreground silhouettes into the fog at depth boundaries.
	float warpedDepth = texture2DRect(texture2, warped).r;
	float relativeDepthDifference = abs(viewDepth(warpedDepth) -
		sceneDistance) / max(sceneDistance, 0.0001);
	float sameSurface = 1.0 - smoothstep(0.025, 0.12,
		relativeDepthDifference);
	vec4 distorted = texture2DRect(texture0, warped);
	vec4 result = mix(scene, distorted, haze * sameSurface);

	// Animated Density adds irregular banks of fog that rise through the view.
	// Scene depth offsets the noise domain, so the fog occupies the world rather
	// than reading as horizontal bands painted across the screen. The noise
	// layer travels by exactly one lattice period per wrapped CPU phase, keeping
	// the animation seamless without expensive trigonometry. A single smooth
	// noise field keeps the depth variation while halving the hash work of the
	// previous two-layer version.
	if (mediaWetness > 0.5) {
		const float inverseTwoPi = 0.159154943;
		float cycle = time * inverseTwoPi;
		float depthLayer = fogAmount * 5.0;
		vec2 broadPosition = vec2(
			uv.x * 4.0 + depthLayer * 0.83,
			uv.y * 3.0 + depthLayer * 1.17 - cycle * 8.0);
		float broadClouds = periodicFogNoise(broadPosition, 8.0);
		float clouds = smoothstep(0.34, 0.68, broadClouds);
		float cloudDepth = smoothstep(0.18, 0.82, fogAmount) *
			bloomScale;
		float addedFog = clouds * cloudDepth * 0.20 * bloomShift;
		result.rgb = mix(result.rgb, gl_Fog.color.rgb, addedFog);
	}
	gl_FragColor = result;
}
)";

	defaultVertexPrograms["landscape_light_shafts"] =
		defaultVertexPrograms["underwater_ripple"];
defaultFragmentPrograms["landscape_light_shafts"] = R"(
uniform sampler2DRect texture0;
uniform sampler2DRect texture1;
uniform sampler2DRect texture3;
uniform float pixelWidth;
uniform float pixelHeight;
uniform float offsetx;
uniform float offsety;
uniform float repeat;
uniform float bloomScale;
uniform float bloomShift;
uniform float logicalWidth;
uniform float logicalHeight;
uniform float yaw;
uniform float pitch;
uniform float sunAzimuth;
uniform float sunElevation;

void main(void) {
	vec2 p = gl_FragCoord.xy;
	vec4 scene = texture2DRect(texture0, p);
	// The configured world-space sun remains stable while the camera turns.
	float relativeAzimuth = sunAzimuth - yaw;
	float horizontalLength = cos(sunElevation);
	vec3 sunDirection = vec3(
		horizontalLength * sin(relativeAzimuth),
		sin(sunElevation),
		horizontalLength * cos(relativeAzimuth));

	// Rotate the fixed direction by camera pitch, then project it with the same
	// horizontal and vertical scales used for world geometry.
	float cosinePitch = cos(pitch);
	float sinePitch = sin(pitch);
	float viewY = sunDirection.y * cosinePitch -
		sunDirection.z * sinePitch;
	float viewZ = sunDirection.y * sinePitch +
		sunDirection.z * cosinePitch;
	float safeViewZ = max(viewZ, 0.08);
	vec2 sunNdc = vec2(
		sunDirection.x * logicalWidth / safeViewZ,
		viewY * logicalHeight / safeViewZ);
	// Preserve the true projected direction even when the sun is far outside
	// the framebuffer. Component-wise clamping to an off-screen rectangle made
	// the apparent shaft angle rotate as each axis reached the clamp separately.
	vec2 sourceDimensions = vec2(offsetx, offsety);
	vec2 sourceNormalized = vec2(0.5) + sunNdc * (0.5 / repeat);
	vec2 source = sourceNormalized * sourceDimensions;
	// Keep the atmospheric scattering visible around and somewhat beyond the
	// camera's side plane. It should not vanish as soon as the sun leaves view.
	float frontFade = smoothstep(-0.75, 0.05, viewZ);
	// A high sun can project a long way above the framebuffer when looking down.
	// Fade only at truly extreme projections rather than near the screen edge.
	float offscreenFade = 1.0 - smoothstep(24.0, 64.0, length(sunNdc));
	float sunVisibility = frontFade * offscreenFade;
	// More closely spaced samples avoid the visible stair-stepping produced
	// when long shafts stretched only a couple of dozen landscape samples. Cap
	// the traced distance so a far-offscreen sun does not make each tap leap over
	// most of the visible landscape.
	vec2 sourceP = (p / vec2(pixelWidth, pixelHeight) - vec2(0.5)) /
		repeat + vec2(0.5);
	sourceP *= sourceDimensions;
	vec2 rayVector = source - sourceP;
	float rayDistance = length(rayVector);
	vec2 rayDirection = rayVector / max(rayDistance, 0.001);
	float maximumTraceDistance = max(sourceDimensions.x,
		sourceDimensions.y) * 1.35;
	float desiredTraceDistance = min(rayDistance, maximumTraceDistance) *
		bloomShift;
	// Fit all taps between this fragment and the framebuffer boundary. This
	// keeps a constant sample count and smoothly reduces spacing near an edge,
	// instead of losing taps in discrete bands or repeating the outermost row.
	vec2 maximumCoordinate = sourceDimensions - vec2(0.5);
	float distanceToVerticalEdge = 1000000.0;
	if (rayDirection.x > 0.0001)
		distanceToVerticalEdge = (maximumCoordinate.x - sourceP.x) / rayDirection.x;
	else if (rayDirection.x < -0.0001)
		distanceToVerticalEdge = (0.5 - sourceP.x) / rayDirection.x;
	float distanceToHorizontalEdge = 1000000.0;
	if (rayDirection.y > 0.0001)
		distanceToHorizontalEdge = (maximumCoordinate.y - sourceP.y) / rayDirection.y;
	else if (rayDirection.y < -0.0001)
		distanceToHorizontalEdge = (0.5 - sourceP.y) / rayDirection.y;
	float availableTraceDistance = max(0.0,
		min(distanceToVerticalEdge, distanceToHorizontalEdge) - 2.0);
	float traceDistance = min(desiredTraceDistance, availableTraceDistance);
	vec2 stepVector = rayDirection * (traceDistance / 48.0);
	vec2 q = sourceP;
	vec3 shafts = vec3(0.0);
	float weight = 1.0;
	float totalWeight = 0.0;
	for (int i = 0; i < 48; ++i) {
		q += stepVector;
		vec2 samplePosition = clamp(q, vec2(0.5), maximumCoordinate);
		float sampleDepth = texture2DRect(texture3, samplePosition).r;
		vec3 sampleColor = texture2DRect(texture1, samplePosition).rgb;
		float peak = max(sampleColor.r, max(sampleColor.g, sampleColor.b));
		// Only far-depth landscape pixels emit shafts. Use peak channel energy
		// rather than luminance so saturated blue, red, purple and green skies
		// are not penalized compared with white landscapes.
		float landscape = step(0.9995, sampleDepth);
		float emission = landscape * smoothstep(0.01, 0.12, peak);
		// Preserve the landscape hue while lifting darker source textures into a
		// useful shaft colour. Truly black clear-depth pixels remain rejected.
		vec3 hue = sampleColor / max(peak, 0.001);
		vec3 emitterColor = mix(sampleColor, hue * 0.48, 0.78);
		shafts += emitterColor * emission * weight;
		totalWeight += weight;
		// Equivalent falloff to the former 24-tap pass, distributed across twice
		// as many samples so the appearance remains consistent but smoother.
		weight *= 0.9721;
	}
	shafts /= max(totalWeight, 0.0001);
	// Keep the additive result restrained; strength remains user-controlled.
	gl_FragColor = vec4(scene.rgb + shafts * bloomScale * 1.15 *
		sunVisibility, scene.a);
}
)";

	defaultVertexPrograms["anamorphic_lens_flare"] =
		defaultVertexPrograms["underwater_ripple"];
	defaultFragmentPrograms["anamorphic_lens_flare"] = R"(
uniform sampler2DRect texture0;
uniform sampler2DRect texture2;
uniform float pixelWidth;
uniform float pixelHeight;
uniform float bloomScale;
uniform float scalex;
uniform float scaley;
uniform float fogMode;

float viewDepth(float windowDepth) {
	float ndcDepth = windowDepth * 2.0 - 1.0;
	return abs(scaley / (ndcDepth + scalex));
}

float fogVisibility(float distance) {
	if (fogMode == 0.0)
		return clamp((gl_Fog.end - distance) /
			(gl_Fog.end - gl_Fog.start), 0.0, 1.0);
	if (fogMode == 1.0)
		return clamp(exp(-gl_Fog.density * distance), 0.0, 1.0);
	if (fogMode == 2.0)
		return clamp(exp(-gl_Fog.density * gl_Fog.density *
			distance * distance), 0.0, 1.0);
	return 1.0;
}

void main(void) {
	vec2 p = gl_FragCoord.xy;
	vec2 dimensions = vec2(pixelWidth, pixelHeight);
	vec4 scene = texture2DRect(texture0, p);
	vec3 flare = vec3(0.0);
	float totalWeight = 0.0;
	// A broad horizontal convolution produces an anamorphic streak. Only very
	// bright world pixels with real geometry depth may emit, excluding skies.
	for (int i = -16; i <= 16; ++i) {
		float tap = float(i) / 16.0;
		vec2 q = p + vec2(tap * pixelWidth * 0.22, 0.0);
		q = clamp(q, vec2(0.5), dimensions - vec2(0.5));
		vec3 sampleColor = texture2DRect(texture0, q).rgb;
		float sampleDepth = texture2DRect(texture2, q).r;
		float peak = max(sampleColor.r, max(sampleColor.g, sampleColor.b));
		float luminance = dot(sampleColor, vec3(0.2126, 0.7152, 0.0722));
		// Peak-channel energy allows strongly coloured lamps, projectiles and
		// glowing sprites to emit just as readily as white textures. A small
		// luminance contribution keeps pale lights smooth without desaturating
		// the resulting streak.
		float brightness = max(peak, luminance * 1.15);
		float visibility = fogVisibility(viewDepth(sampleDepth));
		// A source must remain substantially clearer than the surrounding fog.
		// This prevents bright fog colours from masquerading as emitters and
		// removes real flares smoothly before their lights disappear into haze.
		float clearOfFog = smoothstep(0.55, 0.90, visibility);
		float emitter = smoothstep(0.42, 0.88, brightness) *
			(1.0 - step(0.9995, sampleDepth)) * clearOfFog;
		// Looking directly toward the lamp matters: emitters fade rapidly as they
		// leave the central portion of the view.
		vec2 centered = q / dimensions - vec2(0.5);
		float facing = 1.0 - smoothstep(0.08, 0.48, length(centered));
		float weight = exp(-abs(tap) * 3.4);
		vec3 hue = sampleColor / max(peak, 0.001);
		// Retain the source hue. The slight cool lift is strongest on neutral
		// lights and nearly absent from already-saturated coloured emitters.
		float saturation = peak - min(sampleColor.r,
			min(sampleColor.g, sampleColor.b));
		float neutral = 1.0 - smoothstep(0.08, 0.45, saturation);
		vec3 tint = mix(hue * peak,
			vec3(0.68, 0.84, 1.0) * peak, neutral * 0.28);
		flare += tint * emitter * facing * weight;
		totalWeight += weight;
	}
	flare /= max(totalWeight, 0.0001);
	// A thin blue-white streak with a restrained additive response preserves the
	// original lamp and avoids turning every bright wall into a bloom cloud.
	float verticalCore = exp(-abs(p.y - pixelHeight * 0.5) /
		max(pixelHeight * 0.12, 1.0));
	flare *= mix(0.62, 1.0, verticalCore);
	gl_FragColor = vec4(scene.rgb + flare * bloomScale * 2.2, scene.a);
}
)";

    defaultVertexPrograms["bloom"] = ""
        "varying vec4 vertexColor;\n"
        "void main(void) {\n"
        "	gl_TexCoord[0] = gl_MultiTexCoord0;\n"
        "	gl_TexCoord[1] = gl_MultiTexCoord1;\n"
        "	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
        "	vertexColor = gl_Color;\n"
        "}\n";
    defaultFragmentPrograms["bloom"] = ""
        "uniform sampler2DRect texture0;\n"
        "uniform sampler2DRect texture1;\n"
        "uniform float pass;\n"
        "varying vec4 vertexColor;\n"
        "vec3 s2l(vec3 srgb) { return srgb * srgb; }\n"
        "vec3 l2s(vec3 linear) { return sqrt(linear); }\n"
		"#ifndef BLOOM_SRGB_FRAMEBUFFER\n"
	    "vec3 b2l(vec3 bloom) { return bloom * bloom; }\n"
		"#else\n"
		"vec3 b2l(vec3 bloom) { return bloom; }\n"
        "#endif\n"
        "void main (void) {\n"
        "	vec4 color0 = texture2DRect(texture0, gl_TexCoord[0].xy);\n"
        "	vec4 color1 = texture2DRect(texture1, gl_TexCoord[1].xy);\n"
        "	vec3 color = l2s(s2l(color0.rgb) + b2l(color1.rgb));\n"
        "	gl_FragColor = vec4(color, 1.0);\n"
        "}\n";

	defaultVertexPrograms["landscape"] =
        #include "Shaders/landscape.vert"
		;
	defaultFragmentPrograms["landscape"] =
		#include "Shaders/landscape.frag"
		;
	
    defaultVertexPrograms["landscape_bloom"] = defaultVertexPrograms["landscape"];
	defaultFragmentPrograms["landscape_bloom"] =
		#include "Shaders/landscape_bloom.frag"
		;
	
	defaultVertexPrograms["landscape_infravision"] = defaultVertexPrograms["landscape"];
	defaultFragmentPrograms["landscape_infravision"] =
        #include "Shaders/landscape_infravision.frag"
		;

	defaultVertexPrograms["sprite"] =
        #include "Shaders/sprite.vert"
		;
	defaultFragmentPrograms["sprite"] =
        #include "Shaders/sprite.frag"
		;
	
    defaultVertexPrograms["sprite_bloom"] = defaultVertexPrograms["sprite"];
	defaultFragmentPrograms["sprite_bloom"] =
		#include "Shaders/sprite_bloom.frag"
		;

	defaultVertexPrograms["sprite_infravision"] = defaultVertexPrograms["sprite"];
	defaultFragmentPrograms["sprite_infravision"] =
        #include "Shaders/sprite_infravision.frag"
		;

	defaultVertexPrograms["sprite_shadow"] = defaultVertexPrograms["sprite"];
	defaultFragmentPrograms["sprite_shadow"] = R"(
uniform float fogMode;
uniform float mediaFogEnabled;
uniform float mediaFogTop;
uniform float mediaFogSoftness;
varying vec3 viewDir;
varying float worldZ;
varying vec4 vertexColor;

float getFogFactor(float distance) {
	if (fogMode == 0.0)
		return clamp((gl_Fog.end - distance) /
			(gl_Fog.end - gl_Fog.start), 0.0, 1.0);
	if (fogMode == 1.0)
		return clamp(exp(-gl_Fog.density * distance), 0.0, 1.0);
	if (fogMode == 2.0)
		return clamp(exp(-gl_Fog.density * gl_Fog.density *
			distance * distance), 0.0, 1.0);
	return 1.0;
}

void main(void) {
	vec2 blob = (gl_TexCoord[0].xy - vec2(0.5)) * 2.0;
	float radiusSquared = dot(blob, blob);
	if (radiusSquared >= 1.0)
		discard;
	float fogFactor = getFogFactor(length(viewDir));
	if (mediaFogEnabled > 0.0) {
		float heightFog = clamp((mediaFogTop - worldZ) /
			mediaFogSoftness, 0.0, 1.0);
		fogFactor = 1.0 - (1.0 - fogFactor) *
			mix(1.0, heightFog, mediaFogEnabled);
	}
	float softenedMask = 1.0 - smoothstep(0.38, 1.0, radiusSquared);
	gl_FragColor = vec4(0.0, 0.0, 0.0,
		softenedMask * vertexColor.a * fogFactor);
}
)";
	
    defaultVertexPrograms["invincible"] = defaultVertexPrograms["sprite"];
	defaultFragmentPrograms["invincible"] =
		#include "Shaders/invincible.frag"
		;
	
    defaultVertexPrograms["invincible_bloom"] = defaultVertexPrograms["invincible"];
	defaultFragmentPrograms["invincible_bloom"] =
        #include "Shaders/invincible_bloom.frag"
		;

    defaultVertexPrograms["invisible"] = defaultVertexPrograms["sprite"];
	defaultFragmentPrograms["invisible"] =
        #include "Shaders/invisible.frag"
		;
    defaultVertexPrograms["invisible_bloom"] = defaultVertexPrograms["invisible"];
	defaultFragmentPrograms["invisible_bloom"] =
        #include "Shaders/invisible_bloom.frag"
		;

	defaultVertexPrograms["wall"] =
        #include "Shaders/wall.vert"
		;
	defaultFragmentPrograms["wall"] =
        #include "Shaders/wall.frag"
		;
	
    defaultVertexPrograms["wall_bloom"] = defaultVertexPrograms["wall"];
	defaultFragmentPrograms["wall_bloom"] =
		#include "Shaders/wall_bloom.frag"
		;
	
	defaultVertexPrograms["wall_infravision"] = defaultVertexPrograms["wall"];
	defaultFragmentPrograms["wall_infravision"] =
        #include "Shaders/wall_infravision.frag"
		;
    
    defaultVertexPrograms["bump"] = defaultVertexPrograms["wall"];
	defaultFragmentPrograms["bump"] =
        #include "Shaders/bump.frag"
		;
	
    defaultVertexPrograms["bump_bloom"] = defaultVertexPrograms["bump"];
    defaultFragmentPrograms["bump_bloom"] =
        #include "Shaders/bump_bloom.frag"
		;

	defaultVertexPrograms["landscape_sphere"] =
		#include "Shaders/landscape_sphere.vert"
	;

	defaultFragmentPrograms["landscape_sphere"] =
		#include "Shaders/landscape_sphere.frag"
	;

	defaultVertexPrograms["landscape_sphere_bloom"] = defaultVertexPrograms["landscape_sphere"];
	defaultFragmentPrograms["landscape_sphere_bloom"] =
		#include "Shaders/landscape_sphere_bloom.frag"
	;

	defaultVertexPrograms["landscape_sphere_infravision"] = defaultVertexPrograms["landscape_sphere"];
	defaultFragmentPrograms["landscape_sphere_infravision"] =
		#include "Shaders/landscape_sphere_infravision.frag"
	;
}

#endif

#ifndef _OGL_SHADER_
#define _OGL_SHADER_
/*
 OGL_SHADER.H
 
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

#include <string>
#include <map>
#include "OGL_Headers.h"
#include "FileHandler.h"

#ifdef HAVE_OPENGL

class Shader {

friend class XML_ShaderParser;
friend class Shader_MML_Parser;
public:
	enum UniformName {
		U_Texture0,
		U_Texture1,
		U_Texture2,
		U_Texture3,
		U_Time,
		U_Pulsate,
		U_Wobble,
		U_Flare,
		U_BloomScale,
		U_BloomShift,
		U_Repeat,
		U_OffsetX,
		U_OffsetY,
		U_Pass,
		U_FogMix,
		U_Visibility,
        U_TransferFadeOut,
		U_Depth,
		U_StrictDepthMode,
		U_Glow,
		U_LandscapeInverseMatrix,
		U_ScaleX,
		U_ScaleY,
		U_Yaw,
		U_Pitch,
		U_SunAzimuth,
		U_SunElevation,
		U_SelfLuminosity,
		U_GammaAdjust,
		U_LogicalWidth,
		U_LogicalHeight,
		U_PixelWidth,
		U_PixelHeight,
		U_FogMode,
		U_MediaFogEnabled,
		U_MediaFogTop,
		U_MediaFogSoftness,
		U_ObjectWorldZ,
		U_MediaRipple,
		U_MediaWetness,
		U_SprintathonLightPosition,
		U_SprintathonLightColor,
		U_SprintathonLightPosition2,
		U_SprintathonLightColor2,
		U_SprintathonLightPosition3,
		U_SprintathonLightColor3,
		U_SprintathonLightPosition4,
		U_SprintathonLightColor4,
		U_SprintathonLightPosition5,
		U_SprintathonLightColor5,
		U_SprintathonLightPosition6,
		U_SprintathonLightColor6,
		U_SprintathonLightPosition7,
		U_SprintathonLightColor7,
		U_SprintathonLightPosition8,
		U_SprintathonLightColor8,
		U_SprintathonLightPosition9,
		U_SprintathonLightColor9,
		U_SprintathonLightPosition10,
		U_SprintathonLightColor10,
		U_SprintathonLightPosition11,
		U_SprintathonLightColor11,
		U_SprintathonLightPosition12,
		U_SprintathonLightColor12,
		U_SprintathonLightPosition13,
		U_SprintathonLightColor13,
		U_SprintathonLightPosition14,
		U_SprintathonLightColor14,
		U_SprintathonLightPosition15,
		U_SprintathonLightColor15,
		U_SprintathonLightPosition16,
		U_SprintathonLightColor16,
		U_SprintathonLightPosition17,
		U_SprintathonLightColor17,
		U_SprintathonLightPosition18,
		U_SprintathonLightColor18,
		U_SprintathonLightPosition19,
		U_SprintathonLightColor19,
		U_SprintathonLightPosition20,
		U_SprintathonLightColor20,
		U_SprintathonLightPosition21,
		U_SprintathonLightColor21,
		U_SprintathonSectorEdge0,
		U_SprintathonSectorEdge1,
		U_SprintathonSectorEdge2,
		U_SprintathonSectorEdge3,
		U_SprintathonSectorEdge4,
		U_SprintathonSectorEdge5,
		U_SprintathonSectorEdge6,
		U_SprintathonSectorEdge7,
		U_SprintathonSectorSpan0,
		U_SprintathonSectorSpan1,
		U_SprintathonSectorSpan2,
		U_SprintathonSectorSpan3,
		U_SprintathonSectorSpan4,
		U_SprintathonSectorSpan5,
		U_SprintathonSectorSpan6,
		U_SprintathonSectorSpan7,
		U_SprintathonMuzzlePosition,
		U_SprintathonMuzzleColor,
		U_SprintathonShaftSource,
		NUMBER_OF_UNIFORM_LOCATIONS
	};

	enum ShaderType {
		S_Error,
        S_Blur,
		S_UnderwaterRipple,
		S_AmbientOcclusion,
		S_AmbientOcclusionComposite,
		S_FogHaze,
		S_LandscapeLightShafts,
		S_AnamorphicLensFlare,
		S_Bloom,
		S_Landscape,
		S_LandscapeBloom,
		S_LandscapeInfravision,
		S_Sprite,
		S_SpriteBloom,
		S_SpriteInfravision,
		S_SpriteShadow,
		S_Invincible,
		S_InvincibleBloom,
		S_Invisible,
		S_InvisibleBloom,
		S_Wall,
		S_WallBloom,
		S_WallInfravision,
		S_Bump,
		S_BumpBloom,
		S_Gamma,
		S_LandscapeSphere,
		S_LandscapeSphereBloom,
		S_LandscapeSphereInfravision,
		S_LandscapeLightShaftsComposite,
		NUMBER_OF_SHADER_TYPES
	};
private:

	GLhandleARB _programObj;
	std::string _vert;
	std::string _frag;
	int16 _passes;
	bool _loaded;

	static const char* _shader_names[NUMBER_OF_SHADER_TYPES];
	static std::vector<Shader> _shaders;

	static const char* _uniform_names[NUMBER_OF_UNIFORM_LOCATIONS];
	GLint _uniform_locations[NUMBER_OF_UNIFORM_LOCATIONS];
	float _cached_floats[NUMBER_OF_UNIFORM_LOCATIONS];
	float _cached_vectors[NUMBER_OF_UNIFORM_LOCATIONS][4];

	GLint getUniformLocation(UniformName name) { 
		if (_uniform_locations[name] == -1) {
			_uniform_locations[name] = glGetUniformLocationARB(_programObj, _uniform_names[name]);
		}
		return _uniform_locations[name];
	}
	
public:

	static Shader* get(ShaderType type) { return &_shaders[type]; }
	static void loadAll();
	static void unloadAll();
	
	Shader() : _programObj(0), _passes(-1), _loaded(false) {}
	Shader(const std::string& name);
	Shader(const std::string& name, FileSpecifier& vert, FileSpecifier& frag, int16& passes);
	~Shader();

	void load();
	void init();
	void enable();
	void unload();
	void setFloat(UniformName name, float); // shader must be enabled
	void setMatrix4(UniformName name, float *f);
	void setVector4(UniformName name, float x, float y, float z, float w);

	int16 passes();

	static void disable();
};


class InfoTree;
void parse_mml_opengl_shader(const InfoTree& root);
void reset_mml_opengl_shader();

#endif

#endif

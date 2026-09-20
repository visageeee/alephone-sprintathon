/*
 *  RenderRasterize_Shader.cpp
 *  Created by Clemens Unterkofler on 1/20/09.
 *  for Aleph One
 *
 *  http://www.gnu.org/licenses/gpl.html
 */

#include "OGL_Headers.h"

#include <algorithm>
#include <iostream>

#include "RenderRasterize_Shader.h"

#include "lightsource.h"
#include "media.h"
#include "player.h"
#include "weapons.h"
#include "AnimatedTextures.h"
#include "OGL_Faders.h"
#include "OGL_Blitter.h"
#include "OGL_Textures.h"
#include "OGL_Shader.h"
#include "ChaseCam.h"
#include "preferences.h"
#include "screen.h"
#include "map.h"

#ifdef HAVE_OPENGL

#define MAXIMUM_VERTICES_PER_WORLD_POLYGON (MAXIMUM_VERTICES_PER_POLYGON+4)

class Blur {

private:
	FBOSwapper _swapper;
	Shader *_shader_blur;
	Shader *_shader_bloom;
	GLuint _width;
	GLuint _height;

public:

	Blur(GLuint w, GLuint h, Shader* s_blur, Shader* s_bloom)
	: _swapper(w, h, Bloom_sRGB), _shader_blur(s_blur), _shader_bloom(s_bloom), _width(w), _height(h) {}

	GLuint width() { return _width; }
	GLuint height() { return _height; }
	
	void begin() {
		_swapper.activate();
		glDisable(GL_FRAMEBUFFER_SRGB_EXT); // don't blend for initial
	}

	void end() {
		_swapper.swap();
	}

	void draw(FBOSwapper& dest) {
		
		int passes = _shader_bloom->passes();
		if (passes < 0)
			passes = 5;

		glBlendFunc(GL_SRC_ALPHA,GL_ONE);
		for (int i = 0; i < passes; i++) {
			_shader_blur->enable();
			_shader_blur->setFloat(Shader::U_OffsetX, 1);
			_shader_blur->setFloat(Shader::U_OffsetY, 0);
			_shader_blur->setFloat(Shader::U_Pass, i + 1);
			_swapper.filter(false);

			_shader_blur->setFloat(Shader::U_OffsetX, 0);
			_shader_blur->setFloat(Shader::U_OffsetY, 1);
			_shader_blur->setFloat(Shader::U_Pass, i + 1);
			_swapper.filter(false);

			_shader_bloom->enable();
			_shader_bloom->setFloat(Shader::U_Pass, i + 1);
//			if (Bloom_sRGB)
//				dest.blend(_swapper.current_contents(), true);
//			else
				dest.blend_multisample(_swapper.current_contents());
			
			Shader::disable();
		}
		
		glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	}
};


RenderRasterize_Shader::RenderRasterize_Shader() = default;
RenderRasterize_Shader::~RenderRasterize_Shader() = default;

/*
 * initialize some stuff
 * happens once after opengl, shaders and textures are setup
 */
void RenderRasterize_Shader::setupGL(Rasterizer_Shader_Class& Rasterizer) {

	RasPtr = &Rasterizer;

	Shader::loadAll();

	Shader* s_blur = Shader::get(Shader::S_Blur);
	Shader* s_bloom = Shader::get(Shader::S_Bloom);

	blur.reset();
	if(TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_Blur)) {
		if(s_blur && s_bloom) {
			blur.reset(new Blur(640., 640. * graphics_preferences->screen_mode.height / graphics_preferences->screen_mode.width, s_blur, s_bloom));
		}
	}
	
//	glDisable(GL_CULL_FACE);
//	glDisable(GL_LIGHTING);
}

/*
 * override for RenderRasterizerClass::render_tree()
 *
 * with multiple rendering passes for glow effect
 */
const double TWO_PI = 8*atan(1.0);
const float FixedAngleToRadians = TWO_PI/(float(FIXED_ONE)*float(FULL_CIRCLE));
const float FixedAngleToDegrees = 360.0/(float(FIXED_ONE)*float(FULL_CIRCLE));

static float sprintathon_underwater_phase(int16 media_type)
{
	const OGL_ConfigureData& config = Get_OGL_ConfigureData();
	int16 speed_index = config.AnimatedMediaRippleSpeed;
	switch (media_type)
	{
		case _media_lava: speed_index = config.AnimatedLavaRippleSpeed; break;
		case _media_goo: speed_index = config.AnimatedGooRippleSpeed; break;
		case _media_sewage: speed_index = config.AnimatedSewageRippleSpeed; break;
		case _media_jjaro: speed_index = config.AnimatedJjaroRippleSpeed; break;
		default: break;
	}

	static uint32 last_tick = machine_tick_count();
	static double phase = 0.0;
	const uint32 now = machine_tick_count();
	const uint32 elapsed_ticks = now - last_tick;
	if (elapsed_ticks > 0)
	{
		const double elapsed = std::min(
			static_cast<double>(elapsed_ticks) / MACHINE_TICKS_PER_SECOND,
			0.25);
		const double bullet_time_rate =
			sprintathon_bullet_time_active() ? 0.35 : 1.0;
		const double media_rate =
			(static_cast<double>(speed_index) + 1.0) * 0.25;
		phase = std::fmod(phase + elapsed * bullet_time_rate * media_rate,
			TWO_PI);
		last_tick = now;
	}
	return static_cast<float>(phase);
}

void RenderRasterize_Shader::render_tree() {

	weaponFlare = PIN(view->maximum_depth_intensity - NATURAL_LIGHT_INTENSITY, 0, FIXED_ONE)/float(FIXED_ONE);
	selfLuminosity = PIN(NATURAL_LIGHT_INTENSITY, 0, FIXED_ONE)/float(FIXED_ONE);

	Shader* s = Shader::get(Shader::S_Invincible);
	s->enable();
	s->setFloat(Shader::U_Time, view->tick_count);
	s->setFloat(Shader::U_LogicalWidth, view->screen_width);
	s->setFloat(Shader::U_LogicalHeight, view->screen_height);
	s->setFloat(Shader::U_PixelWidth, view->screen_width * MainScreenPixelScale());
	s->setFloat(Shader::U_PixelHeight, view->screen_height * MainScreenPixelScale());
	if (blur.get()) {
		s = Shader::get(Shader::S_InvincibleBloom);
		s->enable();
		s->setFloat(Shader::U_Time, view->tick_count);
		s->setFloat(Shader::U_LogicalWidth, view->screen_width);
		s->setFloat(Shader::U_LogicalHeight, view->screen_height);
		s->setFloat(Shader::U_PixelWidth, blur->width());
		s->setFloat(Shader::U_PixelHeight, blur->height());
	}

	short leftmost = INT16_MAX;
	short rightmost = INT16_MIN;
	vector<clipping_window_data>& windows = RSPtr->RVPtr->ClippingWindows;
	for (vector<clipping_window_data>::const_iterator it = windows.begin(); it != windows.end(); ++it) {
		if (it->x0 < leftmost) {
			leftmost = it->x0;
			leftmost_clip = it->left;
		}
		if (it->x1 > rightmost) {
			rightmost = it->x1;
			rightmost_clip = it->right;
		}
	}
	
	float fogMix = 0.0;
	auto fogdata = OGL_GetCurrFogData();
	if (fogdata && fogdata->IsPresent && fogdata->AffectsLandscapes) {
		fogMix = fogdata->LandscapeMix;
	}

	float fogmode = -1.0;
	if (fogdata) {
		fogmode = fogdata->Mode;
	}

	float media_fog_enabled = 0.0f;
	float media_fog_top = 0.0f;
	float media_fog_softness = static_cast<float>(WORLD_ONE) * 0.5f;
	const bool any_forced_fallback_fog = fogdata && !fogdata->IsPresent &&
		TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_ForceFog);
	const bool forced_fallback_fog = any_forced_fallback_fog &&
		Get_OGL_ConfigureData().ForceFogMediaRelative &&
		!(current_player->variables.flags & _HEAD_BELOW_MEDIA_BIT);
	if (forced_fallback_fog)
	{
		const media_data* selected_media = nullptr;
		// Use the highest media surface as one stable plane for the whole level.
		// A camera-relative choice would follow the player between vertically
		// stacked pools and prevent the player from ever entering the fog.
		for (size_t media_index = 0;
			media_index < MediaList.size();
			++media_index)
		{
			const media_data* candidate = get_media_data(media_index);
			if (candidate &&
				(!selected_media || candidate->height > selected_media->height))
			{
				selected_media = candidate;
			}
		}

		if (selected_media)
		{
			media_fog_top = static_cast<float>(
				selected_media->height + WORLD_ONE / 2);
			// Blend between global fog while immersed and height-limited fog
			// above the layer, rather than popping at the surface.
			const float camera_z = static_cast<float>(
				current_player->camera_location.z);
			const float transition_start =
				media_fog_top - media_fog_softness * 0.5f;
			float transition = A1_PIN(
				(camera_z - transition_start) / media_fog_softness,
				0.0f, 1.0f);
			transition = transition * transition *
				(3.0f - 2.0f * transition);
			media_fog_enabled = transition;
		}
	}

	// Landscapes cannot intersect a local height band. Mix them only while the
	// viewer is inside global fallback fog, or when media-relative fog is off.
	if (any_forced_fallback_fog)
	{
		static const float preset_landscape_mix[] = {0.20f, 0.55f, 0.35f, 0.40f};
		const int preset = std::max(0, std::min(3,
			static_cast<int>(Get_OGL_ConfigureData().ForceFogWeatherPreset)));
		const float global_fog_blend =
			Get_OGL_ConfigureData().ForceFogMediaRelative ?
				1.0f - media_fog_enabled : 1.0f;
		fogMix = preset_landscape_mix[preset] * global_fog_blend;
	}

	const float virtual_yaw = view->virtual_yaw * FixedAngleToRadians;
	const float virtual_pitch = view->virtual_pitch * FixedAngleToRadians;

	Shader* landscape_shaders[] = {
		Shader::get(Shader::S_Landscape),
		Shader::get(Shader::S_LandscapeBloom),
		Shader::get(Shader::S_LandscapeInfravision),
		Shader::get(Shader::S_LandscapeSphere),
		Shader::get(Shader::S_LandscapeSphereBloom),
		Shader::get(Shader::S_LandscapeSphereInfravision)
	};

	for (auto s : landscape_shaders) {
		s->enable();
		s->setFloat(Shader::U_FogMix, fogMix);
		s->setFloat(Shader::U_Yaw, virtual_yaw);
		s->setFloat(Shader::U_Pitch, view->mimic_sw_perspective ? 0.0 : virtual_pitch);
	}

	Shader* fog_mode_shaders[] = {
		Shader::get(Shader::S_Bump),
		Shader::get(Shader::S_BumpBloom),
		Shader::get(Shader::S_Invincible),
		Shader::get(Shader::S_InvincibleBloom),
		Shader::get(Shader::S_Invisible),
		Shader::get(Shader::S_InvisibleBloom),
		Shader::get(Shader::S_Wall),
		Shader::get(Shader::S_WallBloom),
		Shader::get(Shader::S_WallInfravision),
		Shader::get(Shader::S_Sprite),
		Shader::get(Shader::S_SpriteBloom),
		Shader::get(Shader::S_SpriteInfravision)
	};
	
	for (auto s : fog_mode_shaders) {
		s->enable();
		s->setFloat(Shader::U_FogMode, fogmode);
		s->setFloat(Shader::U_MediaFogEnabled, media_fog_enabled);
		s->setFloat(Shader::U_MediaFogTop, media_fog_top);
		s->setFloat(Shader::U_MediaFogSoftness, media_fog_softness);
	}
	
	Shader::disable();

	RenderRasterizerClass::render_tree(kDiffuse);
        render_viewer_sprite_layer(kDiffuse);

	if (current_player->infravision_duration == 0 &&
		TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_Blur) &&
		blur.get())
	{
		blur->begin();
		RenderRasterizerClass::render_tree(kGlow);
                render_viewer_sprite_layer(kGlow);
		blur->end();
		RasPtr->swapper->deactivate();
		blur->draw(*RasPtr->swapper);
		RasPtr->swapper->activate();
	}

	// Refract the completed 3D view while submerged. This runs before the HUD
	// is composited, so interface text and meters remain crisp.
	if (view->under_media_boundary && view->origin_polygon_index != NONE)
	{
		polygon_data *polygon = get_polygon_data(view->origin_polygon_index);
		if (polygon && polygon->media_index != NONE)
		{
			media_data *media = get_media_data(polygon->media_index);
			if (media)
			{
				Shader *underwater = Shader::get(Shader::S_UnderwaterRipple);
				// Finish the scene target before filtering it. At this point the
				// freshly rendered frame is still the swapper's draw target;
				// filter() samples current_contents(), which otherwise refers to
				// the previous (often black) buffer.
				RasPtr->swapper->swap();
				underwater->enable();
				underwater->setFloat(Shader::U_Time,
					sprintathon_underwater_phase(media->type));
				underwater->setFloat(Shader::U_PixelWidth,
					view->screen_width * MainScreenPixelScale());
				underwater->setFloat(Shader::U_PixelHeight,
					view->screen_height * MainScreenPixelScale());
				RasPtr->swapper->filter(false);
				Shader::disable();

				// Preserve the orientation expected by Rasterizer_Shader::End().
				// This unfiltered copy makes both sides contain the refracted
				// frame; End() can perform its normal final swap and presentation.
				RasPtr->swapper->filter(false);
			}
		}
	}

	glAlphaFunc(GL_GREATER, 0.5);
}

void RenderRasterize_Shader::render_node(sorted_node_data *node, bool SeeThruLiquids, RenderStep renderStep)
{
	// parasitic object detection
    objectCount = 0;
    objectY = 0;

    RenderRasterizerClass::render_node(node, SeeThruLiquids, renderStep);

	// turn off clipping planes
	glDisable(GL_CLIP_PLANE0);
	glDisable(GL_CLIP_PLANE1);
}

void RenderRasterize_Shader::clip_to_window(clipping_window_data *win)
{
    GLdouble clip[] = { 0., 0., 0., 0. };
        
    // recenter to player's orientation temporarily
    glPushMatrix();
    glTranslatef(view->origin.x, view->origin.y, 0.);
    glRotatef(view->yaw * (360/float(FULL_CIRCLE)) + 90., 0., 0., 1.);
    
    glRotatef(-0.1, 0., 0., 1.); // leave some excess to avoid artifacts at edges
	if (win->left.i != leftmost_clip.i || win->left.j != leftmost_clip.j) {
		clip[0] = win->left.i;
		clip[1] = win->left.j;
		glEnable(GL_CLIP_PLANE0);
		glClipPlane(GL_CLIP_PLANE0, clip);
	} else {
		glDisable(GL_CLIP_PLANE0);
	}
	
    glRotatef(0.2, 0., 0., 1.); // breathing room for right-hand clip
	if (win->right.i != rightmost_clip.i || win->right.j != rightmost_clip.j) {
		clip[0] = win->right.i;
		clip[1] = win->right.j;
		glEnable(GL_CLIP_PLANE1);
		glClipPlane(GL_CLIP_PLANE1, clip);
	} else {
		glDisable(GL_CLIP_PLANE1);
	}
    
    glPopMatrix();
}

void RenderRasterize_Shader::store_endpoint(
	endpoint_data *endpoint,
	long_vector2d& p)
{
	p.i = endpoint->vertex.x;
	p.j = endpoint->vertex.y;
}

std::unique_ptr<TextureManager> RenderRasterize_Shader::setupSpriteTexture(const rectangle_definition& rect, short type, float offset, RenderStep renderStep) {

	Shader *s = NULL;
	GLfloat color[3];
	GLdouble shade = PIN(static_cast<GLfloat>(rect.ambient_shade)/static_cast<GLfloat>(FIXED_ONE),0,1);
	color[0] = color[1] = color[2] = shade;

	auto TMgr = std::make_unique<TextureManager>();

	TMgr->ShapeDesc = rect.ShapeDesc;
	TMgr->LowLevelShape = rect.LowLevelShape;
	TMgr->ShadingTables = rect.shading_tables;
	TMgr->Texture = rect.texture;
	TMgr->TransferMode = rect.transfer_mode;
	TMgr->TransferData = rect.transfer_data;
	TMgr->IsShadeless = (rect.flags&_SHADELESS_BIT) != 0;
	TMgr->TextureType = type;

	if (current_player->infravision_duration) {
		struct bitmap_definition* dummy;
		// grab the normal shading tables, since the shader does the tinting
		extended_get_shape_bitmap_and_shading_table(GET_DESCRIPTOR_COLLECTION(TMgr->ShapeDesc), TMgr->LowLevelShape, &dummy, &TMgr->ShadingTables, _shading_normal);
	}

	float flare = weaponFlare;

	glEnable(GL_TEXTURE_2D);

	// priorities: static, infravision, tinted/solid, shadeless
	if (TMgr->TransferMode == _static_transfer) {
		TMgr->IsShadeless = 1;
		flare = -1;
		if (renderStep == kDiffuse) {
			s = Shader::get(Shader::S_Invincible);
		} else {
			s = Shader::get(Shader::S_InvincibleBloom);
		}
		s->enable();
        s->setFloat(Shader::U_TransferFadeOut,((float)((uint16)rect.transfer_data))/(float)((int)FIXED_ONE));
	} else if (current_player->infravision_duration) {
		color[0] = color[1] = color[2] = 1;
		FindInfravisionVersionRGBA(GET_COLLECTION(GET_DESCRIPTOR_COLLECTION(rect.ShapeDesc)), color);
		s = Shader::get(Shader::S_SpriteInfravision);
		s->enable();
	} else if (TMgr->TransferMode == _tinted_transfer) {
		flare = -1;
		if (renderStep == kDiffuse) {
			s = Shader::get(Shader::S_Invisible);
		} else {
			s = Shader::get(Shader::S_InvisibleBloom);
		}
		s->enable();
		s->setFloat(Shader::U_Visibility, 1.0 - rect.transfer_data/32.0f);
	} else if (TMgr->TransferMode == _solid_transfer) {
		// is this ever used?
		color[0] = 0;
		color[1] = 1;
		color[2] = 0;
	} else if (TMgr->TransferMode == _textured_transfer) {
		if (TMgr->IsShadeless) {
			if (renderStep == kDiffuse) {
				color[0] = color[1] = color[2] = 1;
			} else {
				color[0] = color[1] = color[2] = 0;
			}
			flare = -1;
		}
	} else {
		// I've never seen this happen
		color[0] = 0;
		color[1] = 0;
		color[2] = 1;
	}

	if(s == NULL) {
		if (renderStep == kDiffuse) {
			s = Shader::get(Shader::S_Sprite);
		} else {
			s = Shader::get(Shader::S_SpriteBloom);
		}
		s->enable();
	}

	if(TMgr->Setup()) {
		TMgr->RenderNormal();
	} else {
		TMgr->ShapeDesc = UNONE;
		return TMgr;
	}

	TMgr->SetupTextureMatrix();

	if (renderStep == kGlow) {
		s->setFloat(Shader::U_BloomScale, TMgr->BloomScale());
		s->setFloat(Shader::U_BloomShift, TMgr->BloomShift());
	}
	s->setFloat(Shader::U_Flare, flare);
	s->setFloat(Shader::U_SelfLuminosity, selfLuminosity);
	s->setFloat(Shader::U_Pulsate, 0);
	s->setFloat(Shader::U_Wobble, 0);
	s->setFloat(Shader::U_Depth, offset);
	s->setFloat(Shader::U_ObjectWorldZ,
		static_cast<float>(rect.Position.z));
	const bool sprintathon_strict_sprite_depth =
		!view->mimic_sw_perspective &&
		input_preferences->sprintathon_enabled &&
		input_preferences->sprintathon_mouselook_mode > 0;
	s->setFloat(Shader::U_StrictDepthMode,
		(OGL_ForceSpriteDepth() || sprintathon_strict_sprite_depth) ? 1 : 0);
	s->setFloat(Shader::U_Glow, 0);
	glColor4f(color[0], color[1], color[2], 1);
	return TMgr;
}

// Circle constants
const double Radian2Circle = 1/TWO_PI;			// A circle is 2*pi radians
const double FullCircleReciprocal = 1/double(FULL_CIRCLE);

std::unique_ptr<TextureManager> RenderRasterize_Shader::setupWallTexture(const shape_descriptor& Texture, short transferMode, float pulsate, float wobble, float intensity, float offset, RenderStep renderStep, int16 mediaType) {

	Shader *s = NULL;

	auto TMgr = std::make_unique<TextureManager>();
	LandscapeOptions *opts = NULL;
	TMgr->ShapeDesc = Texture;
	if (TMgr->ShapeDesc == UNONE) { return TMgr; }
	get_shape_bitmap_and_shading_table(Texture, &TMgr->Texture, &TMgr->ShadingTables, _shading_normal);

	TMgr->TransferMode = _textured_transfer;
	TMgr->IsShadeless = current_player->infravision_duration ? 1 : 0;
	TMgr->TransferData = 0;

	float flare = weaponFlare;

	glEnable(GL_TEXTURE_2D);
	glColor4f(intensity, intensity, intensity, 1.0);

	switch(transferMode) {
		case _xfer_static:
			TMgr->TextureType = OGL_Txtr_Wall;
			TMgr->TransferMode = _static_transfer;
			TMgr->IsShadeless = 1;
			flare = -1;
			s = Shader::get(renderStep == kGlow ? Shader::S_InvincibleBloom : Shader::S_Invincible);
			s->enable();
            s->setFloat(Shader::U_TransferFadeOut,0);
			break;
		case _xfer_landscape:
		case _xfer_big_landscape:
			TMgr->TextureType = OGL_Txtr_Landscape;
			TMgr->TransferMode = _big_landscaped_transfer;
			opts = View_GetLandscapeOptions(Texture);
			TMgr->LandscapeVertRepeat = opts->VertRepeat;
			TMgr->Landscape_AspRatExp = opts->SphereMap ? 1 : opts->OGL_AspRatExp;
			if (current_player->infravision_duration) {
				GLfloat color[3] {1, 1, 1};
				FindInfravisionVersionRGBA(GET_COLLECTION(GET_DESCRIPTOR_COLLECTION(Texture)), color);
				glColor4f(color[0], color[1], color[2], 1);
				if (opts->SphereMap)
				{
					s = Shader::get(Shader::S_LandscapeSphereInfravision);
				}
				else
				{
					s = Shader::get(Shader::S_LandscapeInfravision);
				}
			} else {
				if (opts->SphereMap)
				{
					if (renderStep == kDiffuse)
					{
						s = Shader::get(Shader::S_LandscapeSphere);
					}
					else
					{
						s = Shader::get(Shader::S_LandscapeSphereBloom);
					}
				}
				else
				{
					if (renderStep == kDiffuse) {
						s = Shader::get(Shader::S_Landscape);
					} else {
						s = Shader::get(Shader::S_LandscapeBloom);
					}
				}
			}
			s->enable();
			break;
		default:
			TMgr->TextureType = OGL_Txtr_Wall;
			if(TMgr->IsShadeless) {
				if (renderStep == kDiffuse) {
					glColor4f(1,1,1,1);
				} else {
					glColor4f(0,0,0,1);
				}
				flare = -1;
			}
	}

	if(s == NULL) {
		if (current_player->infravision_duration) {
			GLfloat color[3] {1, 1, 1};
			FindInfravisionVersionRGBA(GET_COLLECTION(GET_DESCRIPTOR_COLLECTION(Texture)), color);
			glColor4f(color[0], color[1], color[2], 1);
			s = Shader::get(Shader::S_WallInfravision);
		} else if(TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_BumpMap)) {
			s = Shader::get(renderStep == kGlow ? Shader::S_BumpBloom : Shader::S_Bump);
		} else {
			s = Shader::get(renderStep == kGlow ? Shader::S_WallBloom : Shader::S_Wall);
		}
		s->enable();
	}

	if(TMgr->Setup()) {
		TMgr->RenderNormal(); // must allocate first
		if (TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_BumpMap)) {
			glActiveTextureARB(GL_TEXTURE1_ARB);
			TMgr->RenderBump();
			glActiveTextureARB(GL_TEXTURE0_ARB);
		}
	} else {
		TMgr->ShapeDesc = UNONE;
		return TMgr;
	}

	TMgr->SetupTextureMatrix();
	const OGL_ConfigureData& config = Get_OGL_ConfigureData();
	int16 ripple_speed_index = config.AnimatedMediaRippleSpeed;
	switch (mediaType)
	{
		case _media_lava:
			ripple_speed_index = config.AnimatedLavaRippleSpeed;
			break;
		case _media_goo:
			ripple_speed_index = config.AnimatedGooRippleSpeed;
			break;
		case _media_sewage:
			ripple_speed_index = config.AnimatedSewageRippleSpeed;
			break;
		case _media_jjaro:
			ripple_speed_index = config.AnimatedJjaroRippleSpeed;
			break;
		default:
			break;
	}
	const float ripple_speed =
		(static_cast<float>(ripple_speed_index) + 1.0f) * 0.25f;
	// The temporal phase wraps at 2-pi. Shader time harmonics and spatial wave
	// cycles are integers, so both time and scrolling texture UVs wrap cleanly.
	// Accumulate phase instead of multiplying absolute wall-clock time. This
	// lets bullet time change animation speed without producing a phase jump.
	static uint32 last_ripple_tick = machine_tick_count();
	static double ripple_seconds = 0.0;
	const uint32 ripple_tick = machine_tick_count();
	const uint32 elapsed_ripple_ticks = ripple_tick - last_ripple_tick;
	if (elapsed_ripple_ticks > 0)
	{
		const double elapsed = std::min(
			static_cast<double>(elapsed_ripple_ticks) /
				MACHINE_TICKS_PER_SECOND, 0.25);
		ripple_seconds += elapsed *
			(sprintathon_bullet_time_active() ? 0.35 : 1.0);
		last_ripple_tick = ripple_tick;
	}
	const float ripple_phase = static_cast<float>(std::fmod(
		ripple_seconds * 1.05 * ripple_speed, TWO_PI));
	s->setFloat(Shader::U_Time, ripple_phase);
	s->setFloat(Shader::U_PixelWidth,
		view->screen_width * MainScreenPixelScale());
	s->setFloat(Shader::U_PixelHeight,
		view->screen_height * MainScreenPixelScale());
	s->setFloat(Shader::U_MediaRipple,
		mediaType != NONE && config.AnimatedMediaRipples ?
			(static_cast<float>(config.AnimatedMediaRippleStrength) + 1.0f) *
				0.25f : 0.0f);
	s->setFloat(Shader::U_MediaWetness,
		mediaType != NONE && config.AnimatedMediaRipples ?
			static_cast<float>(config.AnimatedMediaWetTextureStrength) *
				0.25f : 0.0f);

	// Capture the scene already drawn behind a transparent media surface.
	// Diffuse media shaders sample it from texture unit 2 and bend that sample
	// with the same wave field used to animate the liquid texture.
	if (mediaType != NONE && renderStep == kDiffuse &&
		config.AnimatedMediaRipples &&
		TEST_FLAG(config.Flags, OGL_Flag_LiqSeeThru))
	{
		static GLuint media_scene_texture = 0;
		static GLsizei media_scene_width = 0;
		static GLsizei media_scene_height = 0;
		const GLsizei width = static_cast<GLsizei>(
			view->screen_width * MainScreenPixelScale());
		const GLsizei height = static_cast<GLsizei>(
			view->screen_height * MainScreenPixelScale());

		glActiveTextureARB(GL_TEXTURE2_ARB);
		if (!media_scene_texture)
			glGenTextures(1, &media_scene_texture);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB, media_scene_texture);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		if (width != media_scene_width || height != media_scene_height)
		{
			glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, width, height,
				0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
			media_scene_width = width;
			media_scene_height = height;
		}
		glCopyTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
			0, 0, width, height);
		glActiveTextureARB(GL_TEXTURE0_ARB);
	}
	
	if (TMgr->TextureType == OGL_Txtr_Landscape && opts) {
		if (opts->SphereMap)
		{
			s->setFloat(Shader::U_OffsetX, opts->Azimuth * TWO_PI * FullCircleReciprocal);
		}
		else
		{
			double TexScale = std::abs(TMgr->U_Scale);
			double HorizScale = double(1 << opts->HorizExp);
			s->setFloat(Shader::U_ScaleX, HorizScale * (npotTextures ? 1.0 : TexScale) * Radian2Circle);
			s->setFloat(Shader::U_OffsetX, HorizScale * (0.25 + opts->Azimuth * FullCircleReciprocal));
			
			short AdjustedVertExp = opts->VertExp + opts->OGL_AspRatExp;
			double VertScale = (AdjustedVertExp >= 0) ? double(1 << AdjustedVertExp)
		                       : 1/double(1 << (-AdjustedVertExp));
			s->setFloat(Shader::U_ScaleY, VertScale * TexScale * Radian2Circle);
			s->setFloat(Shader::U_OffsetY, (0.5 + TMgr->U_Offset) * TexScale);
		}
	}

	if (renderStep == kGlow) {
		if (TMgr->TextureType == OGL_Txtr_Landscape) {
			s->setFloat(Shader::U_BloomScale, TMgr->LandscapeBloom());
		} else {
			s->setFloat(Shader::U_BloomScale, TMgr->BloomScale());
			s->setFloat(Shader::U_BloomShift, TMgr->BloomShift());
		}
	}
	s->setFloat(Shader::U_Flare, flare);
	s->setFloat(Shader::U_SelfLuminosity, selfLuminosity);
	s->setFloat(Shader::U_Pulsate, pulsate);
	s->setFloat(Shader::U_Wobble, wobble);
	s->setFloat(Shader::U_Depth, offset);
	s->setFloat(Shader::U_Glow, 0);
	return TMgr;
}

void instantiate_transfer_mode(struct view_data *view, short transfer_mode, world_distance &x0, world_distance &y0) {
	short alternate_transfer_phase;
	short transfer_phase = view->tick_count;

	switch (transfer_mode) {

		case _xfer_fast_horizontal_slide:
		case _xfer_horizontal_slide:
		case _xfer_vertical_slide:
		case _xfer_fast_vertical_slide:
		case _xfer_wander:
		case _xfer_fast_wander:
		case _xfer_reverse_horizontal_slide:
		case _xfer_reverse_fast_horizontal_slide:
		case _xfer_reverse_vertical_slide:
		case _xfer_reverse_fast_vertical_slide:
			x0 = y0= 0;
			switch (transfer_mode) {
				case _xfer_fast_horizontal_slide: transfer_phase<<= 1;
				case _xfer_horizontal_slide: x0= (transfer_phase<<2)&(WORLD_ONE-1); break;

				case _xfer_fast_vertical_slide: transfer_phase<<= 1;
				case _xfer_vertical_slide: y0= (transfer_phase<<2)&(WORLD_ONE-1); break;
				case _xfer_reverse_fast_horizontal_slide: transfer_phase<<= 1;
				case _xfer_reverse_horizontal_slide: x0 = WORLD_ONE - (transfer_phase<<2)&(WORLD_ONE-1); break;
			
		        case _xfer_reverse_fast_vertical_slide: transfer_phase<<= 1;
				case _xfer_reverse_vertical_slide: y0 = WORLD_ONE - (transfer_phase<<2)&(WORLD_ONE-1); break;

				case _xfer_fast_wander: transfer_phase<<= 1;
				case _xfer_wander:
					alternate_transfer_phase= transfer_phase%(10*FULL_CIRCLE);
					transfer_phase= transfer_phase%(6*FULL_CIRCLE);
					x0 = (cosine_table[NORMALIZE_ANGLE(alternate_transfer_phase)] +
						 (cosine_table[NORMALIZE_ANGLE(2*alternate_transfer_phase)]>>1) +
						 (cosine_table[NORMALIZE_ANGLE(5*alternate_transfer_phase)]>>1))>>(WORLD_FRACTIONAL_BITS-TRIG_SHIFT+2);
					y0 = (sine_table[NORMALIZE_ANGLE(transfer_phase)] +
						 (sine_table[NORMALIZE_ANGLE(2*transfer_phase)]>>1) +
						 (sine_table[NORMALIZE_ANGLE(3*transfer_phase)]>>1))>>(WORLD_FRACTIONAL_BITS-TRIG_SHIFT+2);
					break;
			}
			break;
		// wobble is done in the shader
		default:
			break;
	}
}

float calcWobble(short transferMode, short transfer_phase) {
	float wobble = 0;
	switch(transferMode) {
		case _xfer_fast_wobble:
			transfer_phase*= 15;
		case _xfer_pulsate:
		case _xfer_wobble:
			transfer_phase&= WORLD_ONE/16-1;
			transfer_phase= (transfer_phase>=WORLD_ONE/32) ? (WORLD_ONE/32+WORLD_ONE/64 - transfer_phase) : (transfer_phase - WORLD_ONE/64);
			wobble = transfer_phase / 1024.0;
			break;
	}
	return wobble;
}

void setupBlendFunc(short blendType) {
	switch(blendType)
	{
		case OGL_BlendType_Crossfade:
			glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
			break;
		case OGL_BlendType_Add:
			glBlendFunc(GL_SRC_ALPHA,GL_ONE);
			break;
		case OGL_BlendType_Crossfade_Premult:
			glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
			break;
		case OGL_BlendType_Add_Premult:
			glBlendFunc(GL_ONE, GL_ONE);
			break;
	}
}

bool setupGlow(struct view_data *view, std::unique_ptr<TextureManager>& TMgr, float wobble, float intensity, float flare, float selfLuminosity, float offset, RenderStep renderStep) {
	if (TMgr->TransferMode == _textured_transfer && TMgr->IsGlowMapped()) {
		Shader *s = NULL;
		if (TMgr->TextureType == OGL_Txtr_Wall) {
			if (TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_BumpMap)) {
				s = Shader::get(renderStep == kGlow ? Shader::S_BumpBloom : Shader::S_Bump);
			} else {
				s = Shader::get(renderStep == kGlow ? Shader::S_WallBloom : Shader::S_Wall);
			}
		} else {
			s = Shader::get(renderStep == kGlow ? Shader::S_SpriteBloom : Shader::S_Sprite);
		}

		TMgr->RenderGlowing();
		setupBlendFunc(TMgr->GlowBlend());
		glEnable(GL_TEXTURE_2D);
		glEnable(GL_BLEND);
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.001);

		s->enable();
		if (renderStep == kGlow) {
			s->setFloat(Shader::U_BloomScale, TMgr->GlowBloomScale());
			s->setFloat(Shader::U_BloomShift, TMgr->GlowBloomShift());
		}
		s->setFloat(Shader::U_Flare, flare);
		s->setFloat(Shader::U_SelfLuminosity, selfLuminosity);
		s->setFloat(Shader::U_Wobble, wobble);
		s->setFloat(Shader::U_Depth, offset - 1.0);
		s->setFloat(Shader::U_Glow, TMgr->MinGlowIntensity());
		return true;
	}
	return false;
}

void RenderRasterize_Shader::render_node_floor_or_ceiling(clipping_window_data *window,
	polygon_data *polygon, horizontal_surface_data *surface, bool void_present, bool ceil, RenderStep renderStep) {

	float offset = 0;

	const shape_descriptor& texture = AnimTxtr_Translate(surface->texture);
	float intensity = get_light_intensity(surface->lightsource_index) / float(FIXED_ONE - 1);
	float wobble = calcWobble(surface->transfer_mode, view->tick_count);
	// note: wobble and pulsate behave the same way on floors and ceilings
	// note 2: stronger wobble looks more like classic with default shaders
	auto TMgr = setupWallTexture(texture, surface->transfer_mode, wobble * 4.0,
		0, intensity, offset, renderStep, surface->media_type);
	if(TMgr->ShapeDesc == UNONE) { return; }

	const bool adjustable_media = surface->is_media &&
		TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_LiqSeeThru);
	if (adjustable_media)
	{
		GLfloat color[4];
		glGetFloatv(GL_CURRENT_COLOR, color);
		// The preference is the final surface opacity, not a multiplier on
		// the scenario texture's pre-existing alpha.
		color[3] = A1_PIN(
			Get_OGL_ConfigureData().AnimatedMediaOpacity / 100.0f,
			0.25f, 1.0f);
		glColor4fv(color);
	}

	if (TMgr->IsBlended() || adjustable_media) {
		glEnable(GL_BLEND);
		setupBlendFunc(TMgr->NormalBlend());
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.001);
	} else {
		glDisable(GL_BLEND);
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.5);
	}

	if (void_present && TMgr->IsBlended()) {
		glDisable(GL_BLEND);
		glDisable(GL_ALPHA_TEST);
	}

	short vertex_count = polygon->vertex_count;

	if (vertex_count) {
        clip_to_window(window);

		world_distance x = 0.0, y = 0.0;
		instantiate_transfer_mode(view, surface->transfer_mode, x, y);

		vec3 N;
		vec3 T;
		float sign;
		if(ceil) {
			N = vec3(0,0,-1);
			T = vec3(0,1,0);
			sign = 1;
		} else {
			N = vec3(0,0,1);
			T = vec3(0,1,0);
			sign = -1;
		}
		glNormal3f(N[0], N[1], N[2]);
		glMultiTexCoord4fARB(GL_TEXTURE1_ARB, T[0], T[1], T[2], sign);

		GLfloat vertex_array[MAXIMUM_VERTICES_PER_POLYGON * 3];
		GLfloat texcoord_array[MAXIMUM_VERTICES_PER_POLYGON * 2];

		GLfloat* vp = vertex_array;
		GLfloat* tp = texcoord_array;
		float scale;

		switch (surface->transfer_mode)
		{
			case _xfer_2x:
				scale = 2 * WORLD_ONE * TMgr->TileRatio();
				break;
		    case _xfer_4x:
				scale = 4 * WORLD_ONE * TMgr->TileRatio();
				break;
			default:
				scale = WORLD_ONE * TMgr->TileRatio();
				break;
		}

		if (ceil)
		{
			for(short i = 0; i < vertex_count; ++i) {
				world_point2d vertex = get_endpoint_data(polygon->endpoint_indexes[vertex_count - 1 - i])->vertex;
				*vp++ = vertex.x;
				*vp++ = vertex.y;
				*vp++ = surface->height;
				*tp++ = (vertex.x + surface->origin.x + x) / scale;
				*tp++ = (vertex.y + surface->origin.y + y) / scale;
			}
		}
		else
		{
			for(short i=0; i<vertex_count; ++i) {
				world_point2d vertex = get_endpoint_data(polygon->endpoint_indexes[i])->vertex;
				*vp++ = vertex.x;
				*vp++ = vertex.y;
				*vp++ = surface->height;
				*tp++ = (vertex.x + surface->origin.x + x) / scale;
				*tp++ = (vertex.y + surface->origin.y + y) / scale;
			}
		}
		glVertexPointer(3, GL_FLOAT, 0, vertex_array);
		glTexCoordPointer(2, GL_FLOAT, 0, texcoord_array);

		glDrawArrays(GL_POLYGON, 0, vertex_count);

		// see note 2 above; pulsate uniform should stay set from setupWall call
		if (setupGlow(view, TMgr, 0, intensity, weaponFlare, selfLuminosity, offset, renderStep)) {
			glDrawArrays(GL_POLYGON, 0, vertex_count);
		}

		Shader::disable();
		glMatrixMode(GL_TEXTURE);
		glLoadIdentity();
		glMatrixMode(GL_MODELVIEW);
	}
}

void RenderRasterize_Shader::render_node_side(clipping_window_data *window, vertical_surface_data *surface, bool void_present, RenderStep renderStep) {

	float offset = 0;
	if (!void_present) {
		offset = -2.0;
	}

	const shape_descriptor& texture = AnimTxtr_Translate(surface->texture_definition->texture);
	float intensity = (get_light_intensity(surface->lightsource_index) + surface->ambient_delta) / float(FIXED_ONE - 1);
	float wobble = calcWobble(surface->transfer_mode, view->tick_count);
	float pulsate = 0;
	if (surface->transfer_mode == _xfer_pulsate) {
		pulsate = wobble;
		wobble = 0;
	}
	auto TMgr = setupWallTexture(texture, surface->transfer_mode, pulsate, wobble, intensity, offset, renderStep);
	if(TMgr->ShapeDesc == UNONE) { return; }

	if (TMgr->IsBlended()) {
		glEnable(GL_BLEND);
		setupBlendFunc(TMgr->NormalBlend());
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.001);
	} else {
		glDisable(GL_BLEND);
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.5);
	}

	if (void_present && TMgr->IsBlended()) {
		glDisable(GL_BLEND);
		glDisable(GL_ALPHA_TEST);
	}

	world_distance h= MIN(surface->h1, surface->hmax);

	if (h>surface->h0) {

		world_point2d vertex[2];
		uint16 flags;
		flagged_world_point3d vertices[MAXIMUM_VERTICES_PER_WORLD_POLYGON];
		short vertex_count;

		/* initialize the two posts of our trapezoid */
		vertex_count= 2;
		long_to_overflow_short_2d(surface->p0, vertex[0], flags);
		long_to_overflow_short_2d(surface->p1, vertex[1], flags);

		if (vertex_count) {
            clip_to_window(window);

			vertex_count= 4;
			vertices[0].z= vertices[1].z= h + view->origin.z;
			vertices[2].z= vertices[3].z= surface->h0 + view->origin.z;
			vertices[0].x= vertices[3].x= vertex[0].x, vertices[0].y= vertices[3].y= vertex[0].y;
			vertices[1].x= vertices[2].x= vertex[1].x, vertices[1].y= vertices[2].y= vertex[1].y;
			vertices[0].flags = vertices[3].flags = 0;
			vertices[1].flags = vertices[2].flags = 0;

			uint16 div;
			switch (surface->transfer_mode)
			{
				case _xfer_2x:
					div = 2 * WORLD_ONE * TMgr->TileRatio();
					break;
				case _xfer_4x:
					div = 4 * WORLD_ONE * TMgr->TileRatio();
					break;
				default:
					div = WORLD_ONE * TMgr->TileRatio();;
					break;
			}
			
			double dx = (surface->p1.i - surface->p0.i) / double(surface->length);
			double dy = (surface->p1.j - surface->p0.j) / double(surface->length);

			world_distance x0 = surface->texture_definition->x0 % div;
			world_distance y0 = surface->texture_definition->y0 % div;

			double tOffset = surface->h1 + view->origin.z + y0;

			vec3 N(-dy, dx, 0);
			vec3 T(dx, dy, 0);
			float sign = 1;
			glNormal3f(N[0], N[1], N[2]);
			glMultiTexCoord4fARB(GL_TEXTURE1_ARB, T[0], T[1], T[2], sign);

			world_distance x = 0.0, y = 0.0;
			instantiate_transfer_mode(view, surface->transfer_mode, x, y);

			x0 -= x;
			tOffset -= y;

			GLfloat vertex_array[12];
			GLfloat texcoord_array[8];

			GLfloat* vp = vertex_array;
			GLfloat* tp = texcoord_array;

			for(int i = 0; i < vertex_count; ++i) {
				float p2 = 0;
				if(i == 1 || i == 2) { p2 = surface->length; }

				*vp++ = vertices[i].x;
				*vp++ = vertices[i].y;
				*vp++ = vertices[i].z;
				*tp++ = (tOffset - vertices[i].z) / static_cast<float>(div);
				*tp++ = (x0+p2) / static_cast<float>(div);
			}
			glVertexPointer(3, GL_FLOAT, 0, vertex_array);
			glTexCoordPointer(2, GL_FLOAT, 0, texcoord_array);
			
			glDrawArrays(GL_QUADS, 0, vertex_count);

			if (setupGlow(view, TMgr, wobble, intensity, weaponFlare, selfLuminosity, offset, renderStep)) {
				glDrawArrays(GL_QUADS, 0, vertex_count);
			}

			Shader::disable();
			glMatrixMode(GL_TEXTURE);
			glLoadIdentity();
			glMatrixMode(GL_MODELVIEW);
		}
	}
}

extern void FlatBumpTexture(); // from OGL_Textures.cpp

bool RenderModel(rectangle_definition& RenderRectangle, short Collection, short CLUT, float flare, float selfLuminosity, RenderStep renderStep) {

	OGL_ModelData *ModelPtr = RenderRectangle.ModelPtr;
	OGL_SkinData *SkinPtr = ModelPtr->GetSkin(CLUT);
	if(!SkinPtr) { return false; }

	if (ModelPtr->Sidedness < 0) {
		glEnable(GL_CULL_FACE);
		glFrontFace(GL_CCW);
	} else if (ModelPtr->Sidedness > 0) {
		glEnable(GL_CULL_FACE);
		glFrontFace(GL_CW);
	} else {
		glDisable(GL_CULL_FACE);
	}

	glEnable(GL_TEXTURE_2D);
	if (SkinPtr->OpacityType != OGL_OpacType_Crisp || RenderRectangle.transfer_mode == _tinted_transfer) {
		glEnable(GL_BLEND);
		setupBlendFunc(SkinPtr->NormalBlend);
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.001);
	} else {
		glDisable(GL_BLEND);
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.5);
	}

	GLfloat color[3];
	GLdouble shade = PIN(static_cast<GLfloat>(RenderRectangle.ambient_shade)/static_cast<GLfloat>(FIXED_ONE),0,1);
	color[0] = color[1] = color[2] = shade;

	Shader *s = NULL;
	bool canGlow = false;
	if (RenderRectangle.transfer_mode == _static_transfer) {
		flare = -1;
		if (renderStep == kDiffuse) {
			s = Shader::get(Shader::S_Invincible);
		} else {
			s = Shader::get(Shader::S_InvincibleBloom);
		}
        s->enable();
        s->setFloat(Shader::U_TransferFadeOut,((float)((uint16)RenderRectangle.transfer_data))/(float)((int)FIXED_ONE));
	} else if (current_player->infravision_duration) {
		color[0] = color[1] = color[2] = 1;
		FindInfravisionVersionRGBA(GET_COLLECTION(GET_DESCRIPTOR_COLLECTION(RenderRectangle.ShapeDesc)), color);
		s = Shader::get(Shader::S_WallInfravision);
	} else if (RenderRectangle.transfer_mode == _tinted_transfer) {
			flare = -1;
			if (renderStep == kDiffuse) {
				s = Shader::get(Shader::S_Invisible);
			} else {
				s = Shader::get(Shader::S_InvisibleBloom);
			}
			s->enable();
			s->setFloat(Shader::U_Visibility, 1.0 - RenderRectangle.transfer_data/32.0f);
	} else if (RenderRectangle.transfer_mode == _solid_transfer) {
		color[0] = 0;
		color[1] = 1;
		color[2] = 0;
	} else if (RenderRectangle.transfer_mode == _textured_transfer) {
		if (RenderRectangle.flags & _SHADELESS_BIT) {
			if (renderStep == kDiffuse) {
				color[0] = color[1] = color[2] = 1;
			} else {
				color[0] = color[1] = color[2] = 0;
			}
			flare = -1;
		} else {
			canGlow = true;
		}
	} else {
		color[0] = 0;
		color[1] = 0;
		color[2] = 1;
	}

	if(s == NULL) {
		if(TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_BumpMap)) {
			s = Shader::get(renderStep == kGlow ? Shader::S_BumpBloom : Shader::S_Bump);
		} else {
			s = Shader::get(renderStep == kGlow ? Shader::S_WallBloom : Shader::S_Wall);
		}
		s->enable();
	}

	if (renderStep == kGlow) {
		s->setFloat(Shader::U_BloomScale, SkinPtr->BloomScale);
		s->setFloat(Shader::U_BloomShift, SkinPtr->BloomShift);
	}
	s->setFloat(Shader::U_Flare, flare);
	s->setFloat(Shader::U_SelfLuminosity, selfLuminosity);
	s->setFloat(Shader::U_Wobble, 0);
	s->setFloat(Shader::U_Depth, 0);
	s->setFloat(Shader::U_Glow, 0);
	glColor4f(color[0], color[1], color[2], 1);

	// Find an animated model's vertex positions and normals:
	short ModelSequence = RenderRectangle.ModelSequence;
	if (ModelSequence >= 0)
	{
		int NumFrames = ModelPtr->Model.NumSeqFrames(ModelSequence);
		if (NumFrames > 0)
		{
			short ModelFrame = PIN(RenderRectangle.ModelFrame, 0, NumFrames - 1);
			short NextModelFrame = PIN(RenderRectangle.NextModelFrame, 0, NumFrames - 1);
			float MixFrac = RenderRectangle.MixFrac;
			ModelPtr->Model.FindPositions_Sequence(true,
				ModelSequence, ModelFrame, MixFrac, NextModelFrame);
		}
		else
			ModelPtr->Model.FindPositions_Neutral(true);	// Fallback: neutral
	}
	else
		ModelPtr->Model.FindPositions_Neutral(true);	// Fallback: neutral (will do nothing for static models)

	glVertexPointer(3,GL_FLOAT,0,ModelPtr->Model.PosBase());
	glClientActiveTextureARB(GL_TEXTURE0_ARB);
	if (ModelPtr->Model.TxtrCoords.empty()) {
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	} else {
		glTexCoordPointer(2,GL_FLOAT,0,ModelPtr->Model.TCBase());
	}

	glEnableClientState(GL_NORMAL_ARRAY);
	glNormalPointer(GL_FLOAT,0,ModelPtr->Model.NormBase());

	glClientActiveTextureARB(GL_TEXTURE1_ARB);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glTexCoordPointer(4,GL_FLOAT,sizeof(vec4),ModelPtr->Model.TangentBase());

	if(ModelPtr->Use(CLUT,OGL_SkinManager::Normal)) {
		LoadModelSkin(SkinPtr->NormalImg, Collection, CLUT);
	}

	if(TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_BumpMap)) {
		glActiveTextureARB(GL_TEXTURE1_ARB);
		if(ModelPtr->Use(CLUT,OGL_SkinManager::Bump)) {
			LoadModelSkin(SkinPtr->OffsetImg, Collection, CLUT);
		}
		if (!SkinPtr->OffsetImg.IsPresent()) {
			FlatBumpTexture();
		}
		glActiveTextureARB(GL_TEXTURE0_ARB);
	}

	glDrawElements(GL_TRIANGLES,(GLsizei)ModelPtr->Model.NumVI(),GL_UNSIGNED_SHORT,ModelPtr->Model.VIBase());

	if (canGlow && SkinPtr->GlowImg.IsPresent()) {
		glEnable(GL_BLEND);
		setupBlendFunc(SkinPtr->GlowBlend);
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.001);

		s->enable();
		s->setFloat(Shader::U_Glow, SkinPtr->MinGlowIntensity);
		if (renderStep == kGlow) {
			s->setFloat(Shader::U_BloomScale, SkinPtr->GlowBloomScale);
			s->setFloat(Shader::U_BloomShift, SkinPtr->GlowBloomShift);
		}

		if(ModelPtr->Use(CLUT,OGL_SkinManager::Glowing)) {
			LoadModelSkin(SkinPtr->GlowImg, Collection, CLUT);
		}
		glDrawElements(GL_TRIANGLES,(GLsizei)ModelPtr->Model.NumVI(),GL_UNSIGNED_SHORT,ModelPtr->Model.VIBase());
	}

	glDisableClientState(GL_NORMAL_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glClientActiveTextureARB(GL_TEXTURE0_ARB);
	if (ModelPtr->Model.TxtrCoords.empty()) {
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	}

	// Restore the default render sidedness
	glEnable(GL_CULL_FACE);
	glFrontFace(GL_CW);
	Shader::disable();
	return true;
}

void RenderRasterize_Shader::render_node_object(render_object_data *object, bool other_side_of_media, RenderStep renderStep) {

    if (!object->clipping_windows)
        return;

	clipping_window_data *win;

	// To properly handle sprites in media, we render above and below
	// the media boundary in separate passes, just like the original
	// software renderer.
	short media_index = get_polygon_data(object->node->polygon_index)->media_index;
	media_data *media = (media_index != NONE) ? get_media_data(media_index) : NULL;
	if (media) {
		float h = media->height;
		GLdouble plane[] = { 0.0, 0.0, 1.0, -h };
		if (view->under_media_boundary ^ other_side_of_media) {
			plane[2] = -1.0;
			plane[3] = h;
		}
		glClipPlane(GL_CLIP_PLANE5, plane);
		glEnable(GL_CLIP_PLANE5);
	} else if (other_side_of_media) {
		// When there's no media present, we can skip the second pass.
		return;
	}

    for (win = object->clipping_windows; win; win = win->next_window)
    {
        clip_to_window(win);
        _render_node_object_helper(object, renderStep);
    }
    
    glDisable(GL_CLIP_PLANE5);
}

void RenderRasterize_Shader::_render_node_object_helper(render_object_data *object, RenderStep renderStep) {

	rectangle_definition& rect = object->rectangle;
	const world_point3d& pos = rect.Position;
    
	if(rect.ModelPtr) {
		glPushMatrix();
		glTranslated(pos.x, pos.y, pos.z);
		glRotated((360.0/FULL_CIRCLE)*rect.Azimuth,0,0,1);
		GLfloat HorizScale = rect.Scale*rect.HorizScale;
		glScalef(HorizScale,HorizScale,rect.Scale);

		short descriptor = GET_DESCRIPTOR_COLLECTION(rect.ShapeDesc);
		short collection = GET_COLLECTION(descriptor);
		short clut = ModifyCLUT(rect.transfer_mode,GET_COLLECTION_CLUT(descriptor));

		RenderModel(rect, collection, clut, weaponFlare, selfLuminosity, renderStep);
		glPopMatrix();
		return;
	}

	glPushMatrix();
	glTranslated(pos.x, pos.y, pos.z);

	double yaw = view->virtual_yaw * FixedAngleToDegrees;
	glRotated(yaw, 0.0, 0.0, 1.0);

			
	float offset = 0;
	const bool sprintathon_strict_sprite_depth =
		!view->mimic_sw_perspective &&
		input_preferences->sprintathon_enabled &&
		input_preferences->sprintathon_mouselook_mode > 0;
	if (OGL_ForceSpriteDepth() || sprintathon_strict_sprite_depth) {
		// look for parasitic objects based on y position,
		// and offset them to draw in proper depth order
		if(pos.y == objectY) {
			objectCount++;
			offset = objectCount * -1.0;
		} else {
			objectCount = 0;
			objectY = pos.y;
		}
	} else {
		glDisable(GL_DEPTH_TEST);
	}

	auto TMgr = setupSpriteTexture(rect, OGL_Txtr_Inhabitant, offset, renderStep);
	if (TMgr->ShapeDesc == UNONE) { glPopMatrix(); return; }

	if (!view->mimic_sw_perspective)
	{
		if (TMgr->ForceXYBillboard() ||
			(view->billboard_xy && !TMgr->ForceYBillboard()))
		{
			glRotated(view->virtual_pitch * FixedAngleToDegrees, 0.0, -1.0, 0.0);
		}
	}

	float texCoords[2][2];

	if(rect.flip_vertical) {
		texCoords[0][1] = TMgr->U_Offset;
		texCoords[0][0] = TMgr->U_Scale+TMgr->U_Offset;
	} else {
		texCoords[0][0] = TMgr->U_Offset;
		texCoords[0][1] = TMgr->U_Scale+TMgr->U_Offset;
	}

	if(rect.flip_horizontal) {
		texCoords[1][1] = TMgr->V_Offset;
		texCoords[1][0] = TMgr->V_Scale+TMgr->V_Offset;
	} else {
		texCoords[1][0] = TMgr->V_Offset;
		texCoords[1][1] = TMgr->V_Scale+TMgr->V_Offset;
	}

	if(TMgr->IsBlended() || TMgr->TransferMode == _tinted_transfer) {
		glEnable(GL_BLEND);
		setupBlendFunc(TMgr->NormalBlend());
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.001);
	} else {
		glDisable(GL_BLEND);
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.5);
	}

	GLfloat vertex_array[12] = {
		0,
		rect.WorldLeft * rect.HorizScale * rect.Scale,
		rect.WorldTop * rect.Scale,
		0,
		rect.WorldRight * rect.HorizScale * rect.Scale,
		rect.WorldTop * rect.Scale,
		0,
		rect.WorldRight * rect.HorizScale * rect.Scale,
		rect.WorldBottom * rect.Scale,
		0,
		rect.WorldLeft * rect.HorizScale * rect.Scale,
		rect.WorldBottom * rect.Scale
	};

	GLfloat texcoord_array[8] = {
		texCoords[0][0],
		texCoords[1][0],
		texCoords[0][0],
		texCoords[1][1],
		texCoords[0][1],
		texCoords[1][1],
		texCoords[0][1],
		texCoords[1][0]
	};

	glVertexPointer(3, GL_FLOAT, 0, vertex_array);
	glTexCoordPointer(2, GL_FLOAT, 0, texcoord_array);

	glDrawArrays(GL_QUADS, 0, 4);

	if (setupGlow(view, TMgr, 0, 1, weaponFlare, selfLuminosity, offset, renderStep)) {
		glDrawArrays(GL_QUADS, 0, 4);
	}
        
	glEnable(GL_DEPTH_TEST);
	glPopMatrix();
	Shader::disable();
	TMgr->RestoreTextureMatrix();
}

extern void position_sprite_axis(short *x0, short *x1, short scale_width, short screen_width, short positioning_mode, _fixed position, bool flip, world_distance world_left, world_distance world_right);

extern GLdouble Screen_2_Clip[16];

static void render_slide_legs(view_data *view, RenderStep renderStep)
{
	const bool back_dodge_active =
		current_player &&
		current_player->dodge_last_direction == 2 &&
		current_player->dodge_ticks_remaining > 0;
	const bool back_dodge_recovering =
		current_player &&
		current_player->dodge_last_direction == 2 &&
		current_player->back_dodge_recovery_ticks > 0;
	const bool back_dodge =
		back_dodge_active || back_dodge_recovering;

	/*
	 * Keep this visual state separate from gameplay. The legs respond more
	 * slowly than the weapon, creating the impression that the camera/head
	 * turns first and the body is dragged behind it.
	 */
	static float smooth_back_dodge_drag_x = 0.0f;
	static bool back_dodge_was_active = false;

	if (!back_dodge)
	{
		smooth_back_dodge_drag_x = 0.0f;
		back_dodge_was_active = false;
	}
	else if (!back_dodge_was_active)
	{
		smooth_back_dodge_drag_x = 0.0f;
		back_dodge_was_active = true;
	}

	if (renderStep != kDiffuse ||
		!input_preferences->sprintathon_enabled ||
		!current_player ||
		(!back_dodge &&
		 (!input_preferences->sprintathon_slide ||
		  (current_player->slide_ticks_remaining == 0 &&
		   !current_player->flying_kick_active &&
		   current_player->flying_kick_landing_ticks == 0 &&
		   current_player->flying_kick_exit_ticks == 0))))
	{
		return;
	}

	static OGL_Blitter slide_legs;
	static OGL_Blitter front_legs;
	static bool load_attempted = false;
	static bool front_load_attempted = false;

	if (!load_attempted)
	{
		load_attempted = true;
		FileSpecifier file("gfx/slidelegs.png");

		if (!file.Exists() &&
			!file.SetNameWithPath("Sprintathon/slidelegs.png"))
		{
			file = FileSpecifier(
				get_data_path(kPathDefaultData) +
				"/Sprintathon/slidelegs.png");
		}

		if (file.Exists())
		{
			ImageDescriptor image;
			if (image.LoadFromFile(file, ImageLoader_Colors, 0))
				slide_legs.Load(image);
		}
	}

	if (!front_load_attempted)
	{
		front_load_attempted = true;
		FileSpecifier file("gfx/frontlegs.png");

		if (!file.Exists() &&
			!file.SetNameWithPath("Sprintathon/frontlegs.png"))
		{
			file = FileSpecifier(
				get_data_path(kPathDefaultData) +
				"/Sprintathon/frontlegs.png");
		}

		if (file.Exists())
		{
			ImageDescriptor image;
			if (image.LoadFromFile(file, ImageLoader_Colors, 0))
				front_legs.Load(image);
		}
	}

	OGL_Blitter& visible_legs =
		back_dodge ? front_legs : slide_legs;

	if (!visible_legs.Loaded())
		return;

	constexpr int slide_duration = (TICKS_PER_SECOND * 3) / 4;
	constexpr int back_dodge_duration = 12;
	constexpr int back_dodge_recovery_duration = 26;
	const bool flying_kick = current_player->flying_kick_active;
	const bool kick_landing =
		current_player->flying_kick_landing_ticks > 0;
	const bool kick_exit = current_player->flying_kick_exit_ticks > 0;
	const int ticks_remaining = flying_kick ? slide_duration :
		kick_landing ?
			static_cast<int>(current_player->flying_kick_landing_ticks) :
		kick_exit ?
			static_cast<int>(current_player->flying_kick_exit_ticks) :
		back_dodge_active ?
			A1_PIN(static_cast<int>(current_player->dodge_ticks_remaining),
				0, back_dodge_duration) :
		back_dodge_recovering ?
			A1_PIN(static_cast<int>(
				current_player->back_dodge_recovery_ticks),
				0, back_dodge_recovery_duration) :
			A1_PIN(static_cast<int>(current_player->slide_ticks_remaining),
				0, slide_duration);
	const int ticks_elapsed = back_dodge_active ?
		back_dodge_duration - ticks_remaining :
		back_dodge_recovering ?
			back_dodge_duration :
		flying_kick ?
			static_cast<int>(current_player->flying_kick_ticks) :
			slide_duration - ticks_remaining;

	/*
	 * Use position rather than transparency for the animation. The legs
	 * rise quickly at the start, remain fully visible, then drop rapidly
	 * below the screen over the final eight ticks.
	 */
	float slide_in = A1_PIN(ticks_elapsed / 4.0f, 0.0f, 1.0f);
	float slide_out = flying_kick ? 1.0f :
		back_dodge_active ? 1.0f :
		back_dodge_recovering ?
			A1_PIN(
				ticks_remaining /
					static_cast<float>(back_dodge_recovery_duration),
				0.0f, 1.0f) :
		A1_PIN(ticks_remaining /
			(kick_landing ? 12.0f : 8.0f), 0.0f, 1.0f);

	slide_in =
		slide_in * slide_in * (3.0f - 2.0f * slide_in);
	slide_out =
		slide_out * slide_out * (3.0f - 2.0f * slide_out);

	const float visibility =
		std::min(slide_in, slide_out);

	const float sprite_height = view->screen_height * 0.78f;
	const float sprite_width = sprite_height *
		static_cast<float>(visible_legs.UnscaledWidth()) /
		static_cast<float>(visible_legs.UnscaledHeight());
	// Keep most of the body below the frame at level pitch. Looking down
	// progressively reveals it, while the slide animation moves it up from
	// below rather than abruptly appearing in the middle of the view.
	/*
	 * Follow the player's live aim instead of the pitch stored in the
	 * current rendered view. This lets the body remain visually attached
	 * to the floor while the player looks around during the slide.
	 */
	const fixed_angle live_pitch =
		FIXED_INTEGERAL_PART(
			current_player->variables.elevation) * FIXED_ONE +
		virtual_aim_delta().pitch;

	const float downward_degrees =
		-static_cast<float>(live_pitch) * FixedAngleToDegrees;

	// Use both downward and upward pitch so the body travels continuously
	// rather than stopping at its level-view position.
	const float pitch_position =
		A1_PIN((downward_degrees + 45.0f) / 120.0f, 0.0f, 1.0f);

	float floor_reveal =
		pitch_position * pitch_position *
		(3.0f - 2.0f * pitch_position);

	/*
	 * Looking upward leaves only the boots at the bottom edge. Looking
	 * downward brings nearly the entire body into view.
	 */
	const float revealed_fraction = back_dodge ?
		0.08f + 0.92f * floor_reveal :
		(flying_kick || kick_exit) ?
			0.32f + 0.68f * floor_reveal :
			0.12f + 0.88f * floor_reveal;

	// A small independent offset creates the entrance from below without
	// allowing the fade animation to hide the sprite completely.
	float slide_in_offset =
		(1.0f - visibility) * sprite_height * 0.15f;

	/*
	 * Once back-dodge recovery starts, physically sweep the complete body
	 * below the frame instead of making it appear to vanish at the edge.
	 * Six ticks gives a quick but still readable downward motion.
	 */
	if (back_dodge_recovering)
	{
		constexpr float exit_ticks = 6.0f;
		float exit_progress = A1_PIN(
			(back_dodge_recovery_duration -
			 current_player->back_dodge_recovery_ticks) / exit_ticks,
			0.0f,
			1.0f);

		exit_progress =
			exit_progress * exit_progress *
			(3.0f - 2.0f * exit_progress);

		slide_in_offset +=
			exit_progress * sprite_height * 1.10f;
	}

	float back_dodge_drag_x = 0.0f;
	if (back_dodge)
	{
		const int horizontal_velocity =
			FIXED_INTEGERAL_PART(
				current_player->variables.angular_velocity);
		const int maximum_drag = view->screen_width / 7;
		const int target_drag = A1_PIN(
			(-horizontal_velocity * view->screen_width) / 192,
			-maximum_drag,
			maximum_drag);

		// Slower than the weapon's 0.14 follow speed.
		constexpr float leg_drag_follow_speed = 0.035f;
		smooth_back_dodge_drag_x +=
			(target_drag - smooth_back_dodge_drag_x) *
			leg_drag_follow_speed;
		back_dodge_drag_x = smooth_back_dodge_drag_x;
	}

	const Image_Rect destination(
		(view->screen_width - sprite_width) * 0.5f +
			back_dodge_drag_x,
		view->screen_height -
			sprite_height * revealed_fraction +
			slide_in_offset,
		sprite_width,
		sprite_height);

	// Match the first-person weapon's lighting source: the floor light of the
	// polygon containing the camera. Keep alpha independent so the entrance
	// and exit remain position-only animations.
	const _fixed polygon_light = get_light_intensity(
		get_polygon_data(view->origin_polygon_index)->floor_lightsource_index);
	const float light_shade = A1_PIN(
		static_cast<float>(polygon_light) / static_cast<float>(FIXED_ONE),
		0.0f,
		1.0f);
	visible_legs.tint_color_r = light_shade;
	visible_legs.tint_color_g = light_shade;
	visible_legs.tint_color_b = light_shade;
	visible_legs.tint_color_a = 1.0f;
	visible_legs.rotation = 0.0f;
	Shader::disable();
	visible_legs.Draw(destination);
}

void RenderRasterize_Shader::render_viewer_sprite_layer(RenderStep renderStep)
{
        if (!view->show_weapons_in_hand) return;
	if (sprintathon_single_pistol_zoom_active()) return;
    
        glMatrixMode(GL_TEXTURE);
        glPushMatrix();
    
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadMatrixd(Screen_2_Clip);

        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

	// Draw the sliding body beneath the normal first-person weapon sprites.
	render_slide_legs(view, renderStep);

        rectangle_definition rect;
	weapon_display_information display_data;
	shape_information_data *shape_information;
	short count;

	/*
	 * Briefly tuck the weapon away while either mantle system pulls the
	 * player over an edge. Calculate this once per rendered frame so paired
	 * weapons receive exactly the same offset.
	 */
	static float mantle_weapon_lower = 0.0f;
	const bool sprintathon_mantling =
		current_player &&
		input_preferences->sprintathon_enabled &&
		((current_player->variables.flags&_DRY_MANTLING_BIT) ||
		 (current_player->variables.flags&_WATER_MANTLING_BIT));
	const float mantle_lower_target = sprintathon_mantling ?
		static_cast<float>(view->screen_height) * 0.30f : 0.0f;
	const float mantle_ease =
		mantle_lower_target > mantle_weapon_lower ? 0.38f : 0.30f;
	mantle_weapon_lower +=
		(mantle_lower_target - mantle_weapon_lower) * mantle_ease;
	const short mantle_lower_offset =
		static_cast<short>(mantle_weapon_lower);

        rect.ModelPtr = nullptr;
        rect.Opacity = 1;

        /* get_weapon_display_information() returns true if there is a weapon to be drawn.  it
           should initially be passed a count of zero.  it returns the weapon’s texture and
           enough information to draw it correctly. */
	count= 0;
	while (get_weapon_display_information(&count, &display_data))
	{
		/* fetch relevant shape data */
                shape_information= extended_get_shape_information(display_data.collection, display_data.low_level_shape_index);

                // Nonexistent frame: skip
		if (!shape_information) continue;
		
		// LP change: for the convenience of the OpenGL renderer
		rect.ShapeDesc = BUILD_DESCRIPTOR(display_data.collection,0);
		rect.LowLevelShape = display_data.low_level_shape_index;

		if (shape_information->flags&_X_MIRRORED_BIT) display_data.flip_horizontal= !display_data.flip_horizontal;
		if (shape_information->flags&_Y_MIRRORED_BIT) display_data.flip_vertical= !display_data.flip_vertical;

		/* calculate shape rectangle */
		position_sprite_axis(&rect.x0, &rect.x1, view->screen_height, view->screen_width, display_data.horizontal_positioning_mode,
			display_data.horizontal_position, display_data.flip_horizontal, shape_information->world_left, shape_information->world_right);
		position_sprite_axis(&rect.y0, &rect.y1, view->screen_height, view->screen_height, display_data.vertical_positioning_mode,
			display_data.vertical_position, display_data.flip_vertical, -shape_information->world_top, -shape_information->world_bottom);

		if (input_preferences->sprintathon_enabled)
		{
		// Experimental: shrink first-person weapon around bottom-centre.
		constexpr int weapon_scale_percent = 82;
		const int weapon_anchor_x = view->screen_width / 2;
		const int weapon_anchor_y = view->screen_height;
		rect.x0 = weapon_anchor_x + (rect.x0 - weapon_anchor_x) * weapon_scale_percent / 100;
		rect.x1 = weapon_anchor_x + (rect.x1 - weapon_anchor_x) * weapon_scale_percent / 100;
		rect.y0 = weapon_anchor_y + (rect.y0 - weapon_anchor_y) * weapon_scale_percent / 100;
		rect.y1 = weapon_anchor_y + (rect.y1 - weapon_anchor_y) * weapon_scale_percent / 100;

		// Subtle viewmodel sway opposite the current camera movement.
		const int horizontal_velocity =
			FIXED_INTEGERAL_PART(
				local_player->variables.angular_velocity);
		const int vertical_velocity =
			FIXED_INTEGERAL_PART(
				local_player->variables.vertical_angular_velocity);

		/*
		 * Stronger viewmodel sway, scaled with resolution.
		 *
		 * Static visual state is intentionally renderer-local: it does
		 * not affect gameplay, networking or replay determinism.
		 */
		const int maximum_sway_x = view->screen_width / 16;
		const int maximum_sway_y = view->screen_height / 14;

		const int target_sway_x = A1_PIN(
			(-horizontal_velocity * view->screen_width) / 256,
			-maximum_sway_x,
			maximum_sway_x);
		const int target_sway_y = A1_PIN(
			(vertical_velocity * view->screen_height) / 192,
			-maximum_sway_y,
			maximum_sway_y);

		static float smooth_sway_x = 0.0f;
		static float smooth_sway_y = 0.0f;

		// Lower values are smoother but produce more visual lag.
		constexpr float sway_follow_speed = 0.14f;

		smooth_sway_x +=
			(target_sway_x - smooth_sway_x) * sway_follow_speed;
		smooth_sway_y +=
			(target_sway_y - smooth_sway_y) * sway_follow_speed;

		const int weapon_sway_x =
			static_cast<int>(smooth_sway_x);
		const int weapon_sway_y =
			static_cast<int>(smooth_sway_y);

		rect.x0 += weapon_sway_x;
		rect.x1 += weapon_sway_x;
		rect.y0 += weapon_sway_y;
		rect.y1 += weapon_sway_y;

		/*
		 * Keep the weapon upright and anchored at the bottom during a
		 * cartwheel, but let it swing heavily against the camera rotation.
		 */
		if (current_player && current_player->cartwheel_active)
		{
			const angle cartwheel_phase= NORMALIZE_ANGLE(
				static_cast<angle>(current_player->cartwheel_camera_roll));
			const int cartwheel_sway_x= static_cast<int>(
				(static_cast<int64_t>(view->screen_width)*
				 sine_table[cartwheel_phase])/(5*TRIG_MAGNITUDE));
			const int cartwheel_sway_y= static_cast<int>(
				(static_cast<int64_t>(view->screen_height)*
				 (TRIG_MAGNITUDE-cosine_table[cartwheel_phase]))/
				 (14*TRIG_MAGNITUDE));
			rect.x0 -= cartwheel_sway_x;
			rect.x1 -= cartwheel_sway_x;
			rect.y0 += cartwheel_sway_y;
			rect.y1 += cartwheel_sway_y;
		}
		
		// Smoothly lower the weapon while sprinting.
		static float sprint_weapon_lower = 0.0f;
		float sprint_lower_target = 0.0f;

		if (current_player)
		{
			if (current_player->sprinting)
			{
				sprint_lower_target =
					static_cast<float>(view->screen_height) / 8.0f;
			}

			/*
			 * Lower the weapon more deeply during the final slide
			 * phase, then raise it throughout the 16-tick recovery.
			 */
			float slide_recovery_amount = 0.0f;

			if (current_player->flying_kick_landing_ticks > 0)
			{
				slide_recovery_amount = 1.0f;
			}
			else if (current_player->slide_ticks_remaining > 0 &&
				current_player->slide_ticks_remaining <= 8)
			{
				slide_recovery_amount = 1.0f;
			}
			else if (current_player->slide_recovery_ticks > 0)
			{
				slide_recovery_amount =
					current_player->slide_recovery_ticks / 24.0f;
			}

			sprint_lower_target = std::max(
				sprint_lower_target,
				slide_recovery_amount *
					static_cast<float>(view->screen_height) * 0.42f);
		}

		sprint_weapon_lower +=
			(sprint_lower_target - sprint_weapon_lower) * 0.16f;

		const short sprint_lower_offset =
			static_cast<short>(sprint_weapon_lower);

		rect.y0 += sprint_lower_offset;
		rect.y1 += sprint_lower_offset;

		rect.y0 += mantle_lower_offset;
		rect.y1 += mantle_lower_offset;

		// Quick side-to-side weapon swing while sprinting.
		static float sprint_sway_amount = 0.0f;

		const float sprint_sway_target =
			(current_player && current_player->sprinting)
				? 1.0f
				: 0.0f;

		sprint_sway_amount +=
			(sprint_sway_target - sprint_sway_amount) * 0.30f;

		// Follow the physics step phase so the swing peaks stay locked to
		// sprint footsteps instead of drifting with rendering time.
		const float sprint_sway_phase = current_player
			? static_cast<float>(current_player->variables.step_phase) *
				6.283185307f / FIXED_ONE
			: 0.0f;

		const short sprint_sway_offset = static_cast<short>(
			std::sin(sprint_sway_phase) *
			(static_cast<float>(view->screen_width) / 32.0f) *
			sprint_sway_amount);

		rect.x0 += sprint_sway_offset;
		rect.x1 += sprint_sway_offset;
		}

		/*
		 * Scaling and sway are applied after the scenario's weapon origin,
		 * so a sprite intentionally pinned to an edge can otherwise drift far
		 * enough inward to expose the hard boundary of its bitmap. Clamp these
		 * side-mounted sprites last and retain a small off-screen bleed. Doing
		 * this here also accounts for every per-frame sway contribution.
		 */
		if (display_data.side_mounted &&
			display_data.horizontal_positioning_mode == _position_center)
		{
			const _fixed side_margin = FIXED_ONE / 8;
			const int edge_bleed = std::max<int>(2, view->screen_width / 128);
			if (display_data.horizontal_position < FIXED_ONE_HALF - side_margin &&
				rect.x0 > -edge_bleed)
			{
				const int offset = -edge_bleed - rect.x0;
				rect.x0 += offset;
				rect.x1 += offset;
			}
			else if (display_data.horizontal_position > FIXED_ONE_HALF + side_margin &&
				rect.x1 < view->screen_width + edge_bleed)
			{
				const int offset = view->screen_width + edge_bleed - rect.x1;
				rect.x0 += offset;
				rect.x1 += offset;
			}
		}

		/* set rectangle bitmap and shading table */
		extended_get_shape_bitmap_and_shading_table(display_data.collection, display_data.low_level_shape_index, &rect.texture, &rect.shading_tables, view->shading_mode);
		if (!rect.texture) continue;
		
		rect.flags= 0;

		/* initialize clipping window to full screen */
		rect.clip_left= 0;
		rect.clip_right= view->screen_width;
		rect.clip_top= 0;
		rect.clip_bottom= view->screen_height;

		/* copy mirror flags */
		rect.flip_horizontal= display_data.flip_horizontal;
		rect.flip_vertical= display_data.flip_vertical;
		
		/* lighting: depth of zero in the camera’s polygon index */
		rect.depth= 0;
		rect.ambient_shade= get_light_intensity(get_polygon_data(view->origin_polygon_index)->floor_lightsource_index);
		rect.ambient_shade= MAX(shape_information->minimum_light_intensity, rect.ambient_shade);
		if (view->shading_mode==_shading_infravision) rect.flags|= _SHADELESS_BIT;

		// Calculate the object's horizontal position
		// for the convenience of doing teleport-in/teleport-out
		rect.xc = (rect.x0 + rect.x1) >> 1;

                /* make the weapon reflect the owner’s transfer mode */
		instantiate_rectangle_transfer_mode(view, &rect, display_data.transfer_mode, display_data.transfer_phase);

                render_viewer_sprite(rect, renderStep,
			display_data.rotation_degrees);
        }

        Shader::disable();
    
        glPopMatrix();

        glMatrixMode(GL_PROJECTION);
        glPopMatrix();

        glMatrixMode(GL_TEXTURE);
        glPopMatrix();
    
        glMatrixMode(GL_MODELVIEW);
}

struct ExtendedVertexData
{
	GLdouble Vertex[4];
	GLdouble TexCoord[2];
	GLfloat Color[3];
	GLfloat GlowColor[3];
};

void RenderRasterize_Shader::render_viewer_sprite(rectangle_definition& RenderRectangle,
	RenderStep renderStep, float rotation_degrees)
{
	// Find texture coordinates
	ExtendedVertexData ExtendedVertexList[4];
	
	point2d TopLeft, BottomRight;
	// Clipped corners:
	TopLeft.x = MAX(RenderRectangle.x0,RenderRectangle.clip_left);
	TopLeft.y = MAX(RenderRectangle.y0,RenderRectangle.clip_top);
	BottomRight.x = MIN(RenderRectangle.x1,RenderRectangle.clip_right);
	BottomRight.y = MIN(RenderRectangle.y1,RenderRectangle.clip_bottom);
	
        // Screen coordinates; weapons-in-hand are in the foreground
        ExtendedVertexList[0].Vertex[0] = TopLeft.x;
        ExtendedVertexList[0].Vertex[1] = TopLeft.y;
        ExtendedVertexList[0].Vertex[2] = 1;
        ExtendedVertexList[2].Vertex[0] = BottomRight.x;
        ExtendedVertexList[2].Vertex[1] = BottomRight.y;
        ExtendedVertexList[2].Vertex[2] = 1;
	
	// Completely clipped away?
	if (BottomRight.x <= TopLeft.x) return;
	if (BottomRight.y <= TopLeft.y) return;
	
	// Use that texture
	auto TMgr = setupSpriteTexture(RenderRectangle, OGL_Txtr_WeaponsInHand, 0, renderStep);
	
	// Calculate the texture coordinates;
	// the scanline direction is downward, (texture coordinate 0)
	// while the line-to-line direction is rightward (texture coordinate 1)
	GLdouble U_Scale = TMgr->U_Scale/(RenderRectangle.y1 - RenderRectangle.y0);
	GLdouble V_Scale = TMgr->V_Scale/(RenderRectangle.x1 - RenderRectangle.x0);
	GLdouble U_Offset = TMgr->U_Offset;
	GLdouble V_Offset = TMgr->V_Offset;
	
	if (RenderRectangle.flip_vertical)
	{
		ExtendedVertexList[0].TexCoord[0] = U_Offset + U_Scale*(RenderRectangle.y1 - TopLeft.y);
		ExtendedVertexList[2].TexCoord[0] = U_Offset + U_Scale*(RenderRectangle.y1 - BottomRight.y);
	} else {
		ExtendedVertexList[0].TexCoord[0] = U_Offset + U_Scale*(TopLeft.y - RenderRectangle.y0);
		ExtendedVertexList[2].TexCoord[0] = U_Offset + U_Scale*(BottomRight.y - RenderRectangle.y0);
	}
	if (RenderRectangle.flip_horizontal)
	{
		ExtendedVertexList[0].TexCoord[1] = V_Offset + V_Scale*(RenderRectangle.x1 - TopLeft.x);
		ExtendedVertexList[2].TexCoord[1] = V_Offset + V_Scale*(RenderRectangle.x1 - BottomRight.x);
	} else {
		ExtendedVertexList[0].TexCoord[1] = V_Offset + V_Scale*(TopLeft.x - RenderRectangle.x0);
		ExtendedVertexList[2].TexCoord[1] = V_Offset + V_Scale*(BottomRight.x - RenderRectangle.x0);
	}
	
	// Fill in remaining points
	// Be sure that the order gives a sidedness the same as
	// that of the world-geometry polygons
	ExtendedVertexList[1].Vertex[0] = ExtendedVertexList[2].Vertex[0];
	ExtendedVertexList[1].Vertex[1] = ExtendedVertexList[0].Vertex[1];
	ExtendedVertexList[1].Vertex[2] = ExtendedVertexList[0].Vertex[2];
	ExtendedVertexList[1].TexCoord[0] = ExtendedVertexList[0].TexCoord[0];
	ExtendedVertexList[1].TexCoord[1] = ExtendedVertexList[2].TexCoord[1];
	ExtendedVertexList[3].Vertex[0] = ExtendedVertexList[0].Vertex[0];
	ExtendedVertexList[3].Vertex[1] = ExtendedVertexList[2].Vertex[1];
	ExtendedVertexList[3].Vertex[2] = ExtendedVertexList[2].Vertex[2];
	ExtendedVertexList[3].TexCoord[0] = ExtendedVertexList[2].TexCoord[0];
	ExtendedVertexList[3].TexCoord[1] = ExtendedVertexList[0].TexCoord[1];

	if (rotation_degrees != 0.0f)
	{
		constexpr double pi = 3.14159265358979323846;
		const double radians = rotation_degrees * pi / 180.0;
		const double cosine = std::cos(radians);
		const double sine = std::sin(radians);
		const double pivot_x =
			(RenderRectangle.x0 + RenderRectangle.x1) * 0.5;
		const double pivot_y = RenderRectangle.y1;
		for (auto& vertex : ExtendedVertexList)
		{
			const double x = vertex.Vertex[0] - pivot_x;
			const double y = vertex.Vertex[1] - pivot_y;
			vertex.Vertex[0] = pivot_x + x * cosine - y * sine;
			vertex.Vertex[1] = pivot_y + x * sine + y * cosine;
		}

		/*
		 * Rotation raises one of the quad's bottom corners. If the original
		 * sprite reached the bottom of the viewport, lower the rotated quad
		 * just enough to keep both corners off-screen and avoid an empty wedge.
		 */
		if (RenderRectangle.y1 >= RenderRectangle.clip_bottom)
		{
			const double lowest_bottom_edge = std::min(
				ExtendedVertexList[2].Vertex[1],
				ExtendedVertexList[3].Vertex[1]);
			if (lowest_bottom_edge < RenderRectangle.clip_bottom)
			{
				const double vertical_offset =
					RenderRectangle.clip_bottom - lowest_bottom_edge + 1.0;
				for (auto& vertex : ExtendedVertexList)
					vertex.Vertex[1] += vertical_offset;
			}
		}
	}

        if(TMgr->IsBlended() || TMgr->TransferMode == _tinted_transfer) {
		glEnable(GL_BLEND);
		setupBlendFunc(TMgr->NormalBlend());
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.001);
	} else {
		glDisable(GL_BLEND);
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.5);
	}

        glDisable(GL_DEPTH_TEST);

	// Location of data:
	glVertexPointer(3,GL_DOUBLE,sizeof(ExtendedVertexData),ExtendedVertexList[0].Vertex);
	glTexCoordPointer(2,GL_DOUBLE,sizeof(ExtendedVertexData),ExtendedVertexList[0].TexCoord);
	glEnable(GL_TEXTURE_2D);
		
	// Go!
        glDrawArrays(GL_POLYGON,0,4);

        if (setupGlow(view, TMgr, 0, 1, weaponFlare, selfLuminosity, 0, renderStep)) {
            glDrawArrays(GL_QUADS, 0, 4);
	}
	
	glEnable(GL_DEPTH_TEST);
        Shader::disable();
	TMgr->RestoreTextureMatrix();

}

#endif

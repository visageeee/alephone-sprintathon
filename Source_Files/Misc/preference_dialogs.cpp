/*

	Copyright (C) 2006 and beyond by Bungie Studios, Inc.
	and the "Aleph One" developers.
 
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
	
*/

#include "preference_dialogs.h"
#include "preferences.h"
#include "binders.h"
#include "OGL_Setup.h"
#include "screen.h"

#include <functional>
#include <iomanip>
#include <sstream>

class TexQualityPref : public Bindable<int>
{
public:
	TexQualityPref (int16& pref, int16 normal) : m_pref (pref), m_normal (normal) {}
	
	virtual int bind_export ()
	{
		int result = 0;
		int temp = m_pref;
		while (temp >= m_normal) {
			temp = temp >> 1;
			++result;
		}
		return result;
	}
	
	virtual void bind_import (int value)
	{
		m_pref = (value == 0) ? 0 : m_normal << (value - 1);
	}
	
protected:
	int16& m_pref;
	int16 m_normal;
};

class ColourPref : public Bindable<RGBColor>
{
public:
	ColourPref (RGBColor& pref) : m_pref (pref) {}
	
	virtual RGBColor bind_export () { return m_pref; }
	virtual void bind_import (RGBColor value) { m_pref = value; }
	
protected:
	RGBColor& m_pref;
};

class FarFilterPref : public Bindable<int>
{
public:
	FarFilterPref (int16& pref) : m_pref(pref) { }

	int bind_export() {
		if (m_pref == 5)
		{
			return 3;
		} 
		else if (m_pref == 3)
		{
			return 2;
		}
		else
		{
			return m_pref;
		}
	}

	void bind_import(int value) {
		if (value == 2)
		{
			m_pref = 3;
		}
		else if (value == 3)
		{
			m_pref = 5;
		}
		else
		{
			m_pref = value;
		}
	}

protected:
	int16& m_pref;
};

class TimesTwoPref : public Bindable<int>
{
public:
	TimesTwoPref (int16& pref) : m_pref (pref) {}
	
	virtual int bind_export ()
	{
		return (m_pref / 2);
	}
	
	virtual void bind_import (int value)
	{
		m_pref = value * 2;
	}

protected:
	int16& m_pref;
};

class AnisotropyPref : public Bindable<int>
{
public:
	AnisotropyPref (float& pref) : m_pref (pref) {}
	
	virtual int bind_export ()
	{
		int result = 0;
		int temp = static_cast<int> (m_pref);
		while (temp >= 1) {
			temp = temp >> 1;
			++result;
		}
		return result;
	}
	
	virtual void bind_import (int value)
	{
		m_pref = (value == 0) ? 0.0 : 1 << (value - 1);
	}

protected:
	float& m_pref;
};

OpenGLDialog::OpenGLDialog() {  }

OpenGLDialog::~OpenGLDialog()
{
	delete m_cancelWidget;
	delete m_okWidget;
	delete m_fogWidget;
	delete m_forceFogWidget;
	delete m_forceFogMediaRelativeWidget;
	delete m_forceFogAnimatedDensityWidget;
	delete m_forceFogDepthDensityWidget;
	delete m_forceFogBlackWidget;
	delete m_forceFogDistanceDarkeningWidget;
	delete m_deepFogHazeWidget;
	delete m_forceFogWeatherPresetWidget;
	delete m_colourEffectsWidget;
	delete m_transparentLiquidsWidget;
	delete m_3DmodelsWidget;
	delete m_perspectiveWidget;
	delete m_billboardWidget;
	delete m_animatedMediaRipplesWidget;
	delete m_animatedMediaRippleStrengthWidget;
	delete m_animatedMediaWetTextureStrengthWidget;
	delete m_animatedMediaRippleSpeedWidget;
	delete m_animatedLavaRippleSpeedWidget;
	delete m_animatedGooRippleSpeedWidget;
	delete m_animatedSewageRippleSpeedWidget;
	delete m_animatedJjaroRippleSpeedWidget;
	delete m_blurWidget;
	delete m_bumpWidget;
	delete m_colourTheVoidWidget;
	delete m_voidColourWidget;
	delete m_anisotropicWidget;
	delete m_sRGBWidget;
	delete m_useNPOTWidget;
	delete m_vsyncWidget;
	delete m_wallsFilterWidget;
	delete m_spritesFilterWidget;

	for (int i=0; i<OGL_NUMBER_OF_TEXTURE_TYPES; ++i) {
		delete m_textureQualityWidget [i];
		delete m_nearFiltersWidget[i];
	}

}

void OpenGLDialog::OpenGLPrefsByRunning ()
{
	m_cancelWidget->set_callback (std::bind (&OpenGLDialog::Stop, this, false));
	m_okWidget->set_callback (std::bind (&OpenGLDialog::Stop, this, true));
	
	BinderSet binders;
	
	BitPref fogPref (
		graphics_preferences->OGL_Configure.Flags,
		OGL_Flag_Fog);
	binders.insert<bool> (m_fogWidget, &fogPref);

	BitPref forceFogPref (
		graphics_preferences->OGL_Configure.Flags,
		OGL_Flag_ForceFog);
	binders.insert<bool> (m_forceFogWidget, &forceFogPref);
	BoolPref forceFogMediaRelativePref (
		graphics_preferences->OGL_Configure.ForceFogMediaRelative);
	binders.insert<bool> (
		m_forceFogMediaRelativeWidget,
		&forceFogMediaRelativePref);
	BoolPref forceFogAnimatedDensityPref (
		graphics_preferences->OGL_Configure.ForceFogAnimatedDensity);
	binders.insert<bool> (
		m_forceFogAnimatedDensityWidget,
		&forceFogAnimatedDensityPref);
	BoolPref forceFogDepthDensityPref (
		graphics_preferences->OGL_Configure.ForceFogDepthDensity);
	binders.insert<bool> (
		m_forceFogDepthDensityWidget,
		&forceFogDepthDensityPref);
	BoolPref forceFogBlackPref (
		graphics_preferences->OGL_Configure.ForceFogBlack);
	binders.insert<bool> (
		m_forceFogBlackWidget,
		&forceFogBlackPref);
	BoolPref forceFogDistanceDarkeningPref (
		graphics_preferences->OGL_Configure.ForceFogDistanceDarkening);
	binders.insert<bool> (
		m_forceFogDistanceDarkeningWidget,
		&forceFogDistanceDarkeningPref);
	BoolPref deepFogHazePref (
		graphics_preferences->OGL_Configure.DeepFogHaze);
	binders.insert<bool> (m_deepFogHazeWidget, &deepFogHazePref);
	Int16Pref forceFogWeatherPresetPref (
		graphics_preferences->OGL_Configure.ForceFogWeatherPreset);
	binders.insert<int> (
		m_forceFogWeatherPresetWidget,
		&forceFogWeatherPresetPref);
	BitPref colourEffectsPref (graphics_preferences->OGL_Configure.Flags, OGL_Flag_Fader);
	binders.insert<bool> (m_colourEffectsWidget, &colourEffectsPref);
	BitPref transparentLiquidsPref (graphics_preferences->OGL_Configure.Flags, OGL_Flag_LiqSeeThru);
	binders.insert<bool> (m_transparentLiquidsWidget, &transparentLiquidsPref);
	BitPref modelsPref (graphics_preferences->OGL_Configure.Flags, OGL_Flag_3D_Models);
	binders.insert<bool> (m_3DmodelsWidget, &modelsPref);
	BitPref blurPref (graphics_preferences->OGL_Configure.Flags, OGL_Flag_Blur);
	binders.insert<bool> (m_blurWidget, &blurPref);
	BitPref bumpPref (graphics_preferences->OGL_Configure.Flags, OGL_Flag_BumpMap);
	binders.insert<bool> (m_bumpWidget, &bumpPref);
	BoolPref animatedMediaRipplesPref (
		graphics_preferences->OGL_Configure.AnimatedMediaRipples);
	binders.insert<bool> (
		m_animatedMediaRipplesWidget,
		&animatedMediaRipplesPref);
	Int16Pref animatedMediaRippleStrengthPref (
		graphics_preferences->OGL_Configure.AnimatedMediaRippleStrength);
	binders.insert<int> (
		m_animatedMediaRippleStrengthWidget,
		&animatedMediaRippleStrengthPref);
	Int16Pref animatedMediaWetTextureStrengthPref (
		graphics_preferences->OGL_Configure.AnimatedMediaWetTextureStrength);
	binders.insert<int> (
		m_animatedMediaWetTextureStrengthWidget,
		&animatedMediaWetTextureStrengthPref);
	Int16Pref animatedMediaRippleSpeedPref (
		graphics_preferences->OGL_Configure.AnimatedMediaRippleSpeed);
	binders.insert<int> (
		m_animatedMediaRippleSpeedWidget,
		&animatedMediaRippleSpeedPref);
	Int16Pref animatedLavaRippleSpeedPref (
		graphics_preferences->OGL_Configure.AnimatedLavaRippleSpeed);
	binders.insert<int> (m_animatedLavaRippleSpeedWidget,
		&animatedLavaRippleSpeedPref);
	Int16Pref animatedGooRippleSpeedPref (
		graphics_preferences->OGL_Configure.AnimatedGooRippleSpeed);
	binders.insert<int> (m_animatedGooRippleSpeedWidget,
		&animatedGooRippleSpeedPref);
	Int16Pref animatedSewageRippleSpeedPref (
		graphics_preferences->OGL_Configure.AnimatedSewageRippleSpeed);
	binders.insert<int> (m_animatedSewageRippleSpeedWidget,
		&animatedSewageRippleSpeedPref);
	Int16Pref animatedJjaroRippleSpeedPref (
		graphics_preferences->OGL_Configure.AnimatedJjaroRippleSpeed);
	binders.insert<int> (m_animatedJjaroRippleSpeedWidget,
		&animatedJjaroRippleSpeedPref);
	BitPref perspectivePref (graphics_preferences->OGL_Configure.Flags, OGL_Flag_MimicSW, true);
	binders.insert<bool> (m_perspectiveWidget, &perspectivePref);

	BoolPref billboardPref (graphics_preferences->OGL_Configure.BillboardXY);
	binders.insert<bool> (m_billboardWidget, &billboardPref);
	
	BitPref colourTheVoidPref (graphics_preferences->OGL_Configure.Flags, OGL_Flag_VoidColor);
	binders.insert<bool> (m_colourTheVoidWidget, &colourTheVoidPref);
	ColourPref voidColourPref (graphics_preferences->OGL_Configure.VoidColor);
	binders.insert<RGBColor> (m_voidColourWidget, &voidColourPref);
	
	AnisotropyPref anisotropyPref (graphics_preferences->OGL_Configure.AnisotropyLevel);
	binders.insert<int> (m_anisotropicWidget, &anisotropyPref);

	BoolPref sRGBPref (graphics_preferences->OGL_Configure.Use_sRGB);
	binders.insert<bool> (m_sRGBWidget, &sRGBPref);
	
	BoolPref useNPOTPref (graphics_preferences->OGL_Configure.Use_NPOT);
	binders.insert<bool> (m_useNPOTWidget, &useNPOTPref);
	
	BoolPref vsyncPref (graphics_preferences->OGL_Configure.WaitForVSync);
	binders.insert<bool> (m_vsyncWidget, &vsyncPref);

	Int16Pref ephemeraQualityPref(graphics_preferences->ephemera_quality);
	binders.insert<int>(m_ephemeraQualityWidget, &ephemeraQualityPref);
	
	FarFilterPref wallsFarFilterPref (graphics_preferences->OGL_Configure.TxtrConfigList [OGL_Txtr_Wall].FarFilter);
	binders.insert<int> (m_wallsFilterWidget, &wallsFarFilterPref);
	FarFilterPref spritesFarFilterPref (graphics_preferences->OGL_Configure.TxtrConfigList [OGL_Txtr_Inhabitant].FarFilter);
	binders.insert<int> (m_spritesFilterWidget, &spritesFarFilterPref);

	Int16Pref wallsNearFilterPref (graphics_preferences->OGL_Configure.TxtrConfigList[OGL_Txtr_Wall].NearFilter);
	binders.insert<int> (m_nearFiltersWidget[0], &wallsNearFilterPref);
	Int16Pref landscapeNearFilterPref (graphics_preferences->OGL_Configure.TxtrConfigList[OGL_Txtr_Landscape].NearFilter);
	binders.insert<int> (m_nearFiltersWidget[1], &landscapeNearFilterPref);
	Int16Pref spriteNearFilterPref (graphics_preferences->OGL_Configure.TxtrConfigList[OGL_Txtr_Inhabitant].NearFilter);
	binders.insert<int> (m_nearFiltersWidget[2], &spriteNearFilterPref);
	Int16Pref weaponNearFilterPref (graphics_preferences->OGL_Configure.TxtrConfigList[OGL_Txtr_WeaponsInHand].NearFilter);
	binders.insert<int> (m_nearFiltersWidget[3], &weaponNearFilterPref);
	Int16Pref hudNearFilterPref(graphics_preferences->OGL_Configure.TxtrConfigList[OGL_Txtr_HUD].NearFilter);
	binders.insert<int> (m_nearFiltersWidget[4], &hudNearFilterPref);
	
	TexQualityPref wallQualityPref (graphics_preferences->OGL_Configure.TxtrConfigList [0].MaxSize, 128);
	binders.insert<int> (m_textureQualityWidget [0], &wallQualityPref);
	TexQualityPref landscapeQualityPref (graphics_preferences->OGL_Configure.TxtrConfigList [1].MaxSize, 256);
	binders.insert<int> (m_textureQualityWidget [1], &landscapeQualityPref);
	TexQualityPref spriteQualityPref (graphics_preferences->OGL_Configure.TxtrConfigList [2].MaxSize, 256);
	binders.insert<int> (m_textureQualityWidget [2], &spriteQualityPref);
	TexQualityPref weaponQualityPref (graphics_preferences->OGL_Configure.TxtrConfigList [3].MaxSize, 256);
	binders.insert<int> (m_textureQualityWidget [3], &weaponQualityPref);
	TexQualityPref hudQualityPref(graphics_preferences->OGL_Configure.TxtrConfigList[4].MaxSize, 256);
	binders.insert<int>(m_textureQualityWidget[4], &hudQualityPref);
	TexQualityPref modelQualityPref (graphics_preferences->OGL_Configure.ModelConfig.MaxSize, 256);
	binders.insert<int> (m_modelQualityWidget, &modelQualityPref);
	
	// Set initial values from prefs
	binders.migrate_all_second_to_first ();
	
	bool result = Run ();
	
	if (result) {
		// migrate prefs and save
		binders.migrate_all_first_to_second ();
		write_preferences ();
	}
}

static const char *far_filter_labels[5] = {
	"None", "Linear", "Bilinear", "Trilinear", NULL
};

static const char *near_filter_labels[3] = {
	"None", "Linear", NULL
};

static std::vector<std::string> ephemera_quality_labels {
	"Off",
	"Low",
	"Medium",
	"High",
	"Ultra"
};

class w_aniso_slider : public w_slider {
public:
	w_aniso_slider(int num_items, int sel) : w_slider(num_items, sel) {
		init_formatted_value();
	}
	
	virtual std::string formatted_value(void) {
		std::ostringstream ss;
		ss << ((selection == 0) ? 0 : 1 << (selection - 1));
		return ss.str();
	}
};

class w_media_ripple_speed_slider : public w_slider {
public:
	w_media_ripple_speed_slider(int sel) : w_slider(28, sel) {
		init_formatted_value();
	}

	virtual std::string formatted_value(void) {
		std::ostringstream ss;
		ss << std::fixed << std::setprecision(2)
			<< ((selection + 1) * 0.25f) << "x";
		return ss.str();
	}
};

class w_media_ripple_strength_slider : public w_slider {
public:
	w_media_ripple_strength_slider(int sel) : w_slider(16, sel) {
		init_formatted_value();
	}

	virtual std::string formatted_value(void) {
		std::ostringstream ss;
		ss << std::fixed << std::setprecision(2)
			<< ((selection + 1) * 0.25f) << "x";
		return ss.str();
	}
};

class w_media_wet_texture_strength_slider : public w_slider {
public:
	w_media_wet_texture_strength_slider(int sel) : w_slider(17, sel) {
		init_formatted_value();
	}

	virtual std::string formatted_value(void) {
		if (selection == 0) return "Off";
		std::ostringstream ss;
		ss << std::fixed << std::setprecision(2)
			<< (selection * 0.25f) << "x";
		return ss.str();
	}
};


class SdlOpenGLDialog : public OpenGLDialog
{
public:
	SdlOpenGLDialog (int theSelectedRenderer)
	{

		vertical_placer *placer = new vertical_placer;
		placer->dual_add(new w_title("OPENGL OPTIONS"), m_dialog);
		placer->add(new w_spacer(), true);
		
		// horizontal_placer *tabs_placer = new horizontal_placer;
		// w_button *w_general_tab = new w_button("GENERAL");
		// w_general_tab->set_callback(choose_generic_tab, static_cast<void *>(this));
		// tabs_placer->dual_add(w_general_tab, m_dialog);
		// w_button *w_advanced_tab = new w_button("ADVANCED");
		// w_advanced_tab->set_callback(choose_advanced_tab, static_cast<void *>(this));
		// tabs_placer->dual_add(w_advanced_tab, m_dialog);
		// placer->add(tabs_placer, true);

		m_tabs = new tab_placer();

		std::vector<std::string> labels;
		labels.push_back("GRAPHICS");
		labels.push_back("LIQUIDS");
		labels.push_back("FOG");
		w_tab *tabs = new w_tab(labels, m_tabs);
		placer->dual_add(tabs, m_dialog);
		
		placer->add(new w_spacer(), true);

		table_placer *general_table = new table_placer(2, get_theme_space(ITEM_WIDGET), true);
		general_table->col_flags(0, placeable::kAlignRight);
		general_table->col_flags(1, placeable::kAlignLeft);
		table_placer *liquids_table = new table_placer(2, get_theme_space(ITEM_WIDGET), true);
		liquids_table->col_flags(0, placeable::kAlignRight);
		liquids_table->col_flags(1, placeable::kAlignLeft);
		table_placer *fog_table = new table_placer(2, get_theme_space(ITEM_WIDGET), true);
		fog_table->col_flags(0, placeable::kAlignRight);
		fog_table->col_flags(1, placeable::kAlignLeft);
		
		w_toggle *fog_w = new w_toggle(false);
		fog_table->dual_add(fog_w->label("Fog"), m_dialog);
		fog_table->dual_add(fog_w, m_dialog);

		w_toggle *force_fog_w = new w_toggle(false);
		fog_table->dual_add(
			force_fog_w->label("Fog in All Levels"),
			m_dialog);
		fog_table->dual_add(force_fog_w, m_dialog);

		w_toggle *force_fog_media_relative_w = new w_toggle(false);
		fog_table->dual_add(
			force_fog_media_relative_w->label("Media-relative Forced Fog"),
			m_dialog);
		fog_table->dual_add(force_fog_media_relative_w, m_dialog);

		w_select_popup *force_fog_weather_preset_w = new w_select_popup();
		std::vector<std::string> fog_weather_labels;
		fog_weather_labels.push_back("Neutral Mist");
		fog_weather_labels.push_back("Heavy Fog");
		fog_weather_labels.push_back("Toxic Haze");
		fog_weather_labels.push_back("Dust");
		force_fog_weather_preset_w->set_labels(fog_weather_labels);
		fog_table->dual_add(
			force_fog_weather_preset_w->label("Weather Preset"), m_dialog);
		fog_table->dual_add(force_fog_weather_preset_w, m_dialog);

		w_toggle *force_fog_animated_density_w = new w_toggle(false);
		fog_table->dual_add(
			force_fog_animated_density_w->label("Animated Density"), m_dialog);
		fog_table->dual_add(force_fog_animated_density_w, m_dialog);

		w_toggle *force_fog_depth_density_w = new w_toggle(false);
		fog_table->dual_add(
			force_fog_depth_density_w->label("Density Increases with Depth"),
			m_dialog);
		fog_table->dual_add(force_fog_depth_density_w, m_dialog);

		w_toggle *force_fog_black_w = new w_toggle(false);
		fog_table->dual_add(
			force_fog_black_w->label("Black Fog"), m_dialog);
		fog_table->dual_add(force_fog_black_w, m_dialog);

		w_toggle *force_fog_distance_darkening_w = new w_toggle(false);
		fog_table->dual_add(
			force_fog_distance_darkening_w->label("Darken with Distance"),
			m_dialog);
		fog_table->dual_add(force_fog_distance_darkening_w, m_dialog);

		w_toggle *deep_fog_haze_w = new w_toggle(true);
		fog_table->dual_add(
			deep_fog_haze_w->label("Deep Fog Haze"), m_dialog);
		fog_table->dual_add(deep_fog_haze_w, m_dialog);

		w_toggle *fader_w = new w_toggle(false);
		general_table->dual_add(fader_w->label("Color Effects"), m_dialog);
		general_table->dual_add(fader_w, m_dialog);

		w_toggle *liq_w = new w_toggle(false);
		liquids_table->dual_add(liq_w->label("Transparent Liquids"), m_dialog);
		liquids_table->dual_add(liq_w, m_dialog);

		w_toggle *models_w = new w_toggle(false);
		general_table->dual_add(models_w->label("3D Models"), m_dialog);
		general_table->dual_add(models_w, m_dialog);

		w_enabling_toggle *perspective_w = new w_enabling_toggle(false);
		w_toggle *animated_media_ripples_w = new w_toggle(true);
		w_media_ripple_strength_slider *animated_media_ripple_strength_w =
			new w_media_ripple_strength_slider(3);
		w_media_wet_texture_strength_slider *animated_media_wet_texture_strength_w =
			new w_media_wet_texture_strength_slider(4);
		w_media_ripple_speed_slider *animated_media_ripple_speed_w =
			new w_media_ripple_speed_slider(3);
		w_media_ripple_speed_slider *animated_lava_ripple_speed_w =
			new w_media_ripple_speed_slider(1);
		w_media_ripple_speed_slider *animated_goo_ripple_speed_w =
			new w_media_ripple_speed_slider(2);
		w_media_ripple_speed_slider *animated_sewage_ripple_speed_w =
			new w_media_ripple_speed_slider(1);
		w_media_ripple_speed_slider *animated_jjaro_ripple_speed_w =
			new w_media_ripple_speed_slider(3);
		general_table->dual_add(perspective_w->label("3D Perspective"), m_dialog);

		auto billboard_placer = new horizontal_placer(get_theme_space(ITEM_WIDGET));

		w_toggle* billboard_w = new w_toggle(false);
		billboard_placer->add_flags(placeable::kAlignLeft);
		billboard_placer->dual_add(perspective_w, m_dialog);
		billboard_placer->add_flags(placeable::kFill);
		billboard_placer->add(new w_spacer(), true);
		billboard_placer->dual_add(billboard_w->label("Tilt Sprites with Camera"), m_dialog);
		billboard_placer->dual_add(billboard_w, m_dialog);

		perspective_w->add_dependent_widget(billboard_w);
		billboard_w->set_enabled(!(graphics_preferences->OGL_Configure.Flags & OGL_Flag_MimicSW));

		general_table->add(billboard_placer, true);

		w_toggle *blur_w = new w_toggle(false);
		general_table->dual_add(blur_w->label("Bloom Effects"), m_dialog);
		general_table->dual_add(blur_w, m_dialog);
		
		w_toggle *bump_w = new w_toggle(false);
		general_table->dual_add(bump_w->label("Bump Mapping"), m_dialog);
		general_table->dual_add(bump_w, m_dialog);

		w_select_popup* ephemera_w = new w_select_popup();
		ephemera_w->set_labels(ephemera_quality_labels);
		general_table->dual_add(ephemera_w->label("Scripted Effects Quality"), m_dialog);
		general_table->dual_add(ephemera_w, m_dialog);
		
		general_table->add_row(new w_spacer(), true);

		w_toggle *vsync_w = new w_toggle(false);
		general_table->dual_add(vsync_w->label("VSync"), m_dialog);
		general_table->dual_add(vsync_w, m_dialog);

		w_aniso_slider* aniso_w = new w_aniso_slider(6, 1);
		general_table->dual_add(aniso_w->label("Anisotropic Filtering"),m_dialog);
		general_table->dual_add(aniso_w, m_dialog);

		w_toggle *srgb_w = new w_toggle(false);
//		general_table->dual_add(srgb_w->label("Gamma-corrected Blending"), m_dialog);
//		general_table->dual_add(srgb_w, m_dialog);


		general_table->add_row(new w_spacer(), true);

		general_table->dual_add_row(new w_static_text("Replacement Texture Quality"), m_dialog);
	
		w_select_popup *texture_quality_wa[OGL_NUMBER_OF_TEXTURE_TYPES];
		for (int i = 0; i < OGL_NUMBER_OF_TEXTURE_TYPES; i++) texture_quality_wa[i] = NULL;
		
		texture_quality_wa[OGL_Txtr_Wall] =  new w_select_popup ();
		general_table->dual_add(texture_quality_wa[OGL_Txtr_Wall]->label("Walls"), m_dialog);
		general_table->dual_add(texture_quality_wa[OGL_Txtr_Wall], m_dialog);
		
		texture_quality_wa[OGL_Txtr_Landscape] = new w_select_popup ();
		general_table->dual_add(texture_quality_wa[OGL_Txtr_Landscape]->label("Landscapes"), m_dialog);
		general_table->dual_add(texture_quality_wa[OGL_Txtr_Landscape], m_dialog);

		texture_quality_wa[OGL_Txtr_Inhabitant] = new w_select_popup ();
		general_table->dual_add(texture_quality_wa[OGL_Txtr_Inhabitant]->label("Sprites"), m_dialog);
		general_table->dual_add(texture_quality_wa[OGL_Txtr_Inhabitant], m_dialog);

		texture_quality_wa[OGL_Txtr_WeaponsInHand] = new w_select_popup ();
		general_table->dual_add(texture_quality_wa[OGL_Txtr_WeaponsInHand]->label("Weapons in Hand"), m_dialog);
		general_table->dual_add(texture_quality_wa[OGL_Txtr_WeaponsInHand], m_dialog);

		texture_quality_wa[OGL_Txtr_HUD] = new w_select_popup();
		general_table->dual_add(texture_quality_wa[OGL_Txtr_HUD]->label("HUD / Terminals"), m_dialog);
		general_table->dual_add(texture_quality_wa[OGL_Txtr_HUD], m_dialog);

		w_select_popup *model_quality_w = new w_select_popup();
		general_table->dual_add(model_quality_w->label("3D Model Skins"), m_dialog);
		general_table->dual_add(model_quality_w, m_dialog);
	
		vector<string> tex_quality_strings;
		tex_quality_strings.push_back ("Unlimited");
		tex_quality_strings.push_back ("Normal");
		tex_quality_strings.push_back ("High");
		tex_quality_strings.push_back ("Higher");
		tex_quality_strings.push_back ("Highest");
	
		for (int i = 0; i < OGL_NUMBER_OF_TEXTURE_TYPES; i++) {
			if (texture_quality_wa[i]) {
				texture_quality_wa[i]->set_labels (tex_quality_strings);
			}
		}
		model_quality_w->set_labels(tex_quality_strings);

		liquids_table->add_row(new w_spacer(), true);
		liquids_table->dual_add(
			animated_media_ripples_w->label("Animated Media Ripples"), m_dialog);
		liquids_table->dual_add(animated_media_ripples_w, m_dialog);
		liquids_table->dual_add(
			animated_media_ripple_strength_w->label("Ripple Strength"), m_dialog);
		liquids_table->dual_add(animated_media_ripple_strength_w, m_dialog);
		liquids_table->dual_add(
			animated_media_wet_texture_strength_w->label("Wet Texture Strength"), m_dialog);
		liquids_table->dual_add(animated_media_wet_texture_strength_w, m_dialog);
		liquids_table->add_row(new w_spacer(), true);
		liquids_table->dual_add_row(
			new w_static_text("Animation Speed by Media Type"), m_dialog);
		liquids_table->dual_add(
			animated_media_ripple_speed_w->label("Water Ripple Speed"), m_dialog);
		liquids_table->dual_add(animated_media_ripple_speed_w, m_dialog);
		liquids_table->dual_add(
			animated_lava_ripple_speed_w->label("Lava Ripple Speed"), m_dialog);
		liquids_table->dual_add(animated_lava_ripple_speed_w, m_dialog);
		liquids_table->dual_add(
			animated_goo_ripple_speed_w->label("Goo Ripple Speed"), m_dialog);
		liquids_table->dual_add(animated_goo_ripple_speed_w, m_dialog);
		liquids_table->dual_add(
			animated_sewage_ripple_speed_w->label("Sewage Ripple Speed"), m_dialog);
		liquids_table->dual_add(animated_sewage_ripple_speed_w, m_dialog);
		liquids_table->dual_add(
			animated_jjaro_ripple_speed_w->label("Jjaro Ripple Speed"), m_dialog);
		liquids_table->dual_add(animated_jjaro_ripple_speed_w, m_dialog);
	
		w_toggle *use_npot_w = new w_toggle(false);
		general_table->add_row(new w_spacer(), true);
		general_table->dual_add(use_npot_w->label("Non-Power-of-Two Textures"), m_dialog);
		general_table->dual_add(use_npot_w, m_dialog);
		general_table->dual_add_row(new w_static_text("Non-power-of-two textures conserve memory,"), m_dialog);
		general_table->dual_add_row(new w_static_text("but cause problems on some machines."), m_dialog);

		general_table->add_row(new w_spacer(), true);
		general_table->dual_add_row(new w_static_text("Texture Filtering"), m_dialog);

		w_select* near_filter_wa[OGL_NUMBER_OF_TEXTURE_TYPES];
		w_select* far_filter_wa[OGL_NUMBER_OF_TEXTURE_TYPES];
		for (int i = 0; i < OGL_NUMBER_OF_TEXTURE_TYPES; ++i)
		{
			near_filter_wa[i] = new w_select(0, near_filter_labels);
			if (i == OGL_Txtr_Wall || i == OGL_Txtr_Inhabitant)
				far_filter_wa[i] = new w_select(0, far_filter_labels);
			else
				far_filter_wa[i] = NULL;
		}
		
		w_label* near_filter_labels[OGL_NUMBER_OF_TEXTURE_TYPES];
		near_filter_labels[OGL_Txtr_Wall] = new w_label("Walls");
		near_filter_labels[OGL_Txtr_Inhabitant] = new w_label("Sprites");
		near_filter_labels[OGL_Txtr_Landscape] = new w_label("Landscapes");
		near_filter_labels[OGL_Txtr_WeaponsInHand] = new w_label("Weapons in Hand");
		near_filter_labels[OGL_Txtr_HUD] = new w_label("HUD / Terminals");
	
		table_placer *ftable = new table_placer(3, get_theme_space(ITEM_WIDGET));
		
		ftable->col_flags(0, placeable::kAlignRight);
		ftable->col_flags(1, placeable::kAlignLeft);
		ftable->col_flags(2, placeable::kAlignLeft);
		
		ftable->add(new w_spacer(), true);
		ftable->dual_add(new w_label("Near"), m_dialog);
		ftable->dual_add(new w_label("Distant"), m_dialog);
		
		for (int i = 0; i < OGL_NUMBER_OF_TEXTURE_TYPES; ++i)
		{
			ftable->dual_add(near_filter_labels[i], m_dialog);
			ftable->dual_add(near_filter_wa[i], m_dialog);
			near_filter_wa[i]->associate_label(near_filter_labels[i]);

			if (far_filter_wa[i])
			{
				ftable->dual_add(far_filter_wa[i], m_dialog);
				far_filter_wa[i]->associate_label(near_filter_labels[i]);
			}
			else
				ftable->add(new w_spacer(), true);
		}
		
		ftable->col_min_width(1, (ftable->col_width(0) - get_theme_space(ITEM_WIDGET)) / 2);
		ftable->col_min_width(2, (ftable->col_width(0) - get_theme_space(ITEM_WIDGET)) / 2);
		
		// The filtering grid spans the complete Graphics tab. Adding it as a
		// single table cell leaves the second column null and crashes layout.
		general_table->add_row(ftable, true);

		m_tabs->add(general_table, true);
		m_tabs->add(liquids_table, true);
		m_tabs->add(fog_table, true);
		placer->add(m_tabs, false);
	
		placer->add(new w_spacer(), true);

		horizontal_placer *button_placer = new horizontal_placer;
		w_button* ok_w = new w_button("ACCEPT");
		button_placer->dual_add(ok_w, m_dialog);
		
		w_button* cancel_w = new w_button("CANCEL");
		button_placer->dual_add(cancel_w, m_dialog);
		placer->add(button_placer, true);

		m_dialog.set_widget_placer(placer);

		m_cancelWidget = new ButtonWidget (cancel_w);
		m_okWidget = new ButtonWidget (ok_w);
		
		m_fogWidget = new ToggleWidget (fog_w);
		m_forceFogWidget = new ToggleWidget (force_fog_w);
		m_forceFogMediaRelativeWidget =
			new ToggleWidget (force_fog_media_relative_w);
		m_forceFogAnimatedDensityWidget =
			new ToggleWidget (force_fog_animated_density_w);
		m_forceFogDepthDensityWidget =
			new ToggleWidget (force_fog_depth_density_w);
		m_forceFogBlackWidget =
			new ToggleWidget (force_fog_black_w);
		m_forceFogDistanceDarkeningWidget =
			new ToggleWidget (force_fog_distance_darkening_w);
		m_deepFogHazeWidget = new ToggleWidget (deep_fog_haze_w);
		m_forceFogWeatherPresetWidget =
			new PopupSelectorWidget (force_fog_weather_preset_w);
		m_colourEffectsWidget = new ToggleWidget (fader_w);
		m_transparentLiquidsWidget = new ToggleWidget (liq_w);
		m_3DmodelsWidget = new ToggleWidget (models_w);
		m_blurWidget = new ToggleWidget (blur_w);
		m_bumpWidget = new ToggleWidget (bump_w);
		m_perspectiveWidget = new ToggleWidget (perspective_w);
		m_billboardWidget = new ToggleWidget (billboard_w);
		m_animatedMediaRipplesWidget =
			new ToggleWidget (animated_media_ripples_w);
		m_animatedMediaRippleStrengthWidget =
			new SliderSelectorWidget (animated_media_ripple_strength_w);
		m_animatedMediaWetTextureStrengthWidget =
			new SliderSelectorWidget (animated_media_wet_texture_strength_w);
		m_animatedMediaRippleSpeedWidget =
			new SliderSelectorWidget (animated_media_ripple_speed_w);
		m_animatedLavaRippleSpeedWidget =
			new SliderSelectorWidget (animated_lava_ripple_speed_w);
		m_animatedGooRippleSpeedWidget =
			new SliderSelectorWidget (animated_goo_ripple_speed_w);
		m_animatedSewageRippleSpeedWidget =
			new SliderSelectorWidget (animated_sewage_ripple_speed_w);
		m_animatedJjaroRippleSpeedWidget =
			new SliderSelectorWidget (animated_jjaro_ripple_speed_w);

		m_colourTheVoidWidget = 0;
		m_voidColourWidget = 0;

		m_ephemeraQualityWidget = new PopupSelectorWidget(ephemera_w);

		m_anisotropicWidget = new SliderSelectorWidget (aniso_w);

		m_sRGBWidget = new ToggleWidget(srgb_w);

		m_useNPOTWidget = new ToggleWidget (use_npot_w);
		m_vsyncWidget = new ToggleWidget (vsync_w);
		
		m_wallsFilterWidget = new SelectSelectorWidget (far_filter_wa[OGL_Txtr_Wall]);
		m_spritesFilterWidget = new SelectSelectorWidget (far_filter_wa[OGL_Txtr_Inhabitant]);

		for (int i = 0; i < OGL_NUMBER_OF_TEXTURE_TYPES; ++i) {
			m_textureQualityWidget [i] = new PopupSelectorWidget (texture_quality_wa[i]);
			m_nearFiltersWidget[i] = new SelectSelectorWidget(near_filter_wa[i]);
		}
		m_modelQualityWidget = new PopupSelectorWidget(model_quality_w);
	}

	~SdlOpenGLDialog() {
		delete m_tabs;
	}

	virtual bool Run ()
	{	
		return (m_dialog.run () == 0);
	}

	virtual void Stop (bool result)
	{
		m_dialog.quit (result ? 0 : -1);
	}

	static void choose_generic_tab(void *arg);
	static void choose_advanced_tab(void *arg);

private:
	enum {
		TAB_WIDGET = 400,
		BASIC_TAB,
		ADVANCED_TAB
	};
	
	tab_placer* m_tabs;
	dialog m_dialog;
};

void SdlOpenGLDialog::choose_generic_tab(void *arg)
{
	SdlOpenGLDialog *d = static_cast<SdlOpenGLDialog *>(arg);
	d->m_tabs->choose_tab(0);
	d->m_dialog.draw();
}

void SdlOpenGLDialog::choose_advanced_tab(void *arg)
{
	SdlOpenGLDialog *d = static_cast<SdlOpenGLDialog *>(arg);
	d->m_tabs->choose_tab(1);
	d->m_dialog.draw();
}

std::unique_ptr<OpenGLDialog>
OpenGLDialog::Create(int theSelectedRenderer)
{
	return std::make_unique<SdlOpenGLDialog>(theSelectedRenderer);
}

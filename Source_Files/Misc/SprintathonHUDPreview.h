#ifndef SPRINTATHON_HUD_PREVIEW_H
#define SPRINTATHON_HUD_PREVIEW_H
#include "ImageLoader.h"
// Optional static artwork, never execute a HUD script in the preferences dialog.
class w_hud_preview : public widget {
public:
    w_hud_preview() : widget(MESSAGE_WIDGET) {
        saved_min_width=scale_dialog_value(430);
        saved_min_height=scale_dialog_value(242);
    }
    ~w_hud_preview(){if(preview)SDL_FreeSurface(preview);}
    bool is_selectable() const override {return false;}
    void show(const Plugin* plugin){
        if(preview){SDL_FreeSurface(preview);preview=nullptr;}
        message=plugin?"This HUD has no preview image.":"Classic built-in HUD";
        if(plugin){
            FileSpecifier file=plugin->directory+"hud-preview.png";
            ImageDescriptor image;
            if(image.LoadFromFile(file,ImageLoader_Colors,0) && image.IsPresent() && image.GetFormat()==ImageDescriptor::RGBA8 && image.GetWidth()>0 && image.GetHeight()>0){
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
                const Uint32 r=0xff,g=0xff00,b=0xff0000,a=0xff000000;
#else
                const Uint32 r=0xff000000,g=0xff0000,b=0xff00,a=0xff;
#endif
                SDL_Surface* source=SDL_CreateRGBSurfaceFrom(image.GetBuffer(),image.GetWidth(),image.GetHeight(),32,image.GetWidth()*4,r,g,b,a);
                if(source){
                    const double scale=std::min(double(saved_min_width-4)/source->w,double(saved_min_height-4)/source->h);
                    const int w=std::max(1,int(source->w*scale)),h=std::max(1,int(source->h*scale));
                    preview=SDL_CreateRGBSurface(0,w,h,32,r,g,b,a);
                    SDL_SetSurfaceBlendMode(source,SDL_BLENDMODE_NONE);
                    if(preview){
                        SDL_FillRect(preview,nullptr,0);
                        if(SDL_BlitScaled(source,nullptr,preview,nullptr)!=0){SDL_FreeSurface(preview);preview=nullptr;}
                        else SDL_SetSurfaceBlendMode(preview,SDL_BLENDMODE_BLEND);
                    }
                    SDL_FreeSurface(source);
                }
            }
        }
        dirty=true;
    }
    void draw(SDL_Surface* surface) const override {
        SDL_Rect area=rect;
        SDL_FillRect(surface,&area,SDL_MapRGB(surface->format,12,18,20));
        draw_rectangle(surface,&area,SDL_MapRGB(surface->format,85,105,105));
        if(preview){
            SDL_Rect dest={rect.x+(rect.w-preview->w)/2,rect.y+(rect.h-preview->h)/2,preview->w,preview->h};
            SDL_BlitSurface(preview,nullptr,surface,&dest);
        }else{
            draw_text(surface,message.c_str(),rect.x+scale_dialog_value(16),rect.y+rect.h/2,
                SDL_MapRGB(surface->format,190,200,200),font,style);
        }
    }
private:
    SDL_Surface* preview=nullptr;
    std::string message;
};
#endif

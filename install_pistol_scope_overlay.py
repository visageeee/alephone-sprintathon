#!/usr/bin/env python3
from pathlib import Path

path = Path("Source_Files/RenderOther/screen.cpp")
text = path.read_text()

if "static OGL_Blitter pistol_scope;" in text:
    print("Scope artwork draw is already installed.")
    raise SystemExit(0)

needle = """\tglColor4f(0.90f, 0.24f, 0.035f, 0.065f*amount);
\tglBegin(GL_QUADS);
\tglVertex2f(0.f, 0.f);
\tglVertex2f(right, 0.f);
\tglVertex2f(right, bottom);
\tglVertex2f(0.f, bottom);
\tglEnd();
"""

overlay = """\tglColor4f(0.90f, 0.24f, 0.035f, 0.065f*amount);
\tglBegin(GL_QUADS);
\tglVertex2f(0.f, 0.f);
\tglVertex2f(right, 0.f);
\tglVertex2f(right, bottom);
\tglVertex2f(0.f, bottom);
\tglEnd();

\t/* Keep the reticle and metal rim sharp while the peripheral scene blurs. */
\tif (scope_amount>0.001f)
\t{
\t\tstatic OGL_Blitter pistol_scope;
\t\tstatic bool load_attempted= false;
\t\tif (!load_attempted)
\t\t{
\t\t\tload_attempted= true;
\t\t\tFileSpecifier file("gfx/pistol_scope.png");
\t\t\tif (!file.Exists() &&
\t\t\t\t!file.SetNameWithPath("Sprintathon/pistol_scope.png"))
\t\t\t{
\t\t\t\tfile= FileSpecifier(
\t\t\t\t\tget_data_path(kPathDefaultData)+
\t\t\t\t\t"/Sprintathon/pistol_scope.png");
\t\t\t}
\t\t\tif (file.Exists())
\t\t\t{
\t\t\t\tImageDescriptor image;
\t\t\t\tif (image.LoadFromFile(file, ImageLoader_Colors, 0))
\t\t\t\t\tpistol_scope.Load(image);
\t\t\t}
\t\t}
\t\tif (pistol_scope.Loaded())
\t\t{
\t\t\tconst float diameter=
\t\t\t\tstd::min<float>(pixel_width, pixel_height)*0.86f;
\t\t\tconst Image_Rect destination(
\t\t\t\t(pixel_width-diameter)*0.5f,
\t\t\t\t(pixel_height-diameter)*0.5f,
\t\t\t\tdiameter, diameter);
\t\t\tpistol_scope.tint_color_r= 1.f;
\t\t\tpistol_scope.tint_color_g= 1.f;
\t\t\tpistol_scope.tint_color_b= 1.f;
\t\t\tpistol_scope.tint_color_a= scope_amount;
\t\t\tpistol_scope.rotation= 0.f;
\t\t\tpistol_scope.Draw(destination);
\t\t}
\t}

"""

count = text.count(needle)
if count != 1:
    raise SystemExit(
        f"Expected one bullet-time overlay insertion point, found {count}; no change made."
    )

path.write_text(text.replace(needle, overlay, 1))
print("Installed the pistol scope artwork draw.")

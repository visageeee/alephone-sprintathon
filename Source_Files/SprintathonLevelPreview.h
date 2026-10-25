#ifndef SPRINTATHON_LEVEL_PREVIEW_H
#define SPRINTATHON_LEVEL_PREVIEW_H
#include "wad.h"
#include "game_errors.h"
#include <map>
#include <array>
#include <algorithm>

// Read-only preview: never installs a map or runs embedded scripts. The cache
// belongs to this picker, so changing scenarios cannot reuse stale geometry.
class w_level_preview : public widget
{
    w_levels* levels;
    const std::vector<entry_point>& entries;
    mutable short displayed = -2;
    typedef std::array<float, 4> Edge;
    mutable std::map<short, std::vector<Edge> > cache;

    static std::vector<Edge> read_geometry(short level)
    {
        std::vector<Edge> edges;
        if (level < 0) return edges;
        ScopedGameError preserve_error;
        OpenedFile file;
        if (!open_wad_file_for_reading(get_map_file(), file)) return edges;
        wad_header header;
        wad_data* wad = nullptr;
        if (read_wad_header(file, &header) && level < header.wad_count)
            wad = read_indexed_wad_from_file(file, &header, level, true);
        close_wad_file(file);
        if (!wad) return edges;
        struct WadGuard { wad_data* p; ~WadGuard() { free_wad(p); } } guard{wad};
        size_t bytes = 0;
        uint8* data = static_cast<uint8*>(extract_type_from_wad(wad, POINT_TAG, &bytes));
        std::vector<world_point2d> points;
        if (data && bytes && bytes % 4 == 0 && bytes / 4 <= 32767)
        {
            points.resize(bytes / 4);
            for (size_t i = 0; i < points.size(); ++i)
            {
                const uint8* p = data + i * 4;
                const int x = (int(p[0]) << 8) | p[1];
                const int y = (int(p[2]) << 8) | p[3];
                points[i].x = x >= 32768 ? x - 65536 : x;
                points[i].y = y >= 32768 ? y - 65536 : y;
            }
        }
        else if (!bytes)
        {
            data = static_cast<uint8*>(extract_type_from_wad(wad, ENDPOINT_DATA_TAG, &bytes));
            if (data && bytes && bytes % SIZEOF_endpoint_data == 0 &&
                bytes / SIZEOF_endpoint_data <= 32767)
            {
                std::vector<endpoint_data> endpoints(bytes / SIZEOF_endpoint_data);
                unpack_endpoint_data(data, endpoints.data(), endpoints.size());
                for (const auto& endpoint : endpoints) points.push_back(endpoint.vertex);
            }
        }
        data = static_cast<uint8*>(extract_type_from_wad(wad, LINE_TAG, &bytes));
        if (points.empty() || !data || !bytes || bytes % SIZEOF_line_data ||
            bytes / SIZEOF_line_data > 32767) return edges;
        std::vector<line_data> lines(bytes / SIZEOF_line_data);
        unpack_line_data(data, lines.data(), lines.size());
        float minx = 32768, miny = 32768, maxx = -32768, maxy = -32768;
        for (const auto& line : lines)
        {
            const int a = line.endpoint_indexes[0], b = line.endpoint_indexes[1];
            if (a < 0 || b < 0 || size_t(a) >= points.size() || size_t(b) >= points.size()) continue;
            Edge e{{float(points[a].x), float(points[a].y), float(points[b].x), float(points[b].y)}};
            edges.push_back(e);
            minx = std::min(minx, std::min(e[0], e[2]));
            miny = std::min(miny, std::min(e[1], e[3]));
            maxx = std::max(maxx, std::max(e[0], e[2]));
            maxy = std::max(maxy, std::max(e[1], e[3]));
        }
        const float span = std::max(1.f, std::max(maxx - minx, maxy - miny));
        for (auto& e : edges)
            for (int i = 0; i < 4; i += 2)
            {
                e[i] = .5f + (e[i] - (minx + maxx) * .5f) / span;
                e[i+1] = .5f + (e[i+1] - (miny + maxy) * .5f) / span;
            }
        return edges;
    }
    short selected() const
    {
        const size_t i = levels->get_selection();
        return i < entries.size() ? entries[i].level_number : NONE;
    }
public:
    w_level_preview(w_levels* list, const std::vector<entry_point>& items, int width)
        : widget(ITEM_WIDGET), levels(list), entries(items)
    {
        saved_min_width = width;
        saved_min_height = width + font->get_line_height() * 3;
        rect.w = saved_min_width; rect.h = saved_min_height;
    }
    bool is_selectable() const override { return false; }
    bool is_dirty() override { return dirty || selected() != displayed; }
    void draw(SDL_Surface* s) const override
    {
        displayed = selected();
        auto it = cache.find(displayed);
        if (it == cache.end())
        {
            // Bound browsing memory, even for enormous custom map packs.
            if (cache.size() >= 32) cache.erase(cache.begin());
            it = cache.emplace(displayed, read_geometry(displayed)).first;
        }
        const int pad = scale_dialog_value(8);
        const int top = rect.y + font->get_line_height() * 2;
        const int size = std::max(1, rect.w - pad * 2);
        const uint32 amber = SDL_MapRGB(s->format, 220, 220, 220);
        draw_text(s, "SECTOR / TOPOLOGY", rect.x + pad, rect.y + font->get_ascent(), amber, font, style);
        SDL_Rect frame = {rect.x + pad, top, size, size};
        draw_rectangle(s, &frame, SDL_MapRGB(s->format, 90, 90, 90));
        for (const auto& e : it->second)
        {
            world_point2d a, b;
            a.x = frame.x + 4 + int(e[0] * std::max(1, size - 9));
            a.y = frame.y + 4 + int(e[1] * std::max(1, size - 9));
            b.x = frame.x + 4 + int(e[2] * std::max(1, size - 9));
            b.y = frame.y + 4 + int(e[3] * std::max(1, size - 9));
            draw_line(s, &a, &b, amber, 1);
        }
        if (it->second.empty())
            draw_text(s, "NO SIGNAL", frame.x + pad, frame.y + pad + font->get_ascent(), amber, font, style);
    }
};
#endif

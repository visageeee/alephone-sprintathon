-- Sprintathon's optional full-screen HUD.

Triggers = {}

local font
local small_font
local scale = 1
local weapon_shapes = {}
local weapon_switch_visible_until = 0
local last_current_weapon = nil

local white = {0.92, 1.00, 0.92, 1.00}
local dim = {0.34, 0.58, 0.36, 1.00}
local green = {0.15, 1.00, 0.22, 1.00}
local light_green = {0.62, 1.00, 0.66, 1.00}
local dark_green = {0.005, 0.025, 0.008, 0.94}
local badge_green = {0.01, 0.075, 0.025, 0.86}
local black = {0.00, 0.00, 0.00, 0.76}
local red = {0.68, 0.18, 0.14, 1.00}
local blue = {0.18, 0.58, 0.68, 1.00}
local yellow = {0.72, 0.58, 0.16, 1.00}
local purple = {0.48, 0.25, 0.62, 1.00}
local amber = {1.00, 0.70, 0.12, 1.00}

local function clamp(value, low, high)
  return math.max(low, math.min(high, value))
end

local function configure_screen()
  scale = clamp(Screen.height / 1080, 0.70, 1.45)
  Screen.world_rect.x = 0
  Screen.world_rect.y = 0
  Screen.world_rect.width = Screen.width
  Screen.world_rect.height = Screen.height
  Screen.map_rect.x = 0
  Screen.map_rect.y = 0
  Screen.map_rect.width = Screen.width
  Screen.map_rect.height = Screen.height
  Screen.term_rect.x = 0
  Screen.term_rect.y = 0
  Screen.term_rect.width = Screen.width
  Screen.term_rect.height = Screen.height
  Screen.text_margins.left = 20 * scale
  Screen.text_margins.right = 20 * scale
  Screen.text_margins.top = 12 * scale
  Screen.text_margins.bottom = 166 * scale
end

local function panel(x, y, width, height)
  Screen.fill_rect(x, y, width, height, black)
  Screen.frame_rect(x, y, width, height, dim, math.max(1, scale))
end

local function draw_edge_panel_shape(x, y, width, height, color, rounded_right)
  local radius = height / 2
  local step = math.max(1, math.floor(2 * scale))
  for offset = 0, height, step do
    local row_x = x
    local row_width = width
    if offset < radius then
      local curve = math.sqrt(math.max(0,
        radius * radius - (radius - offset) * (radius - offset)))
      if rounded_right then
        row_width = width - radius + curve
      else
        row_x = x + radius - curve
        row_width = width - radius + curve
      end
    end
    Screen.fill_rect(row_x, y + offset, row_width,
      math.min(step + 0.5, height - offset), color)
  end
end

local function draw_edge_panel(x, y, width, height, rounded_right)
  local inset = 2.5 * scale
  draw_edge_panel_shape(x, y, width, height,
    {0.035, 0.045, 0.040, 0.56}, rounded_right)
  if rounded_right then
    draw_edge_panel_shape(x, y + inset, width - inset,
      height - inset, {0.003, 0.035, 0.012, 0.68}, true)
  else
    draw_edge_panel_shape(x + inset, y + inset, width - inset,
      height - inset, {0.003, 0.035, 0.012, 0.68}, false)
  end
end

local function draw_capsule(x, y, width, height, color)
  if height <= 0 then return end
  local radius = math.min(width / 2, height / 2)
  local middle_height = math.max(0, height - radius * 2)
  if middle_height > 0 then
    Screen.fill_rect(x, y + radius, width, middle_height, color)
  end
  local slices = math.max(2, math.ceil(radius))
  for slice = 0, slices - 1 do
    local offset = (slice + 0.5) * radius / slices
    local half_width = math.sqrt(math.max(0,
      radius * radius - (radius - offset) * (radius - offset)))
    local slice_height = radius / slices + 0.5
    Screen.fill_rect(x + radius - half_width, y + offset,
      half_width * 2, slice_height, color)
    Screen.fill_rect(x + radius - half_width, y + height - offset - slice_height,
      half_width * 2, slice_height, color)
  end
end

local function draw_horizontal_capsule(x, y, width, height, color)
  if width <= 0 then return end
  local radius = math.min(width / 2, height / 2)
  local middle_width = math.max(0, width - radius * 2)
  if middle_width > 0 then
    Screen.fill_rect(x + radius, y, middle_width, height, color)
  end
  local slices = math.max(2, math.ceil(radius))
  for slice = 0, slices - 1 do
    local offset = (slice + 0.5) * radius / slices
    local half_height = math.sqrt(math.max(0,
      radius * radius - (radius - offset) * (radius - offset)))
    local slice_width = radius / slices + 0.5
    Screen.fill_rect(x + offset, y + radius - half_height,
      slice_width, half_height * 2, color)
    Screen.fill_rect(x + width - offset - slice_width,
      y + radius - half_height, slice_width, half_height * 2, color)
  end
end

local function draw_bottom_bar(x, y, width, height, fraction,
    color, background, fill_from_right, pulse)
  fraction = clamp(fraction, 0, 1)
  local outline = 3 * scale
  draw_horizontal_capsule(x - outline, y - outline,
    width + outline * 2, height + outline * 2, black)
  draw_horizontal_capsule(x, y, width, height, background)
  local filled = width * fraction
  local fill_x = fill_from_right and (x + width - filled) or x
  local fill_color = color
  if pulse then
    local wave = (math.sin(Game.interpolated_ticks * 0.62) + 1) * 0.5
    local brightness = 0.42 + wave * 0.58
    fill_color = {
      color[1] * brightness,
      color[2] * brightness,
      color[3] * brightness,
      color[4]
    }
  end
  draw_horizontal_capsule(fill_x, y, filled, height, fill_color)

  -- Recessed black dividers give both meters the segmented Marathon look.
  local spacing = 6 * scale
  local line_width = math.max(1, 1.15 * scale)
  local marker = x + spacing
  while marker < x + width - spacing / 2 do
    Screen.fill_rect(marker, y, line_width, height, black)
    marker = marker + spacing
  end
end

local function draw_vertical_meter(x, y, width, height, fraction,
    color, background, pulse)
  fraction = clamp(fraction, 0, 1)
  local outline = 3 * scale
  draw_capsule(x - outline, y - outline,
    width + outline * 2, height + outline * 2, black)
  draw_capsule(x, y, width, height, background)
  local fill_height = height * fraction
  local fill_color = color
  if pulse then
    local wave = (math.sin(Game.interpolated_ticks * 0.62) + 1) * 0.5
    local brightness = 0.42 + wave * 0.58
    fill_color = {
      color[1] * brightness,
      color[2] * brightness,
      color[3] * brightness,
      color[4]
    }
  end
  draw_capsule(x, y + height - fill_height,
    width, fill_height, fill_color)

  local spacing = 6 * scale
  local line_height = math.max(1, 1.15 * scale)
  local marker = y + spacing
  while marker < y + height - spacing / 2 do
    Screen.fill_rect(x, marker, width, line_height, black)
    marker = marker + spacing
  end
end

local function draw_tracker(x, y, size)
  local cx = x + size / 2
  local cy = y + size / 2
  local radius = size * 0.47

  -- Build a genuinely circular translucent face instead of putting the
  -- tracker inside a square panel.
  local step = math.max(1, math.floor(2 * scale))
  for offset = -radius, radius, step do
    local half_width = math.sqrt(math.max(0,
      radius * radius - offset * offset))
    Screen.fill_rect(cx - half_width, cy + offset,
      half_width * 2, step + 0.5, dark_green)
  end

  Screen.fill_rect(cx - 0.5 * scale, cy - radius,
    scale, radius * 2, dim)
  Screen.fill_rect(cx - radius, cy - 0.5 * scale,
    radius * 2, scale, dim)
  for ring = 1, 2 do
    local rr = radius * ring / 2
    for degree = 0, 355, 5 do
      local angle = math.rad(degree)
      Screen.fill_rect(cx + math.cos(angle) * rr - 1.2 * scale,
        cy + math.sin(angle) * rr - 1.2 * scale,
        2.4 * scale, 2.4 * scale, dim)
    end
  end
  -- Dense samples make the outside circumference read as one solid ring.
  for degree = 0, 359 do
    local angle = math.rad(degree)
    Screen.fill_rect(cx + math.cos(angle) * radius - 1.6 * scale,
      cy + math.sin(angle) * radius - 1.6 * scale,
      3.2 * scale, 3.2 * scale, dim)
  end
  Screen.fill_rect(cx - 2 * scale, cy - 8 * scale,
    4 * scale, 13 * scale, green)
  Screen.fill_rect(cx - 5 * scale, cy - 7 * scale,
    10 * scale, 3 * scale, green)

  local sensor = Player.motion_sensor
  if sensor and sensor.active then
    local blips = sensor.blips
    for index = 0, #blips - 1 do
      local blip = blips[index]
      local angle = math.rad(blip.direction)
      local distance = clamp(blip.distance / 8, 0, 1) * radius
      local base = red
      if blip.type == SensorBlipTypes["friend"] then
        base = green
      elseif blip.type == SensorBlipTypes["hostile player"] then
        base = amber
      end
      local age = clamp(blip.intensity, 0, 7)
      local freshness = 1 - age / 8
      local color = {base[1], base[2], base[3],
        0.16 + freshness * 0.84}
      local dot = (2.2 + freshness * 7.8) * scale
      Screen.fill_rect(cx + math.cos(angle) * distance - dot / 2,
        cy + math.sin(angle) * distance - dot / 2,
        dot, dot, color)
    end
  else
    local offline_width = small_font:measure_text("OFFLINE")
    small_font:draw_text("OFFLINE", cx - offline_width / 2,
      cy + radius * 0.55, red)
  end
end

local function inventory_entries()
  local entries = {}
  local items = Player.items
  for index = 0, #items - 1 do
    local item = items[index]
    if item.valid and item.count > 0 then
      local name = item.count == 1 and item.singular or item.plural
      entries[#entries + 1] = {name = name, count = item.count}
    end
  end
  return entries
end

local function fit_text(text, maximum_width)
  if font:measure_text(text) <= maximum_width then return text end
  local suffix = "..."
  while #text > 1 and font:measure_text(text .. suffix) > maximum_width do
    text = string.sub(text, 1, #text - 1)
  end
  return text .. suffix
end

local function draw_inventory(x, y, width, height)
  panel(x, y, width, height)
  small_font:draw_text("INVENTORY", x + 10 * scale,
    y + 8 * scale, green)
  local entries = inventory_entries()
  local row_y = y + 31 * scale
  local row_height = 18 * scale
  local maximum_rows = math.floor((height - 38 * scale) / row_height)
  for index = 1, math.min(#entries, maximum_rows) do
    local entry = entries[index]
    local count = tostring(entry.count)
    local count_width = small_font:measure_text(count)
    local name_width = width - count_width - 30 * scale
    font:draw_text(fit_text(entry.name, name_width),
      x + 10 * scale, row_y, white)
    small_font:draw_text(count, x + width - count_width - 10 * scale,
      row_y + 2 * scale, green)
    row_y = row_y + row_height
  end
  if #entries == 0 then
    font:draw_text("Empty", x + 10 * scale, row_y, dim)
  end
end

local function trigger_text(trigger)
  if not trigger or not trigger.weapon_drawn or trigger.total_rounds <= 0 then
    return nil
  end
  return tostring(trigger.rounds) .. " / " .. tostring(trigger.total_rounds)
end

local function get_weapon_shape(texture_index)
  if weapon_shapes[texture_index] == nil then
    weapon_shapes[texture_index] = Shapes.new{
      collection = Collections["interface"],
      texture_index = texture_index,
      type = TextureTypes["interface"]
    }
  end
  return weapon_shapes[texture_index]
end

local function reset_shape_crop(shape)
  shape.crop_rect.x = 0
  shape.crop_rect.y = 0
  shape.crop_rect.width = shape.width
  shape.crop_rect.height = shape.height
end

local weapon_inventory = {
  {weapon = "fist", item = "knife"},
  {weapon = "pistol", item = "pistol", ammo = "pistol ammo"},
  {weapon = "fusion pistol", item = "fusion pistol", ammo = "fusion pistol ammo"},
  {weapon = "assault rifle", item = "assault rifle",
    ammo = "assault rifle ammo", secondary_ammo = "assault rifle grenades"},
  {weapon = "missile launcher", item = "missile launcher", ammo = "missile launcher ammo"},
  {weapon = "flamethrower", item = "flamethrower", ammo = "flamethrower ammo"},
  {weapon = "alien weapon", item = "alien weapon", ammo = "alien weapon ammo"},
  {weapon = "shotgun", item = "shotgun", ammo = "shotgun ammo"},
  {weapon = "smg", item = "smg", ammo = "smg ammo"}
}

local function owned_weapons()
  local owned = {}
  for _, definition in ipairs(weapon_inventory) do
    local item = Player.items[ItemTypes[definition.item]]
    if definition.weapon == "fist" or (item.valid and item.count > 0) then
      local weapon = Player.weapons[WeaponTypes[definition.weapon]]
      local magazines = nil
      if definition.ammo then
        magazines = weapon.primary.magazines
      end
      local secondary_magazines = nil
      if definition.secondary_ammo then
        secondary_magazines = weapon.secondary.magazines
      end
      owned[#owned + 1] = {
        weapon = weapon,
        item_count = item.count,
        magazines = magazines,
        secondary_magazines = secondary_magazines
      }
    end
  end
  table.sort(owned, function(left, right)
    return left.weapon.cycle_order < right.weapon.cycle_order
  end)
  return owned
end

local function draw_weapon_inventory(right, bottom, maximum_height, selected)
  local owned = owned_weapons()
  if #owned == 0 then return end

  local slot_height = math.min(52 * scale, maximum_height / #owned)
  local target_height = math.max(19 * scale, slot_height - 16 * scale)
  local maximum_shape_width = 0
  for _, entry in ipairs(owned) do
    local shape_info = entry.weapon.shape
    if entry.item_count > 1 and entry.weapon.multiple_shape then
      shape_info = entry.weapon.multiple_shape
    end
    entry.shape_info = shape_info
    entry.shape_width = 28 * scale
    if shape_info then
      local shape = get_weapon_shape(shape_info.texture_index)
      if shape then
        entry.shape_width = shape.unscaled_width *
          target_height / shape.unscaled_height
      end
    end
    maximum_shape_width = math.max(maximum_shape_width, entry.shape_width)
  end

  local badge_width = 48 * scale
  local badge_gap = 8 * scale
  local slot_width = maximum_shape_width + badge_width + 22 * scale
  local slot_x = right - slot_width
  local slot_y = bottom - slot_height * #owned

  for _, entry in ipairs(owned) do
    if entry.shape_info then
      local shape = get_weapon_shape(entry.shape_info.texture_index)
      if shape then
        shape:rescale(entry.shape_width, target_height)
        reset_shape_crop(shape)
        shape:draw(right - badge_width - badge_gap - entry.shape_width,
          slot_y + (slot_height - target_height) / 2)
      end
    end

    if entry.magazines ~= nil then
      local badge_x = right - badge_width
      local badge_y = slot_y + 7 * scale
      local badge_height = slot_height - 14 * scale
      Screen.fill_rect(badge_x, badge_y,
        badge_width, badge_height, badge_green)
      Screen.frame_rect(badge_x, badge_y,
        badge_width, badge_height, dim, math.max(1, scale))
      local primary_count = tostring(entry.magazines)
      local primary_width = small_font:measure_text(primary_count)
      if entry.secondary_magazines ~= nil then
        local grenade_count = "G" .. tostring(entry.secondary_magazines)
        local grenade_width = small_font:measure_text(grenade_count)
        small_font:draw_text(primary_count,
          badge_x + (badge_width - primary_width) / 2,
          slot_y + 8 * scale, light_green)
        small_font:draw_text(grenade_count,
          badge_x + (badge_width - grenade_width) / 2,
          slot_y + slot_height - 20 * scale, light_green)
      else
        small_font:draw_text(primary_count,
          badge_x + (badge_width - primary_width) / 2,
          slot_y + (slot_height - 13 * scale) / 2, light_green)
      end
    end
    if selected and selected.type == entry.weapon.type then
      Screen.fill_rect(slot_x, slot_y + 4 * scale,
        3 * scale, slot_height - 8 * scale, green)
    end
    slot_y = slot_y + slot_height
  end
end

local function draw_bullet_ammo(trigger, x, y, maximum_width, maximum_height,
    fill_width)
  if not trigger or not trigger.weapon_drawn or trigger.total_rounds <= 0 then
    return false
  end
  local display = trigger.bullet_display
  if not display or not display.texture_index then return false end
  local loaded = get_weapon_shape(display.texture_index)
  if not loaded then return false end
  local empty = nil
  if display.empty_texture_index then
    empty = get_weapon_shape(display.empty_texture_index)
  end

  local rows = math.max(1, display.down)
  local across = math.max(1, display.across)
  local row_scale = math.min(maximum_width / loaded.unscaled_width,
    maximum_height / (loaded.unscaled_height * rows))
  local row_width = loaded.unscaled_width * row_scale
  local row_height = loaded.unscaled_height * row_scale
  if fill_width then row_width = maximum_width end
  local row_x = x + (maximum_width - row_width) / 2
  loaded:rescale(row_width, row_height)
  if empty then empty:rescale(row_width, row_height) end

  local rounds = clamp(trigger.rounds, 0, rows * across)
  for row = 0, rows - 1 do
    local row_y = y + row * row_height
    if empty then
      reset_shape_crop(empty)
      empty:draw(row_x, row_y)
    end
    local in_row = clamp(rounds - row * across, 0, across)
    if in_row > 0 then
      local loaded_width = row_width * in_row / across
      loaded.crop_rect.y = 0
      loaded.crop_rect.height = row_height
      loaded.crop_rect.width = loaded_width
      if display.right_to_left then
        loaded.crop_rect.x = 0
        loaded:draw(row_x, row_y)
      else
        loaded.crop_rect.x = row_width - loaded_width
        loaded:draw(row_x + row_width - loaded_width, row_y)
      end
    end
  end
  reset_shape_crop(loaded)
  return true
end

local function draw_energy_ammo(trigger, x, y, width, height)
  if not trigger or not trigger.weapon_drawn or trigger.total_rounds <= 0 then
    return false
  end
  local display = trigger.energy_display
  if not display then return false end
  local full = {display.color.r, display.color.g,
    display.color.b, display.color.a}
  local empty = {display.empty_color.r, display.empty_color.g,
    display.empty_color.b, display.empty_color.a}
  Screen.fill_rect(x, y, width, height, full)
  Screen.fill_rect(x + scale, y + scale,
    width - 2 * scale, height - 2 * scale, empty)
  local amount = clamp(trigger.rounds / math.max(1, display.maximum), 0, 1)
  local fill_height = (height - 2 * scale) * amount
  Screen.fill_rect(x + scale, y + height - scale - fill_height,
    width - 2 * scale, fill_height, full)
  return true
end

local function reserve_magazines(trigger, paired_trigger)
  if not trigger or trigger.total_rounds <= 0 then return 0 end
  local count = trigger.magazines or 0
  if trigger.rounds > 0 then count = count - 1 end
  if paired_trigger and paired_trigger.rounds > 0 then
    count = count - 1
  end
  return math.max(0, count)
end

local function draw_weapon(x, y, width, height)
  local weapon = Player.weapons.current
  if not weapon then
    local unarmed_width = font:measure_text("Unarmed")
    font:draw_text("Unarmed", x + (width - unarmed_width) / 2,
      y + height * 0.34, dim)
    return
  end
  local weapon_name = fit_text(weapon.name, width - 30 * scale)
  local name_width = small_font:measure_text(weapon_name)
  small_font:draw_text(weapon_name,
    x + width - name_width - 9 * scale, y + 9 * scale, white)

  local second_drawn = weapon.secondary and weapon.secondary.weapon_drawn
  local owns_pair = false
  for _, definition in ipairs(weapon_inventory) do
    if weapon.type == WeaponTypes[definition.weapon] then
      local item = Player.items[ItemTypes[definition.item]]
      owns_pair = item.valid and item.count > 1
      break
    end
  end
  local akimbo = owns_pair and
    (weapon.type == WeaponTypes["pistol"] or
     weapon.type == WeaponTypes["shotgun"])
  local shape_info = weapon.shape
  if shape_info then
    local shape = get_weapon_shape(shape_info.texture_index)
    if shape then
      local maximum_width = width - 34 * scale
      local maximum_height = height * 0.39
      if akimbo then
        local gap = 4 * scale
        local shape_scale = math.min(
          ((maximum_width - gap) / 2) / shape.unscaled_width,
          maximum_height / shape.unscaled_height)
        local shape_width = shape.unscaled_width * shape_scale
        local shape_height = shape.unscaled_height * shape_scale
        local pair_width = shape_width * 2 + gap
        local pair_x = x + width - pair_width - 9 * scale
        local shape_y = y + 29 * scale +
          (maximum_height - shape_height) / 2
        shape:rescale(shape_width, shape_height)
        reset_shape_crop(shape)
        shape.flip_horizontal = false
        shape:draw(pair_x, shape_y)
        shape.flip_horizontal = true
        shape:draw(pair_x + shape_width + gap, shape_y)
        shape.flip_horizontal = false
      else
        local shape_scale = math.min(maximum_width / shape.unscaled_width,
          maximum_height / shape.unscaled_height)
        local shape_width = shape.unscaled_width * shape_scale
        local shape_height = shape.unscaled_height * shape_scale
        shape:rescale(shape_width, shape_height)
        reset_shape_crop(shape)
        shape.flip_horizontal = false
        shape:draw(x + width - shape_width - 9 * scale,
          y + 29 * scale + (maximum_height - shape_height) / 2)
      end
    end
  end

  local secondary_visible = second_drawn and weapon.secondary.total_rounds > 0
  local number_width = 38 * scale
  local number_right = x + width - 8 * scale
  local indicator_x = x + 18 * scale
  local indicator_width = width - 34 * scale - number_width
  local ammo_top = y + 108 * scale
  local assault_rifle = weapon.type == WeaponTypes["assault rifle"]

  if assault_rifle and secondary_visible then
    -- The rifle's dense bullet mask needs most of the lower-half height;
    -- grenades retain a short strip. Shared-ammo akimbo weapons divide the
    -- space evenly between their two equally important magazines.
    local primary_height = (assault_rifle and 38 or 27) * scale
    local secondary_height = (assault_rifle and 17 or 27) * scale
    if not draw_bullet_ammo(weapon.primary, indicator_x, ammo_top,
        indicator_width, primary_height) then
      draw_energy_ammo(weapon.primary,
        indicator_x + indicator_width / 2 - 5 * scale,
        ammo_top, 10 * scale, primary_height)
    end
    local secondary_y = ammo_top + primary_height + 2 * scale
    if not draw_bullet_ammo(weapon.secondary, indicator_x,
        secondary_y, indicator_width, secondary_height, assault_rifle) then
      draw_energy_ammo(weapon.secondary,
        indicator_x + indicator_width / 2 - 5 * scale,
        secondary_y, 10 * scale, secondary_height)
    end

    local primary_text = tostring(reserve_magazines(weapon.primary)) .. "x"
    local secondary_text = tostring(reserve_magazines(weapon.secondary)) .. "x"
    small_font:draw_text(primary_text,
      number_right - small_font:measure_text(primary_text),
      ammo_top + 9 * scale, light_green)
    small_font:draw_text(secondary_text,
      number_right - small_font:measure_text(secondary_text),
      secondary_y + 2 * scale, light_green)
  elseif akimbo and secondary_visible then
    local lane_height = 27 * scale
    draw_bullet_ammo(weapon.primary, indicator_x, ammo_top,
      indicator_width, lane_height)
    draw_bullet_ammo(weapon.secondary, indicator_x,
      ammo_top + lane_height + 2 * scale,
      indicator_width, lane_height)

    -- Both hands draw from the same ammunition pool, so show the total once.
    local magazine_text = tostring(
      reserve_magazines(weapon.primary, weapon.secondary)) .. "x"
    font:draw_text(magazine_text,
      number_right - font:measure_text(magazine_text),
      ammo_top + 18 * scale, light_green)
  else
    local indicator_height = 48 * scale
    if not draw_bullet_ammo(weapon.primary, indicator_x, ammo_top,
        indicator_width, indicator_height) then
      local energy_width =
        weapon.type == WeaponTypes["fusion pistol"] and 76 or 10
      draw_energy_ammo(weapon.primary,
        indicator_x + indicator_width / 2 - energy_width * scale / 2,
        ammo_top, energy_width * scale, indicator_height)
    end
    local primary_text = weapon.primary.total_rounds > 0 and
      (tostring(reserve_magazines(weapon.primary)) .. "x") or "--"
    font:draw_text(primary_text,
      number_right - font:measure_text(primary_text),
      ammo_top + 16 * scale, light_green)
  end
end

function Triggers.init()
  font = Fonts.new{file = "mono", size = 14}
  small_font = Fonts.new{file = "mono", size = 11}
  weapon_switch_visible_until = 0
  last_current_weapon = nil
  configure_screen()
end

function Triggers.resize()
  configure_screen()
end

function Triggers.draw()
  if Screen.term_active then return end
  local margin = 7 * scale
  local tracker_size = 174 * scale
  local edge_panel_width = 236 * scale
  local weapon_panel_height = 185 * scale
  local weapon_y = Screen.height - weapon_panel_height
  local bottom = Screen.height
  local weapon_x = Screen.width - edge_panel_width
  draw_edge_panel(0, weapon_y, edge_panel_width,
    weapon_panel_height, true)
  draw_edge_panel(weapon_x, weapon_y, edge_panel_width,
    weapon_panel_height, false)
  draw_tracker(56 * scale, weapon_y + 6 * scale, tracker_size)
  local current = Player.weapons.current
  local desired = Player.weapons.desired
  if current and desired and current.type ~= desired.type then
    weapon_switch_visible_until = Game.interpolated_ticks + 12
  elseif current and last_current_weapon and
      current.type ~= last_current_weapon then
    weapon_switch_visible_until = Game.interpolated_ticks + 8
  end
  if current then last_current_weapon = current.type end
  local weapon_list_visible =
    Game.interpolated_ticks < weapon_switch_visible_until
  if weapon_list_visible then
    draw_weapon_inventory(Screen.width - margin,
      weapon_y - 7 * scale, math.min(Screen.height * 0.62, 560 * scale),
      desired or current)
  end
  draw_weapon(weapon_x, weapon_y, edge_panel_width, weapon_panel_height)

  local energy = math.max(0, Player.energy)
  local layer_energy = energy % 150
  if energy > 0 and layer_energy == 0 then layer_energy = 150 end
  local health_fraction = layer_energy / 150
  local health_color = red
  local health_background = {0.05, 0.08, 0.06, 0.78}
  if energy > 300 then
    health_color = purple
    health_background = yellow
  elseif energy > 150 then
    health_color = yellow
    health_background = red
  end
  local meter_width = 13 * scale
  local meter_height = tracker_size
  local meter_y = weapon_y + 6 * scale
  draw_vertical_meter(9 * scale, meter_y, meter_width, meter_height,
    health_fraction, health_color, health_background,
    energy / 150 < 0.20)
  draw_vertical_meter(34 * scale, meter_y, meter_width, meter_height,
    Player.oxygen / 10800, blue, {0.05, 0.08, 0.06, 0.78},
    Player.oxygen / 10800 < 0.20)

  -- Below half health the HUD electronics begin to brown out. Instability
  -- ramps sharply and becomes almost continuous at ten percent health. The
  -- deterministic hashes avoid a regular pulse while keeping replays stable.
  local base_health_fraction = clamp(energy / 150, 0, 1)
  if base_health_fraction < 0.50 then
    local severity = clamp(
      (0.50 - base_health_fraction) / 0.40, 0, 1)
    local tick = math.floor(Game.interpolated_ticks)
    local noise = (tick * 37 + tick * tick * 11 + 17) % 101
    local threshold = 7 + severity * 79
    if noise < threshold then
      local variation =
        ((tick * 53 + tick * tick * 7 + 29) % 101) / 100
      local flicker_alpha =
        0.035 + severity * (0.17 + variation * 0.52)
      draw_edge_panel_shape(0, weapon_y, edge_panel_width,
        weapon_panel_height, {0, 0, 0, flicker_alpha}, true)
      draw_edge_panel_shape(weapon_x, weapon_y, edge_panel_width,
        weapon_panel_height, {0, 0, 0, flicker_alpha}, false)
    end
  end
end

-- MAME autoboot script: log Galaxian sound register writes and play automatically.
--   GLOG  : output file, lines "<time_sec> <L|S|P> <offset> <data>"
--   GBASE : sound register page, 0x6000 Galaxian map (default), 0xa000 Moon Cresta map
-- usage: mame galaxian -autoboot_script mame_log.lua -wavwrite mame.wav
--        -nothrottle -video none -sound none -seconds_to_run 150
local out = io.open(os.getenv("GLOG") or "writes.txt", "w")
local base = tonumber(os.getenv("GBASE") or "0x6000")
local prog = manager.machine.devices[":maincpu"].spaces["program"]
local function tnow() return manager.machine.time:as_double() end

-- keep the handles in globals, otherwise they are garbage collected
G_TAP = prog:install_write_tap(base, base + 0x1fff, "sndlog", function(offset, data, mask)
	local page = offset & 0xf800
	if page == base and (offset & 7) >= 4 then
		out:write(string.format("%.9f L %d %d\n", tnow(), offset & 3, data & 0xff))
	elseif page == base + 0x0800 then
		out:write(string.format("%.9f S %d %d\n", tnow(), offset & 7, data & 0xff))
	elseif page == base + 0x1800 then
		out:write(string.format("%.9f P 0 %d\n", tnow(), data & 0xff))
	end
end)

local function field(name)
	for _, port in pairs(manager.machine.ioport.ports) do
		for k, f in pairs(port.fields) do if k == name then return f end end
	end
end
local coin, start = field("Coin 1"), field("1 Player Start")
local fire, left, right = field("P1 Button 1"), field("P1 Left"), field("P1 Right")
local frame = 0
G_SUB = emu.add_machine_frame_notifier(function()
	frame = frame + 1
	local t = tnow()
	coin:set_value((t > 3.0 and t < 3.2) and 1 or 0)
	start:set_value((t > 5.0 and t < 5.2) and 1 or 0)
	if t > 7 then
		fire:set_value((frame % 16) < 3 and 1 or 0)
		local ph = math.floor(t / 1.3) % 4
		left:set_value(ph == 0 and 1 or 0)
		right:set_value(ph == 2 and 1 or 0)
	end
	out:flush()
end)

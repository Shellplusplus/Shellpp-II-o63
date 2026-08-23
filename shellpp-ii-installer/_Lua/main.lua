-- Production Shell++ II installer for Xiaomi Watch S4 41mm.
--
-- Run performs, in order:
--   supervisor LOAD -> apply restored boot intents -> INSTALL 0 -> 1 -> 2.
-- Each supervisor command is issued from a separate LuaLVGL timer callback so
-- miwear receives an event-loop turn between native registration stages.

local lvgl = require("lvgl")

local MODULE_PATH = SCRIPT_PATH .. "shellpp_ii.bin"
local MODULE_NAME = "shellpp_ii"
local MANAGER_ICON_RESOURCE = SCRIPT_PATH .. "shellpp_ii_icon.bin"
local MANAGER_ICON_PATH = "/data/shellpp-ii/shellpp_ii_icon.bin"
local DEVICE_PATH = "/dev/shellpp"
local STATUS_SIZE = 384
local EXPECTED_MAGIC = 0x53505331 -- Shell++ status/control magic
local EXPECTED_STATUS_ABI = 2
local EXPECTED_BUILD_MARKER = 0x53494937
local EXPECTED_CMD_MAGIC = 0x53505331 -- Shell++ control magic
local CMD_INSTALL = 0x53510002
local CMD_NOTIFY_LOADED = 0x53510004
-- Canopus uses command suffix 0x0A for its restore-after-boot phase. Keep
-- Shell++ on the same CPC1 command family while retaining its own magic.
local CMD_RESTORE_AFTER_BOOT = 0x5351000A
local CMD_UNINSTALL = 0x53510003
local RESULT_COMPLETED = 5
local RESULT_FAILED = 15

local function describe_error(code)
    if code == -95 then
        return "native App injection is not implemented for firmware 3.100.028"
    elseif code == -22 then
        return "invalid supervisor command"
    elseif code == -19 then
        return "App-install queue is unavailable"
    elseif code == -101 then
        return "App ID 0x00CD is occupied by another package"
    end
    return "error " .. tostring(code)
end

local status
local log_panel
local run_timer
local run_phase = 1
local run_attempted = false
local clear_armed = false
local step_waits = 0

local function shell_quote(value)
    return "'" .. tostring(value):gsub("'", "'\\''") .. "'"
end

local function run(command)
    print("[shellpp-ii-installer] exec: " .. command)
    local ok = os.execute(command)
    return ok == true or ok == 0
end

local function set_status(text, color)
    status:set { text = tostring(text), text_color = color or 0xBFD9FF }
end

local function read_all(path, mode)
    if type(io) ~= "table" or type(io.open) ~= "function" then return nil end
    local file = io.open(path, mode or "rb")
    if not file then return nil end
    local content = file:read("*a")
    file:close()
    return content
end

local function supervisor_present()
    local file = io.open(DEVICE_PATH, "rb")
    if not file then return false end
    file:close()
    return true
end

local function stage_manager_icon()
    local content = read_all(MANAGER_ICON_RESOURCE, "rb")
    if type(content) ~= "string" or #content < 13
        or content:byte(1) ~= 0x19 then
        return false, "missing or invalid shellpp_ii_icon.bin"
    end
    local width = content:byte(5) + content:byte(6) * 0x100
    local height = content:byte(7) + content:byte(8) * 0x100
    if width < 1 or height < 1 or #content ~= 12 + width * height * 4 then
        return false, "shellpp_ii_icon.bin size mismatch"
    end
    local output = io.open(MANAGER_ICON_PATH, "wb")
    if not output then
        if not run("mkdir /data/shellpp-ii") then
            return false, "cannot create /data/shellpp-ii"
        end
        output = io.open(MANAGER_ICON_PATH, "wb")
    end
    if not output then return false, "cannot stage Manager icon" end
    local write_ok, write_result = pcall(output.write, output, content)
    local close_ok, close_result = pcall(output.close, output)
    if not write_ok or write_result == nil
        or not close_ok or close_result == nil then
        return false, "Manager icon write failed"
    end
    if read_all(MANAGER_ICON_PATH, "rb") ~= content then
        return false, "Manager icon verification failed"
    end
    return true
end

local function bytes_to_words(content)
    if type(content) ~= "string" or #content ~= STATUS_SIZE then return nil end
    local words = {}
    for offset = 1, STATUS_SIZE, 4 do
        local a, b, c, d = content:byte(offset, offset + 3)
        words[#words + 1] = a + b * 0x100 + c * 0x10000 + d * 0x1000000
    end
    return words
end

local function signed32(value)
    if value >= 0x80000000 then return value - 0x100000000 end
    return value
end

local function read_status()
    local file = io.open(DEVICE_PATH, "rb")
    if not file then return nil, "cannot open /dev/shellpp" end
    local raw = file:read(STATUS_SIZE)
    file:close()
    local words = bytes_to_words(raw)
    if not words or words[1] ~= EXPECTED_MAGIC then
        return nil, "supervisor status ABI mismatch"
    end
    if words[2] ~= EXPECTED_STATUS_ABI then
        return nil, "old Supervisor is resident; reboot and Run this build"
    end
    if words[12] ~= EXPECTED_BUILD_MARKER then
        return nil, "Supervisor build marker mismatch; reboot and Run this build"
    end
    -- Canopus updates status from asynchronous native workers and therefore
    -- needs a sequence-pair consistency check. Shell++ II's control endpoint
    -- completes each command synchronously inside write(), so the returned
    -- 384-byte snapshot is already coherent.
    return {
        pending_op = words[6],
        pending_state = words[7],
        error_code = signed32(words[9]),
        driver_registered = words[13],
        app_id = words[14],
        app_registered = words[15],
        launcher_published = words[16],
        loaded_notified = words[17],
        registration_result = signed32(words[18]),
        launcher_result = signed32(words[19]),
        queue_result = signed32(words[20]),
        queue_ready = words[21],
        queue_pending = words[22],
        queue_failed = words[23],
    }
end

local function word(value)
    value = math.floor(value)
    return string.char(value % 0x100, math.floor(value / 0x100) % 0x100,
        math.floor(value / 0x10000) % 0x100,
        math.floor(value / 0x1000000) % 0x100)
end

local function write_command(command, arg0)
    local payload = word(EXPECTED_CMD_MAGIC) .. word(command)
        .. word(arg0 or 0) .. word(0)
    local file = io.open(DEVICE_PATH, "wb")
    if not file then return false, "cannot open /dev/shellpp" end
    local write_ok, write_result, write_error = pcall(file.write, file, payload)
    local close_ok, close_result, close_error = pcall(file.close, file)
    if not write_ok or write_result == nil then
        return false, tostring(write_error or write_result or "write failed")
    end
    if not close_ok or close_result == nil then
        return false, tostring(close_error or close_result or "close failed")
    end
    return true
end

local function execute_step(command, arg0, submitted)
    if not submitted then
        local ok, message = write_command(command, arg0)
        if not ok then return false, message end
        return nil, "waiting for supervisor command", true
    end
    local current, status_error = read_status()
    if not current then return false, status_error end
    if current.pending_op ~= command then
        return nil, "waiting for supervisor command"
    end
    if current.pending_state == RESULT_COMPLETED then
        return true, current
    end
    if current.pending_state == RESULT_FAILED then
        local detail = string.format("result=%d error=%d register=%d launcher=%d queue=%d",
            current.pending_state, current.error_code,
            current.registration_result or 0, current.launcher_result or 0,
            current.queue_result or 0)
        return false, detail .. ": " .. describe_error(current.error_code)
    end
    step_waits = step_waits + 1
    if step_waits >= 30 then
        return false, string.format("timeout queue_ready=%d pending=%d failed=%d result=%d",
            current.queue_ready or 0, current.queue_pending or 0,
            current.queue_failed or 0, current.queue_result or 0)
    end
    return nil, "waiting for App/UI loop"
end

local steps = {
    { command = CMD_NOTIFY_LOADED, arg0 = 0,
      progress = "Sending Shell++ loaded notification..." },
    { command = CMD_RESTORE_AFTER_BOOT, arg0 = 0,
      progress = "Loading enabled modules..." },
    { command = CMD_INSTALL, arg0 = 0,
      progress = "Registering Manager..." },
    { command = CMD_INSTALL, arg0 = 1,
      progress = "Registering module apps..." },
    { command = CMD_INSTALL, arg0 = 2,
      progress = "Publishing Launcher entries..." },
}

local function finish_run(timer, success, message)
    timer:delete()
    if run_timer == timer then run_timer = nil end
    set_status(message, success and 0x8FF0A4 or 0xFF9A9A)
end

local function run_next_step(timer)
    local step = steps[run_phase]
    if not step then
        finish_run(timer, true, "Run completed")
        return
    end
    set_status(step.progress)
    local ok, message, did_submit = execute_step(step.command, step.arg0, step.submitted)
    if did_submit then step.submitted = true end
    if ok == false then
        finish_run(timer, false, "Run failed: " .. tostring(message)
            .. "\nReboot before retrying.")
        return
    end
    if ok == nil then
        timer:ready()
        return
    end
    step.submitted = false
    step_waits = 0
    run_phase = run_phase + 1
    if run_phase > #steps then
        finish_run(timer, true, "Run completed")
    else
        timer:ready()
    end
end

local function start_run_timer()
    run_phase = 1
    local created = lvgl.Timer {
        period = 1000,
        repeat_count = -1,
        paused = true,
        cb = function(timer)
            local ok, message = pcall(run_next_step, timer)
            if not ok then
                finish_run(timer, false, "Run failed: " .. tostring(message)
                    .. "\nReboot before retrying.")
            end
        end,
    }
    if not created then return false, "cannot create LuaLVGL timer" end
    run_timer = created
    created:resume()
    created:ready()
    return true
end

local SHELLPP_PERSISTENT_PATHS = {
    "/data/shellpp-ii",
    "/data/shellpp-ii-supervisor.log",
    "/data/term_out.txt",
    "/data/h69_dbg.txt",
    "/data/h71_obj.txt",
    "/data/h72_res.txt",
    "/data/h74_read.txt",
}

local function path_exists(path)
    local file = io.open(path, "rb")
    if file then file:close(); return true end
    -- io.open() cannot reliably identify directories on every Vela build.
    local ok = os.execute("test -e " .. shell_quote(path))
    return ok == true or ok == 0
end

local function clear_shellpp_environment()
    local quoted = {}
    for index, path in ipairs(SHELLPP_PERSISTENT_PATHS) do
        quoted[index] = shell_quote(path)
    end
    if not run("rm -rf " .. table.concat(quoted, " ")) then
        return false, "remove command failed"
    end
    local remaining = {}
    for _, path in ipairs(SHELLPP_PERSISTENT_PATHS) do
        if path_exists(path) then remaining[#remaining + 1] = path end
    end
    if #remaining > 0 then
        return false, "still exists: " .. table.concat(remaining, ", ")
    end
    return true
end

local rootbase = lvgl.Object(nil, {
    w = lvgl.HOR_RES(), h = lvgl.VER_RES(), bg_color = 0x07111F,
    bg_opa = lvgl.OPA(100), border_width = 0,
})
rootbase:clear_flag(lvgl.FLAG.SCROLLABLE)
local root = lvgl.Object(rootbase, {
    w = 336, h = 480, bg_color = 0x07111F, bg_opa = lvgl.OPA(100),
    border_width = 0, pad_all = 0, align = lvgl.ALIGN.CENTER,
})
root:clear_flag(lvgl.FLAG.SCROLLABLE)

-- Presentation shell only. The controls below retain their original
-- callbacks and command sequence; this panel only makes long status text
-- readable through native LVGL scrolling.
lvgl.Label(root, {
    text = "Shell++ II", text_color = 0xFFFFFF,
    text_font = lvgl.Font("MiSans-Demibold", 32),
    width = 300, height = 48,
    align = { type = lvgl.ALIGN.CENTER, x_ofs = 0, y_ofs = -202 },
})
lvgl.Label(root, {
        text = "原生应用安装器 For 3.100.028", text_color = 0x9DB7D8,
    text_font = lvgl.Font("MiSans-Regular", 20),
    width = 300, height = 47,
    align = { type = lvgl.ALIGN.CENTER, x_ofs = 0, y_ofs = -166 },
})
log_panel = lvgl.Object(root, {
    w = 304, h = 90, bg_color = 0x0D1D31, bg_opa = lvgl.OPA(100),
    radius = 14, border_width = 1, border_color = 0x244566,
    pad_left = 14, pad_right = 14, pad_top = 10, pad_bottom = 10,
    align = { type = lvgl.ALIGN.CENTER, x_ofs = 0, y_ofs = 175 },
})
log_panel:add_flag(lvgl.FLAG.SCROLLABLE)
    log_label = lvgl.Label(log_panel, {
    text = "", text_color = 0xBFD9FF,
    text_font = lvgl.Font("MiSans-Regular", 16),
    width = 276, height = 420, align = { type = lvgl.ALIGN.TOP_LEFT, x_ofs = 0, y_ofs = 0 },
})

local function make_button(text, y, color, on_clicked)
    local button = lvgl.Object(root, {
        w = 220, h = 52, bg_color = color, bg_opa = lvgl.OPA(100),
        radius = 16,
        align = { type = lvgl.ALIGN.CENTER, x_ofs = 0, y_ofs = y },
    })
    button:clear_flag(lvgl.FLAG.SCROLLABLE)
    button:add_flag(lvgl.FLAG.CLICKABLE)
    lvgl.Label(button, {
        text = text, text_color = 0xFFFFFF, align = lvgl.ALIGN.CENTER,
        text_font = lvgl.Font("MiSans-Regular", 22),
    })
    button:onClicked(function()
        local ok, message = pcall(on_clicked)
        if not ok then set_status("Error: " .. tostring(message), 0xFF9A9A) end
    end)
    return button
end

local run_button
run_button = make_button("Run", -112, 0x14508A, function()
    clear_armed = false
    if run_attempted then
        set_status("Run can only be used once; reboot before retrying")
        return
    end
    run_attempted = true
    run_button:clear_flag(lvgl.FLAG.CLICKABLE)
    if not supervisor_present() then
        set_status("Loading supervisor...")
        local inserted = run(string.format("insmod %s %s",
            shell_quote(MODULE_PATH), MODULE_NAME))
        if not inserted or not supervisor_present() then
            set_status("LOAD failed. Ensure the firmware version matches this installer."
                .. "\nReboot before retrying.", 0xFF9A9A)
            return
        end
    end
    local supervisor_status, supervisor_error = read_status()
    if not supervisor_status or supervisor_status.driver_registered ~= 1 then
        set_status("LOAD failed: " .. tostring(supervisor_error
            or "Supervisor driver did not start")
            .. "\nReboot before retrying.", 0xFF9A9A)
        return
    end
    local icon_ok, icon_error = stage_manager_icon()
    if not icon_ok then
        set_status("Run failed: " .. tostring(icon_error), 0xFF9A9A)
        return
    end
    local started, timer_error = start_run_timer()
    if not started then
        set_status("Run failed: " .. tostring(timer_error), 0xFF9A9A)
    else
        set_status("Supervisor loaded; scheduling boot restore...")
    end
end)

make_button("Uninstall", -48, 0x8A1F14, function()
    if run_timer then
        set_status("An installer operation is in progress", 0xFFD27A)
        return
    end
    local cleared, clear_error = clear_shellpp_environment()
    if cleared then
        -- The Supervisor and Launcher callbacks reside in RAM only. After
        -- reboot, NuttX drops the module and the Launcher entry with it.
        set_status("Uninstalled; rebooting...", 0x8FF0A4)
        run("reboot")
    else
        set_status("Uninstall failed: " .. tostring(clear_error), 0xFF9A9A)
    end
end)

make_button("Clear Env", 16, 0x65451A, function()
    if run_timer then
        clear_armed = false
        set_status("Run is in progress; reboot before clearing")
        return
    end
    if not clear_armed then
        clear_armed = true
        set_status("Click again to clear", 0xFFD27A)
        return
    end
    clear_armed = false
    local cleared, clear_error = clear_shellpp_environment()
    if cleared then
        set_status("Environment cleared; rebooting...", 0x8FF0A4)
        run("reboot")
    else
        set_status("Clear Env failed: " .. tostring(clear_error), 0xFF9A9A)
    end
end)

make_button("Reboot", 80, 0x3C526B, function()
    set_status("Rebooting device...", 0xBFD9FF)
    run("reboot")
end)

status = lvgl.Label(log_panel, {
    text = "Ready",
    text_color = 0xBFD9FF, width = 276, height = 70,
    text_font = lvgl.Font("MiSans-Regular", 18),
    align = { type = lvgl.ALIGN.TOP_LEFT, x_ofs = 0, y_ofs = 0 },
})

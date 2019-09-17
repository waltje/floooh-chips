#pragma once
/*#
    # ui_lc80.h

    UI for lc80.h.

    Do this:
    ~~~C
    #define CHIPS_IMPL
    ~~~
    before you include this file in *one* C++ file to create the 
    implementation.

    Optionally provide the following macros with your own implementation
    
    ~~~C
    CHIPS_ASSERT(c)
    ~~~
        your own assert macro (default: assert(c))

    Include the following headers (and their depenencies) before including
    ui_lc80.h both for the declaration and implementation.

    - lc80.h
    - mem.h
    - ui_chip.h
    - ui_util.h
    - ui_z80.h
    - ui_z80pio.h
    - ui_z80ctc.h
    - ui_audio.h
    - ui_kbd.h
    - ui_dbg.h
    - ui_dasm.h
    - ui_memedit.h

    ## zlib/libpng license

    Copyright (c) 2019 Andre Weissflog
    This software is provided 'as-is', without any express or implied warranty.
    In no event will the authors be held liable for any damages arising from the
    use of this software.
    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:
        1. The origin of this software must not be misrepresented; you must not
        claim that you wrote the original software. If you use this software in a
        product, an acknowledgment in the product documentation would be
        appreciated but is not required.
        2. Altered source versions must be plainly marked as such, and must not
        be misrepresented as being the original software.
        3. This notice may not be removed or altered from any source
        distribution. 
#*/
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* general callback type for rebooting to different configs */
typedef void (*ui_lc80_boot_t)(lc80_t* sys);

typedef struct {
    lc80_t* sys;
    ui_lc80_boot_t boot_cb; /* user-provided callback to reboot to different config */
    ui_dbg_create_texture_t create_texture_cb;      /* texture creation callback for ui_dbg_t */
    ui_dbg_update_texture_t update_texture_cb;      /* texture update callback for ui_dbg_t */
    ui_dbg_destroy_texture_t destroy_texture_cb;    /* texture destruction callback for ui_dbg_t */
    ui_dbg_keydesc_t dbg_keys;          /* user-defined hotkeys for ui_dbg_t */
} ui_lc80_desc_t;

typedef struct {
    void* imgui_font;
    lc80_t* sys;
    ui_lc80_boot_t boot_cb;
    ui_z80_t cpu;
    ui_z80pio_t pio_sys;
    ui_z80pio_t pio_usr;
    ui_z80ctc_t ctc;
    ui_audio_t audio;
    ui_kbd_t kbd;
    ui_memedit_t memedit[4];
    ui_dasm_t dasm[4];
    ui_dbg_t dbg;
} ui_lc80_t;

void ui_lc80_init(ui_lc80_t* ui, const ui_lc80_desc_t* desc);
void ui_lc80_discard(ui_lc80_t* ui);
void ui_lc80_draw(ui_lc80_t* ui, double time_ms);
bool ui_lc80_before_exec(ui_lc80_t* ui);
void ui_lc80_after_exec(ui_lc80_t* ui);

#ifdef __cplusplus
} /* extern "C" */
#endif

/*-- IMPLEMENTATION (include in C++ source) ----------------------------------*/
#ifdef CHIPS_IMPL
#ifndef __cplusplus
#error "implementation must be compiled as C++"
#endif
#include <string.h> /* memset */
#ifndef CHIPS_ASSERT
    #include <assert.h>
    #define CHIPS_ASSERT(c) assert(c)
#endif
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#endif

/*
    Draw a 7+1 segment digit

     ---
    |   |
     ---
    |   |
     --- x
*/
struct _ui_lc80_display_config {
    float digit_padding = 14.0f;
    float segment_length = 24.0f;
    float segment_thickness = 8.0f;
    float dot_offset_x = 22.0f;
    float dot_offset_y = 24.0f;
    float dot_radius = 5.0f;
    float led_radius = 10.0f;
    ImU32 color_on = 0xFF00FF00;
    ImU32 color_off = 0xFF004400;
    ImU32 color_outline = 0xFF002200;
    ImU32 color_background = 0xFF002200;
};

static void _ui_lc80_draw_seg_hori(ImDrawList* l, const ImVec2& p, bool on, const _ui_lc80_display_config& conf) {
    const float dx = conf.segment_length * 0.5f;
    const float dy = conf.segment_thickness * 0.5f;
    ImVec2 points[6] = {
        ImVec2((p.x - dx),      p.y),
        ImVec2((p.x - dx) + dy, p.y - dy),
        ImVec2((p.x + dx) - dy, p.y - dy),
        ImVec2((p.x + dx),      p.y),
        ImVec2((p.x + dx) - dy, p.y + dy),
        ImVec2((p.x - dx) + dy, p.y + dy)
    };
    l->AddConvexPolyFilled(points, 6, on ? conf.color_on : conf.color_off);
    l->AddPolyline(points, 6, conf.color_outline, true, 1.0f);
}

static void _ui_lc80_draw_seg_vert(ImDrawList* l, const ImVec2& p, bool on, const _ui_lc80_display_config& conf) {
    const float dx = conf.segment_thickness * 0.5f;
    const float dy = conf.segment_length * 0.5f;
    ImVec2 points[6] = {
        ImVec2(p.x,      (p.y - dy)),
        ImVec2(p.x + dx, (p.y - dy) + dx),
        ImVec2(p.x + dx, (p.y + dy) - dx),
        ImVec2(p.x,      (p.y + dy)),
        ImVec2(p.x - dx, (p.y + dy) - dx),
        ImVec2(p.x - dx, (p.y - dy) + dx)
    };
    l->AddConvexPolyFilled(points, 6, on ? conf.color_on : conf.color_off);
    l->AddPolyline(points, 6, conf.color_outline, true, 1.0f);
}

static void _ui_lc80_draw_vqe23_segments(ImDrawList* l, const ImVec2& p, uint8_t segs, const _ui_lc80_display_config& conf) {
    const float seg_l = conf.segment_length;
    const float seg_lh = seg_l * 0.5f;
    const float dot_x = conf.dot_offset_x;
    const float dot_y = conf.dot_offset_y;
    const float dot_r = conf.dot_radius;
    const ImU32 dot_color = (0 == (segs & 0x80)) ? conf.color_on : conf.color_off;
    _ui_lc80_draw_seg_hori(l, ImVec2(p.x, p.y-seg_l), 0==(segs&0x01), conf);
    _ui_lc80_draw_seg_hori(l, p, 0==(segs&0x40), conf);
    _ui_lc80_draw_seg_hori(l, ImVec2(p.x, p.y+seg_l), 0==(segs&0x08), conf);
    _ui_lc80_draw_seg_vert(l, ImVec2(p.x - seg_lh, p.y - seg_lh), 0==(segs&0x20), conf);
    _ui_lc80_draw_seg_vert(l, ImVec2(p.x + seg_lh, p.y - seg_lh), 0==(segs&0x02), conf);
    _ui_lc80_draw_seg_vert(l, ImVec2(p.x - seg_lh, p.y + seg_lh), 0==(segs&0x10), conf);
    _ui_lc80_draw_seg_vert(l, ImVec2(p.x + seg_lh, p.y + seg_lh), 0==(segs&0x04), conf);
    l->AddCircleFilled(ImVec2(p.x + dot_x, p.y + dot_y), dot_r, dot_color);
    l->AddCircle(ImVec2(p.x + dot_x, p.y + dot_y), dot_r, conf.color_outline);
    l->AddCircleFilled(ImVec2(p.x - dot_x, p.y - dot_y), dot_r, conf.color_off);
    l->AddCircle(ImVec2(p.x - dot_x, p.y - dot_y), dot_r, conf.color_outline);
}

static ImVec2 _ui_lc80_draw_vqe23(ImDrawList* l, ImVec2 pos, uint16_t segs, const _ui_lc80_display_config& conf) {
    const float dx = conf.segment_length * 0.5 + conf.digit_padding;
    const float w = 2 * conf.segment_length + 4 * conf.digit_padding;
    const float h = 2 * conf.segment_length + 2 * conf.digit_padding;
    ImVec2 p0(pos.x - w * 0.5f, pos.y - h * 0.5f);
    ImVec2 p1(pos.x + w * 0.5f, pos.y + h * 0.5f);
    l->AddRectFilled(p0, p1, conf.color_background);
    _ui_lc80_draw_vqe23_segments(l, ImVec2(pos.x - dx, pos.y), segs>>8, conf);
    _ui_lc80_draw_vqe23_segments(l, ImVec2(pos.x + dx, pos.y), segs&0xFF, conf);
    return ImVec2(pos.x + w, pos.y);
}

static ImVec2 _ui_lc80_draw_tape_led(ImDrawList* l, ImVec2 pos, bool on, const _ui_lc80_display_config& conf) {
    l->AddCircleFilled(pos, conf.led_radius, on ? conf.color_on : conf.color_off);
    l->AddCircle(pos, conf.led_radius, conf.color_outline);
    return ImVec2(pos.x + 2 * conf.led_radius, pos.y);
}

static void _ui_lc80_draw_display(ui_lc80_t* ui) {
    CHIPS_ASSERT(ui);
    const _ui_lc80_display_config conf;
    ImGui::SetNextWindowPos(ImVec2(600, 172), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(380, 128), ImGuiCond_Once);
    if (ImGui::Begin("LC-80 Display Test")) {
        ImDrawList* l = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 led_pos(p.x + 10.0f, p.y + 24.0f);
        _ui_lc80_draw_tape_led(l, ImVec2(p.x+16.0f, p.y+16.0f), 0==(ui->sys->pio_sys.port[Z80PIO_PORT_B].output&2), conf);
        ImVec2 disp_pos(p.x + 84.0f, p.y + 48.0f);
        disp_pos = _ui_lc80_draw_vqe23(l, disp_pos, ui->sys->led[2], conf);
        disp_pos = _ui_lc80_draw_vqe23(l, disp_pos, ui->sys->led[1], conf);
        disp_pos = _ui_lc80_draw_vqe23(l, disp_pos, ui->sys->led[0], conf);
        ImGui::SetCursorScreenPos(p);
    }
    ImGui::End();
}

static bool _ui_lc80_btn(const char* text, ImU32 fg_color, ImU32 bg_color) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 10.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, bg_color);
    ImGui::PushStyleColor(ImGuiCol_Text, fg_color);
    bool res = ImGui::Button(text, ImVec2(48.0f, 32.0f));
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    return res;
}

static void _ui_lc80_draw_keyboard(ui_lc80_t* ui) {
    CHIPS_ASSERT(ui);
    ImGui::SetNextWindowPos(ImVec2(600, 300), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(380, 400), ImGuiCond_Once);
    if (ImGui::Begin("LC-80 Keyboard Test")) {
        const ImU32 bg_red = 0xFF0000AA;
        const ImU32 bg_black = 0xFF444444;
        const ImU32 bg_white = 0xFFBBBBBB;
        const ImU32 black = 0xFF000000;
        const ImU32 white = 0xFFFFFFFF;
        /* Row 1 */
        if (_ui_lc80_btn("RES", white, bg_red)) {
            lc80_key(ui->sys, 0x01);    /* BACKSPACE */
        }
        ImGui::SameLine();
        if (_ui_lc80_btn("ADR", white, bg_black)) {
            lc80_key(ui->sys, 0x0B);    /* UP */
        }
        ImGui::SameLine();
        if (_ui_lc80_btn("DAT", white, bg_black)) {
            lc80_key(ui->sys, 0x0A);    /* DOWN */
        }
        ImGui::SameLine();
        if (_ui_lc80_btn("+", white, bg_black)) {
            lc80_key(ui->sys, 0x09);    /* RIGHT */
        }
        ImGui::SameLine();
        if (_ui_lc80_btn("-", white, bg_black)) {
            lc80_key(ui->sys, 0x08);    /* LEFT */
        }
        /* Row 2 */
        if (_ui_lc80_btn("NMI", white, bg_red)) {
            lc80_key(ui->sys, 0x0D);    /* ENTER */
        }
        ImGui::SameLine();
        if (_ui_lc80_btn("C", black, bg_white)) {
            lc80_key(ui->sys, 'c');
        }
        ImGui::SameLine();
        if (_ui_lc80_btn("D", black, bg_white)) {
            lc80_key(ui->sys, 'd');
        }
        ImGui::SameLine();
        if (_ui_lc80_btn("E", black, bg_white)) {
            lc80_key(ui->sys, 'e');
        }
        ImGui::SameLine();
        if (_ui_lc80_btn("F", black, bg_white)) {
            lc80_key(ui->sys, 'f');
        }
        /* Row 3 */
        if (_ui_lc80_btn("ST", white, bg_black)) {
            /* FIXME */
        }
        ImGui::SameLine();
        if (_ui_lc80_btn("8", black, bg_white)) {
            lc80_key(ui->sys, '8');
        }
        ImGui::SameLine();
        if (_ui_lc80_btn("9", black, bg_white)) {
            lc80_key(ui->sys, '9');
        }
        ImGui::SameLine();
        if (_ui_lc80_btn("A", black, bg_white)) {
            lc80_key(ui->sys, 'a');
        }
        ImGui::SameLine();
        if (_ui_lc80_btn("B", black, bg_white)) {
            lc80_key(ui->sys, 'b');
        }
        /* Row 4 */
        if (_ui_lc80_btn("LD", white, bg_black)) {
            /* FIXME */
        }
        ImGui::SameLine();
        if (_ui_lc80_btn("4", black, bg_white)) {
            lc80_key(ui->sys, '4');
        }
        ImGui::SameLine();
        if (_ui_lc80_btn("5", black, bg_white)) {
            lc80_key(ui->sys, '5');
        }
        ImGui::SameLine();
        if (_ui_lc80_btn("6", black, bg_white)) {
            lc80_key(ui->sys, '6');
        }
        ImGui::SameLine();
        if (_ui_lc80_btn("7", black, bg_white)) {
            lc80_key(ui->sys, '7');
        }
        /* Row 5 */
        if (_ui_lc80_btn("EX", white, bg_black)) {
            lc80_key(ui->sys, 0x20);    /* SPACE */
        }
        ImGui::SameLine();
        if (_ui_lc80_btn("0", black, bg_white)) {
            lc80_key(ui->sys, '0');
        }
        ImGui::SameLine();
        if (_ui_lc80_btn("1", black, bg_white)) {
            lc80_key(ui->sys, '1');
        }
        ImGui::SameLine();
        if (_ui_lc80_btn("2", black, bg_white)) {
            lc80_key(ui->sys, '2');
        }
        ImGui::SameLine();
        if (_ui_lc80_btn("3", black, bg_white)) {
            lc80_key(ui->sys, '3');
        }
    }
    ImGui::End();
}

static void _ui_lc80_draw_menu(ui_lc80_t* ui, double time_ms) {
    CHIPS_ASSERT(ui && ui->sys && ui->boot_cb);
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("System")) {
            if (ImGui::MenuItem("Reset")) {
                lc80_reset(ui->sys);
                ui_dbg_reset(&ui->dbg);
            }
            if (ImGui::MenuItem("Cold Boot")) {
                ui->boot_cb(ui->sys);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Hardware")) {
            ImGui::MenuItem("Keyboard Matrix", 0, &ui->kbd.open);
            ImGui::MenuItem("Audio Output", 0, &ui->audio.open);
            ImGui::MenuItem("Z80 CPU", 0, &ui->cpu.open);
            ImGui::MenuItem("Z80 PIO (SYS)", 0, &ui->pio_sys.open);
            ImGui::MenuItem("Z80 PIO (USR)", 0, &ui->pio_usr.open);
            ImGui::MenuItem("Z80 CTC", 0, &ui->ctc.open);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Debug")) {
            ImGui::MenuItem("CPU Debugger", 0, &ui->dbg.ui.open);
            ImGui::MenuItem("Breakpoints", 0, &ui->dbg.ui.show_breakpoints);
            ImGui::MenuItem("Memory Heatmap", 0, &ui->dbg.ui.show_heatmap);
            if (ImGui::BeginMenu("Memory Editor")) {
                ImGui::MenuItem("Window #1", 0, &ui->memedit[0].open);
                ImGui::MenuItem("Window #2", 0, &ui->memedit[1].open);
                ImGui::MenuItem("Window #3", 0, &ui->memedit[2].open);
                ImGui::MenuItem("Window #4", 0, &ui->memedit[3].open);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Disassembler")) {
                ImGui::MenuItem("Window #1", 0, &ui->dasm[0].open);
                ImGui::MenuItem("Window #2", 0, &ui->dasm[1].open);
                ImGui::MenuItem("Window #3", 0, &ui->dasm[2].open);
                ImGui::MenuItem("Window #4", 0, &ui->dasm[3].open);
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        ui_util_options_menu(time_ms, ui->dbg.dbg.stopped);
        ImGui::EndMainMenuBar();
    }
}

static const ui_chip_pin_t _ui_lc80_cpu_pins[] = {
    { "D0",     0,      Z80_D0 },
    { "D1",     1,      Z80_D1 },
    { "D2",     2,      Z80_D2 },
    { "D3",     3,      Z80_D3 },
    { "D4",     4,      Z80_D4 },
    { "D5",     5,      Z80_D5 },
    { "D6",     6,      Z80_D6 },
    { "D7",     7,      Z80_D7 },
    { "M1",     9,      Z80_M1 },
    { "MREQ",   10,     Z80_MREQ },
    { "IORQ",   11,     Z80_IORQ },
    { "RD",     12,     Z80_RD },
    { "WR",     13,     Z80_WR },
    { "HALT",   14,     Z80_HALT },
    { "INT",    15,     Z80_INT },
    { "NMI",    16,     Z80_NMI },
    { "WAIT",   17,     Z80_WAIT_MASK },
    { "A0",     18,     Z80_A0 },
    { "A1",     19,     Z80_A1 },
    { "A2",     20,     Z80_A2 },
    { "A3",     21,     Z80_A3 },
    { "A4",     22,     Z80_A4 },
    { "A5",     23,     Z80_A5 },
    { "A6",     24,     Z80_A6 },
    { "A7",     25,     Z80_A7 },
    { "A8",     26,     Z80_A8 },
    { "A9",     27,     Z80_A9 },
    { "A10",    28,     Z80_A10 },
    { "A11",    29,     Z80_A11 },
    { "A12",    30,     Z80_A12 },
    { "A13",    31,     Z80_A13 },
    { "A14",    32,     Z80_A14 },
    { "A15",    33,     Z80_A15 },
};

static const ui_chip_pin_t _ui_lc80_pio_pins[] = {
    { "D0",     0,      Z80_D0 },
    { "D1",     1,      Z80_D1 },
    { "D2",     2,      Z80_D2 },
    { "D3",     3,      Z80_D3 },
    { "D4",     4,      Z80_D4 },
    { "D5",     5,      Z80_D5 },
    { "D6",     6,      Z80_D6 },
    { "D7",     7,      Z80_D7 },
    { "CE",     9,      Z80PIO_CE },
    { "BASEL",  10,     Z80PIO_BASEL },
    { "CDSEL",  11,     Z80PIO_CDSEL },
    { "M1",     12,     Z80PIO_M1 },
    { "IORQ",   13,     Z80PIO_IORQ },
    { "RD",     14,     Z80PIO_RD },
    { "INT",    15,     Z80PIO_INT },
    { "ARDY",   20,     Z80PIO_ARDY },
    { "ASTB",   21,     Z80PIO_ASTB },
    { "PA0",    22,     Z80PIO_PA0 },
    { "PA1",    23,     Z80PIO_PA1 },
    { "PA2",    24,     Z80PIO_PA2 },
    { "PA3",    25,     Z80PIO_PA3 },
    { "PA4",    26,     Z80PIO_PA4 },
    { "PA5",    27,     Z80PIO_PA5 },
    { "PA6",    28,     Z80PIO_PA6 },
    { "PA7",    29,     Z80PIO_PA7 },
    { "BRDY",   30,     Z80PIO_ARDY },
    { "BSTB",   31,     Z80PIO_ASTB },
    { "PB0",    32,     Z80PIO_PB0 },
    { "PB1",    33,     Z80PIO_PB1 },
    { "PB2",    34,     Z80PIO_PB2 },
    { "PB3",    35,     Z80PIO_PB3 },
    { "PB4",    36,     Z80PIO_PB4 },
    { "PB5",    37,     Z80PIO_PB5 },
    { "PB6",    38,     Z80PIO_PB6 },
    { "PB7",    39,     Z80PIO_PB7 },
};

static const ui_chip_pin_t _ui_lc80_ctc_pins[] = {
    { "D0",     0,      Z80_D0 },
    { "D1",     1,      Z80_D1 },
    { "D2",     2,      Z80_D2 },
    { "D3",     3,      Z80_D3 },
    { "D4",     4,      Z80_D4 },
    { "D5",     5,      Z80_D5 },
    { "D6",     6,      Z80_D6 },
    { "D7",     7,      Z80_D7 },
    { "CE",     9,      Z80CTC_CE },
    { "CS0",    10,     Z80CTC_CS0 },
    { "CS1",    11,     Z80CTC_CS1 },
    { "M1",     12,     Z80CTC_M1 },
    { "IORQ",   13,     Z80CTC_IORQ },
    { "RD",     14,     Z80CTC_RD },
    { "INT",    15,     Z80CTC_INT },
    { "CT0",    16,     Z80CTC_CLKTRG0 },
    { "ZT0",    17,     Z80CTC_ZCTO0 },
    { "CT1",    19,     Z80CTC_CLKTRG1 },
    { "ZT1",    20,     Z80CTC_ZCTO1 },
    { "CT2",    22,     Z80CTC_CLKTRG2 },
    { "ZT2",    23,     Z80CTC_ZCTO2 },
    { "CT3",    25,     Z80CTC_CLKTRG3 },
};

static uint8_t _ui_lc80_mem_read(int layer, uint16_t addr, void* user_data) {
    CHIPS_ASSERT(user_data);
    lc80_t* sys = (lc80_t*) user_data;
    if (addr < 0x0800) {
        return sys->rom[addr & 0x07FF];
    }
    else if ((addr >= 0x2000) && (addr < 0x2400)) {
        return sys->ram[addr & 0x3FF];
    }
    else {
        return 0xFF;
    }
}

void _ui_lc80_mem_write(int layer, uint16_t addr, uint8_t data, void* user_data) {
    CHIPS_ASSERT(user_data);
    lc80_t* sys = (lc80_t*) user_data;
    if (addr < 0x0800) {
        sys->rom[addr & 0x07FF] = data;
    }
    else if ((addr >= 0x2000) && (addr < 0x2400)) {
        sys->ram[addr & 0x3FF] = data;
    }
}

void ui_lc80_init(ui_lc80_t* ui, const ui_lc80_desc_t* ui_desc) {
    CHIPS_ASSERT(ui && ui_desc);
    CHIPS_ASSERT(ui_desc->sys);
    CHIPS_ASSERT(ui_desc->boot_cb);

    ui->sys = ui_desc->sys;
    ui->boot_cb = ui_desc->boot_cb;
    int x = 20, y = 20, dx = 10, dy = 10;
    {
        ui_dbg_desc_t desc = {0};
        desc.title = "CPU Debugger";
        desc.x = x;
        desc.y = y;
        desc.z80 = &ui->sys->cpu;
        desc.read_cb = _ui_lc80_mem_read;
        desc.create_texture_cb = ui_desc->create_texture_cb;
        desc.update_texture_cb = ui_desc->update_texture_cb;
        desc.destroy_texture_cb = ui_desc->destroy_texture_cb;
        desc.keys = ui_desc->dbg_keys;
        desc.user_data = ui->sys;
        ui_dbg_init(&ui->dbg, &desc);
    }
    x += dx; y += dy;
    {
        ui_z80_desc_t desc = {0};
        desc.title = "Z80 CPU";
        desc.cpu = &ui->sys->cpu;
        desc.x = x;
        desc.y = y;
        UI_CHIP_INIT_DESC(&desc.chip_desc, "Z80\nCPU", 36, _ui_lc80_cpu_pins);
        ui_z80_init(&ui->cpu, &desc);
    }
    x += dx; y += dy;
    {
        ui_z80pio_desc_t desc = {0};
        desc.title = "Z80 PIO (SYS)";
        desc.pio = &ui->sys->pio_sys;
        desc.x = x;
        desc.y = y;
        UI_CHIP_INIT_DESC(&desc.chip_desc, "Z80\nPIO", 40, _ui_lc80_pio_pins);
        ui_z80pio_init(&ui->pio_sys, &desc);
        x += dx; y += dy;
        desc.title = "Z80 PIO (USR)";
        desc.pio = &ui->sys->pio_usr;
        ui_z80pio_init(&ui->pio_usr, &desc);
    }
    x += dx; y += dy;
    {
        ui_z80ctc_desc_t desc = {0};
        desc.title = "Z80 CTC";
        desc.ctc = &ui->sys->ctc;
        desc.x = x;
        desc.y = y;
        UI_CHIP_INIT_DESC(&desc.chip_desc, "Z80\nCTC", 32, _ui_lc80_ctc_pins);
        ui_z80ctc_init(&ui->ctc, &desc);
    }
    x += dx; y += dy;
    {
        ui_audio_desc_t desc = {0};
        desc.title = "Audio Output";
        desc.sample_buffer = ui->sys->sample_buffer;
        desc.num_samples = ui->sys->num_samples;
        desc.x = x;
        desc.y = y;
        ui_audio_init(&ui->audio, &desc);
    }
    x += dx; y += dy;
    {
        ui_kbd_desc_t desc = {0};
        desc.title = "Keyboard Matrix";
        desc.kbd = &ui->sys->kbd;
        desc.layers[0] = "None";
        desc.x = x;
        desc.y = y;
        ui_kbd_init(&ui->kbd, &desc);
    }
    x += dx; y += dy;
    {
        ui_memedit_desc_t desc = {0};
        desc.layers[0] = "System";
        desc.read_cb = _ui_lc80_mem_read;
        desc.write_cb = _ui_lc80_mem_write;
        desc.user_data = ui->sys;
        static const char* titles[] = { "Memory Editor #1", "Memory Editor #2", "Memory Editor #3", "Memory Editor #4" };
        for (int i = 0; i < 4; i++) {
            desc.title = titles[i]; desc.x = x; desc.y = y;
            ui_memedit_init(&ui->memedit[i], &desc);
            x += dx; y += dy;
        }
    }
    x += dx; y += dy;
    {
        ui_dasm_desc_t desc = {0};
        desc.layers[0] = "System";
        desc.cpu_type = UI_DASM_CPUTYPE_Z80;
        desc.read_cb = _ui_lc80_mem_read;
        desc.user_data = ui->sys;
        static const char* titles[4] = { "Disassembler #1", "Disassembler #2", "Disassembler #2", "Dissassembler #3" };
        for (int i = 0; i < 4; i++) {
            desc.title = titles[i]; desc.x = x; desc.y = y;
            ui_dasm_init(&ui->dasm[i], &desc);
            x += dx; y += dy;
        }
    }
}

void ui_lc80_discard(ui_lc80_t* ui) {
    CHIPS_ASSERT(ui && ui->sys);
    ui->sys = 0;
    ui_z80_discard(&ui->cpu);
    ui_z80pio_discard(&ui->pio_sys);
    ui_z80pio_discard(&ui->pio_usr);
    ui_z80ctc_discard(&ui->ctc);
    ui_audio_discard(&ui->audio);
    ui_kbd_discard(&ui->kbd);
    for (int i = 0; i < 4; i++) {
        ui_memedit_discard(&ui->memedit[i]);
        ui_dasm_discard(&ui->dasm[i]);
    }
    ui_dbg_discard(&ui->dbg);
}

void ui_lc80_draw(ui_lc80_t* ui, double time_ms) {
    CHIPS_ASSERT(ui && ui->sys);
    _ui_lc80_draw_menu(ui, time_ms);
    _ui_lc80_draw_display(ui);
    _ui_lc80_draw_keyboard(ui);
    ui_audio_draw(&ui->audio, ui->sys->sample_pos);
    ui_kbd_draw(&ui->kbd);
    ui_z80_draw(&ui->cpu);
    ui_z80pio_draw(&ui->pio_sys);
    ui_z80pio_draw(&ui->pio_usr);
    ui_z80ctc_draw(&ui->ctc);
    for (int i = 0; i < 4; i++) {
        ui_memedit_draw(&ui->memedit[i]);
        ui_dasm_draw(&ui->dasm[i]);
    }
    ui_dbg_draw(&ui->dbg);
}

bool ui_lc80_before_exec(ui_lc80_t* ui) {
    CHIPS_ASSERT(ui && ui->sys);
    return ui_dbg_before_exec(&ui->dbg);
}

void ui_lc80_after_exec(ui_lc80_t* ui) {
    CHIPS_ASSERT(ui && ui->sys);
    ui_dbg_after_exec(&ui->dbg);
}

#ifdef __clang__
#pragma clang diagnostic pop
#endif
#endif /* CHIPS_IMPL */
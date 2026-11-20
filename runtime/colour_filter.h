// The colour filter ([Colour], the menu's Graphics & Audio > Colour) and the HDR output's numbers: our own screen
// adjustment, a few ALU operations folded into the pass that already draws the game's picture into the window image
// (nvn/vulkan/exec.c vk_present_draw), never a pass of its own. The MH4U port has the same file (app_native/
// colour_filter.h): change both.
//
// Every control is a whole number whose 0 is neutral, so a file without the [Colour] keys, and Reset colour, give the
// picture as before: with all of them at 0 (and the HDR output off) the window image is drawn by the same pipeline as
// before, byte for byte, and nothing here runs.
//
// What it changes: the game's whole picture as it reaches the window (the 3D, the game's HUD and its menus), not our
// own overlay (the touch controls, this menu, the notices), which is drawn over it afterwards: the menu used to adjust
// the colour stays readable and true whatever the sliders say, so Reset colour is always within reach.
//
// The picture is the game's own sRGB-encoded one; each step works on those values, as a television's picture menu does:
//   gamma        c^(2^(-gamma/100))         +: the mid-tones brighter, -: darker; black and white stay where they are
//   temperature  per-channel gains <= 1     +: warmer (blue and a little green down), -: cooler (red and a little green down)
//   tint         per-channel gains <= 1     +: magenta (green down), -: green (red and blue down)
//   hue          a turn round the grey axis (degrees)
//   saturation   luma + s (c - luma), s = 1 + saturation/100 (Rec. 709 luma)
//   contrast     k (c - 1/2) + 1/2, k = 1 + contrast/100
//   brightness   + brightness/400
// then clamped to 0..1. Gains never above 1: nothing white clips. All but the gamma is one affine map: three rows of four.
#pragma once
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ColourAdjust {
    int hue;          // [Colour] Hue: degrees, -180..180
    int temperature;  // [Colour] Temperature: -100..100
    int tint;         // [Colour] Tint: -100..100
    int brightness;   // [Colour] Brightness: -100..100
    int contrast;     // [Colour] Contrast: -100..100
    int saturation;   // [Colour] Saturation: -100..100
    int gamma;        // [Colour] Gamma: -100..100
} ColourAdjust;

#define COLOUR_HUE_MAX 180
#define COLOUR_MAX 100

static inline bool colour_neutral(const ColourAdjust *a) {
    return !a->hue && !a->temperature && !a->tint && !a->brightness && !a->contrast && !a->saturation && !a->gamma;
}

// [Colour] Preset1..3 (1.0.2, 4U#68: the menu's Colour page keeps three, Use / Save the current colour here / Clear): the
// seven controls as "hue,temperature,tint,brightness,contrast,saturation,gamma", whole numbers with their sign, spaces
// allowed. False for text that isn't seven of them; one out of its range is brought into it. The MH4U port's the same.
static inline bool colour_preset_parse(const char *text, ColourAdjust *out) {
    int v[7];
    const char *p = text;
    for (int i = 0; i < 7; i++) {
        char *end;
        while (*p == ' ' || *p == '\t') p++;
        long x = strtol(p, &end, 10);
        if (end == p) return false;
        p = end;
        while (*p == ' ' || *p == '\t') p++;
        if (i < 6 ? *p != ',' : *p != 0) return false;
        if (i < 6) p++;
        long lim = i ? COLOUR_MAX : COLOUR_HUE_MAX;
        v[i] = (int)(x < -lim ? -lim : x > lim ? lim : x);
    }
    ColourAdjust a = {v[0], v[1], v[2], v[3], v[4], v[5], v[6]};
    *out = a;
    return true;
}
static inline void colour_preset_format(const ColourAdjust *a, char *buf, size_t size) {
    snprintf(buf, size, "%d,%d,%d,%d,%d,%d,%d", a->hue, a->temperature, a->tint, a->brightness, a->contrast,
             a->saturation, a->gamma);
}

// The filter as the shader runs it: out = clamp(rows * vec4(pow(c, gamma), 1), 0, 1)
typedef struct {
    float row[3][4];
    float gamma;  // the exponent (1: none)
} ColourMatrix;

static inline void colour_matrix(const ColourAdjust *a, ColourMatrix *out) {
    // gains (temperature, tint), never above 1
    float t = a->temperature / 100.0f, m = a->tint / 100.0f;
    float gain[3] = {1, 1, 1};
    if (t > 0) gain[1] *= 1 - 0.06f * t, gain[2] *= 1 - 0.18f * t;
    else if (t < 0) gain[0] *= 1 + 0.18f * t, gain[1] *= 1 + 0.06f * t;
    if (m > 0) gain[1] *= 1 - 0.15f * m;
    else if (m < 0) gain[0] *= 1 + 0.10f * m, gain[2] *= 1 + 0.10f * m;
    // the hue's turn round (1, 1, 1)
    const float th = a->hue * 3.14159265f / 180.0f, c = cosf(th), s = sinf(th), k3 = (1 - c) / 3.0f, r3 = 0.57735027f * s;
    const float hue[3][3] = {{c + k3, k3 - r3, k3 + r3}, {k3 + r3, c + k3, k3 - r3}, {k3 - r3, k3 + r3, c + k3}};
    // saturation round Rec. 709 luma
    const float sat = 1 + a->saturation / 100.0f, w[3] = {0.2126f, 0.7152f, 0.0722f};
    float satm[3][3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) satm[i][j] = (1 - sat) * w[j] + (i == j ? sat : 0);
    const float con = 1 + a->contrast / 100.0f, offset = 0.5f * (1 - con) + a->brightness / 400.0f;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            float v = 0;  // (satm * hue)[i][j] * gain[j]
            for (int k = 0; k < 3; k++) v += satm[i][k] * hue[k][j];
            out->row[i][j] = con * v * gain[j];
        }
        out->row[i][3] = offset;
    }
    out->gamma = exp2f(-a->gamma / 100.0f);
}

// ---- HDR output ([Colour] HDR, HDRBrightness, HDRHighlights): the game's SDR picture shown on an HDR screen. Its
// white goes to the paper white (203 nits, BT.2408's reference for graphics, times 2^(HDRBrightness/100): 101 to 406);
// the brightest part of the picture (above half of the white, in light) is spread up towards the screen's peak, by
// HDRHighlights (0: half of the way, -100: none, the SDR picture as it is at the paper white; 100: all of the way), the
// colours kept (the three channels scaled together). Encoded for HDR10 (BT.2020 primaries, SMPTE ST 2084).
typedef struct {
    float paper;  // the paper white, in units of 10000 nits (PQ's 1)
    float top;    // where the SDR white ends up, in paper whites (1: no expansion)
    float knee;   // the light (in paper whites) above which the expansion starts
} HdrParams;

#define HDR_PAPER_NITS 203.0f
#define HDR_PEAK_DEFAULT 1000.0f  // (a screen that doesn't say its peak)

static inline HdrParams hdr_params(int brightness, int highlights, float peak_nits) {
    HdrParams p;
    float paper = HDR_PAPER_NITS * exp2f(brightness / 100.0f);
    if (!(peak_nits > 0)) peak_nits = HDR_PEAK_DEFAULT;
    if (peak_nits > 10000) peak_nits = 10000;
    float e = 0.5f + highlights / 200.0f;
    e = e < 0 ? 0 : e > 1 ? 1 : e;
    float ratio = peak_nits / paper;
    if (ratio < 1) ratio = 1;
    p.paper = paper / 10000.0f;
    p.top = 1 + e * (ratio - 1);
    p.knee = 0.5f;
    return p;
}

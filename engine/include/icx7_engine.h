/*  ICX7 — C interface to the Rust dx7-core engine (engine/icx7-ffi).
    The engine always runs at icx7_engine_rate() (48000 Hz); the plugin resamples.
    Voices use the DX7 unpacked layout: 155 bytes, OP6 first (6 x 21), then globals and name. */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Icx7Engine Icx7Engine;

uint32_t    icx7_engine_rate (void);
Icx7Engine* icx7_create (uint32_t polyphony);
void        icx7_destroy (Icx7Engine*);
void        icx7_load_voice (Icx7Engine*, const uint8_t* voice155);
void        icx7_note_on (Icx7Engine*, uint8_t note, uint8_t velocity);
void        icx7_note_off (Icx7Engine*, uint8_t note);
void        icx7_control_change (Icx7Engine*, uint8_t cc, uint8_t value);
void        icx7_pitch_bend (Icx7Engine*, int16_t value);
void        icx7_set_bend_range (Icx7Engine*, uint8_t semitones);
void        icx7_all_notes_off (Icx7Engine*);
void        icx7_panic (Icx7Engine*);          /* silenzio immediato + reset dei controller */
void        icx7_render (Icx7Engine*, float* out, uint32_t frames);

uint32_t    icx7_parse_sysex (const uint8_t* data, size_t len, uint8_t* out32x155);
uint32_t    icx7_build_bulk_dump (const uint8_t* voices32x155, uint8_t* out4104);
void        icx7_sanitize_voice (uint8_t* voice155);
uint32_t    icx7_rom1a_voice (uint32_t index, uint8_t* out155);

#ifdef __cplusplus
}
#endif

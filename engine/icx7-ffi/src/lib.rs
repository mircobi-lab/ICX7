//! ICX7 engine bridge: a small C ABI over `dx7-core` so the JUCE plugin
//! (C++) can drive the Rust DX7 engine.
//!
//! Notes on the engine (see dx7-core):
//! - the engine only supports 44100 and 48000 Hz and keeps those tables in
//!   process-wide statics, so ICX7 always runs it at 48000 Hz and the plugin
//!   resamples to the host rate;
//! - voices are rendered in 64-sample blocks; `Synth::render_mono` buffers
//!   any remainder internally, so any frame count is accepted.
//!
//! Every exported function catches panics so a Rust panic can never unwind
//! into the host.

use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::OnceLock;

use dx7_core::{DxVoice, Synth};

pub const ICX7_ENGINE_RATE: u32 = 48000;
pub const ICX7_VOICE_BYTES: usize = 155;
pub const ICX7_MAX_POLYPHONY: usize = 32;

/// Opaque engine handle.
pub struct Icx7Engine {
    synth: Synth,
}

fn guard<R>(fallback: R, f: impl FnOnce() -> R) -> R {
    catch_unwind(AssertUnwindSafe(f)).unwrap_or(fallback)
}

/// Engine sample rate (always 48000).
#[no_mangle]
pub extern "C" fn icx7_engine_rate() -> u32 {
    ICX7_ENGINE_RATE
}

/// Create an engine with `polyphony` voices (1..=32).
#[no_mangle]
pub extern "C" fn icx7_create(polyphony: u32) -> *mut Icx7Engine {
    guard(std::ptr::null_mut(), || {
        let voices = (polyphony as usize).clamp(1, ICX7_MAX_POLYPHONY);
        let mut synth = Synth::with_max_voices(ICX7_ENGINE_RATE, voices);
        synth.set_master_volume(1.0);
        Box::into_raw(Box::new(Icx7Engine { synth }))
    })
}

/// Destroy an engine created by `icx7_create`.
///
/// # Safety
/// `e` must come from `icx7_create` and not be used afterwards.
#[no_mangle]
pub unsafe extern "C" fn icx7_destroy(e: *mut Icx7Engine) {
    if !e.is_null() {
        let _ = catch_unwind(AssertUnwindSafe(|| drop(Box::from_raw(e))));
    }
}

/// Load a voice in DX7 unpacked (VCED) layout, 155 bytes: 6 x 21 operator
/// bytes (OP6 first), then pitch EG, algorithm, feedback, LFO, transpose and
/// the 10-byte name. Out-of-range values are clamped by the parser.
/// The new voice applies to notes started afterwards (as on the DX7).
///
/// # Safety
/// `e` must be valid; `data` must point to 155 readable bytes.
#[no_mangle]
pub unsafe extern "C" fn icx7_load_voice(e: *mut Icx7Engine, data: *const u8) {
    if e.is_null() || data.is_null() {
        return;
    }
    let bytes = &*(data as *const [u8; ICX7_VOICE_BYTES]);
    let eng = &mut *e;
    guard((), || eng.synth.load_patch(DxVoice::from_unpacked(bytes)));
}

/// # Safety
/// `e` must be valid.
#[no_mangle]
pub unsafe extern "C" fn icx7_note_on(e: *mut Icx7Engine, note: u8, velocity: u8) {
    if let Some(eng) = e.as_mut() {
        guard((), || eng.synth.note_on(note.min(127), velocity.min(127)));
    }
}

/// # Safety
/// `e` must be valid.
#[no_mangle]
pub unsafe extern "C" fn icx7_note_off(e: *mut Icx7Engine, note: u8) {
    if let Some(eng) = e.as_mut() {
        guard((), || eng.synth.note_off(note.min(127)));
    }
}

/// MIDI control change (1 mod wheel, 7 volume, 11 expression, 64 sustain,
/// 100/101/6 RPN 0 pitch-bend range).
///
/// # Safety
/// `e` must be valid.
#[no_mangle]
pub unsafe extern "C" fn icx7_control_change(e: *mut Icx7Engine, cc: u8, value: u8) {
    if let Some(eng) = e.as_mut() {
        guard((), || eng.synth.control_change(cc & 0x7F, value.min(127)));
    }
}

/// Pitch bend, raw value -8192..=8191.
///
/// # Safety
/// `e` must be valid.
#[no_mangle]
pub unsafe extern "C" fn icx7_pitch_bend(e: *mut Icx7Engine, value: i16) {
    if let Some(eng) = e.as_mut() {
        guard((), || eng.synth.pitch_bend(value.clamp(-8192, 8191)));
    }
}

/// Pitch bend range in semitones (0..=24).
///
/// # Safety
/// `e` must be valid.
#[no_mangle]
pub unsafe extern "C" fn icx7_set_bend_range(e: *mut Icx7Engine, semitones: u8) {
    if let Some(eng) = e.as_mut() {
        guard((), || eng.synth.set_pitch_bend_range(semitones));
    }
}

/// Release every note and the sustain pedal.
///
/// # Safety
/// `e` must be valid.
#[no_mangle]
pub unsafe extern "C" fn icx7_all_notes_off(e: *mut Icx7Engine) {
    if let Some(eng) = e.as_mut() {
        guard((), || {
            eng.synth.control_change(64, 0);
            for n in 0..128u8 {
                eng.synth.note_off(n);
            }
        });
    }
}

/// Render `frames` mono samples at 48000 Hz into `out` (range -1..1).
/// On failure the buffer is zeroed.
///
/// # Safety
/// `e` must be valid; `out` must point to `frames` writable floats.
#[no_mangle]
pub unsafe extern "C" fn icx7_render(e: *mut Icx7Engine, out: *mut f32, frames: u32) {
    if out.is_null() || frames == 0 {
        return;
    }
    let buf = std::slice::from_raw_parts_mut(out, frames as usize);
    let Some(eng) = e.as_mut() else {
        buf.fill(0.0);
        return;
    };
    let ok = guard(false, || {
        eng.synth.render_mono(buf);
        true
    });
    if !ok {
        buf.fill(0.0);
    }
}

/// Parse a DX7 32-voice bulk dump (.syx, 4104 bytes or more) into 32
/// unpacked voices of 155 bytes each (`out` must hold 32 * 155 bytes).
/// Returns the number of voices written (32) or 0 if the data is not a
/// bulk dump. A single-voice dump (F0 43 0n 00 01 1B + 155 bytes) is also
/// accepted and yields 1 voice.
///
/// # Safety
/// `data` must point to `len` readable bytes, `out` to 32*155 writable bytes.
#[no_mangle]
pub unsafe extern "C" fn icx7_parse_sysex(data: *const u8, len: usize, out: *mut u8) -> u32 {
    if data.is_null() || out.is_null() || len == 0 {
        return 0;
    }
    let input = std::slice::from_raw_parts(data, len);
    let output = std::slice::from_raw_parts_mut(out, 32 * ICX7_VOICE_BYTES);
    guard(0, || parse_sysex(input, output))
}

fn parse_sysex(input: &[u8], output: &mut [u8]) -> u32 {
    // Find the first Yamaha SysEx message (some files carry junk before it).
    let Some(start) = input
        .windows(2)
        .position(|w| w[0] == 0xF0 && w[1] == 0x43)
    else {
        return 0;
    };
    let msg = &input[start..];

    if msg.len() >= 6 && msg[3] == 0x09 {
        if let Ok(voices) = DxVoice::parse_bulk_dump(msg) {
            for (i, v) in voices.iter().take(32).enumerate() {
                let u = v.to_unpacked();
                output[i * ICX7_VOICE_BYTES..(i + 1) * ICX7_VOICE_BYTES]
                    .copy_from_slice(&u[..ICX7_VOICE_BYTES]);
            }
            return voices.len().min(32) as u32;
        }
    }
    if msg.len() >= 6 + ICX7_VOICE_BYTES && msg[3] == 0x00 {
        let mut raw = [0u8; ICX7_VOICE_BYTES];
        raw.copy_from_slice(&msg[6..6 + ICX7_VOICE_BYTES]);
        // Round-trip through the parser so out-of-range data gets clamped.
        let u = DxVoice::from_unpacked(&raw).to_unpacked();
        output[..ICX7_VOICE_BYTES].copy_from_slice(&u[..ICX7_VOICE_BYTES]);
        return 1;
    }
    0
}

/// Build a 32-voice bulk dump (4104 bytes, with checksum) from 32 unpacked
/// voices. `out` must hold 4104 bytes. Returns the bytes written.
///
/// # Safety
/// `voices` must point to 32*155 readable bytes, `out` to 4104 writable bytes.
#[no_mangle]
pub unsafe extern "C" fn icx7_build_bulk_dump(voices: *const u8, out: *mut u8) -> u32 {
    if voices.is_null() || out.is_null() {
        return 0;
    }
    let input = std::slice::from_raw_parts(voices, 32 * ICX7_VOICE_BYTES);
    let output = std::slice::from_raw_parts_mut(out, 4104);
    guard(0, || {
        output[..6].copy_from_slice(&[0xF0, 0x43, 0x00, 0x09, 0x20, 0x00]);
        let mut sum: u8 = 0;
        for i in 0..32 {
            let mut raw = [0u8; ICX7_VOICE_BYTES];
            raw.copy_from_slice(&input[i * ICX7_VOICE_BYTES..(i + 1) * ICX7_VOICE_BYTES]);
            let packed = DxVoice::from_unpacked(&raw).to_packed();
            for (j, b) in packed.iter().enumerate() {
                output[6 + i * 128 + j] = *b;
                sum = sum.wrapping_add(*b);
            }
        }
        output[4102] = (!sum).wrapping_add(1) & 0x7F;
        output[4103] = 0xF7;
        4104
    })
}

/// Canonicalise a 155-byte voice (clamp every field to its DX7 range).
///
/// # Safety
/// `inout` must point to 155 readable and writable bytes.
#[no_mangle]
pub unsafe extern "C" fn icx7_sanitize_voice(inout: *mut u8) {
    if inout.is_null() {
        return;
    }
    let bytes = &mut *(inout as *mut [u8; ICX7_VOICE_BYTES]);
    guard((), || {
        let u = DxVoice::from_unpacked(bytes).to_unpacked();
        bytes.copy_from_slice(&u[..ICX7_VOICE_BYTES]);
    });
}

static ROM1A: OnceLock<Vec<[u8; ICX7_VOICE_BYTES]>> = OnceLock::new();

/// Copy ROM1A factory voice `index` (0..32) into `out` (155 bytes).
/// Returns 1 on success.
///
/// # Safety
/// `out` must point to 155 writable bytes.
#[no_mangle]
pub unsafe extern "C" fn icx7_rom1a_voice(index: u32, out: *mut u8) -> u32 {
    if out.is_null() || index >= 32 {
        return 0;
    }
    let output = std::slice::from_raw_parts_mut(out, ICX7_VOICE_BYTES);
    guard(0, || {
        let rom = ROM1A.get_or_init(|| {
            dx7_core::load_rom1a()
                .iter()
                .map(|v| {
                    let mut a = [0u8; ICX7_VOICE_BYTES];
                    a.copy_from_slice(&v.to_unpacked()[..ICX7_VOICE_BYTES]);
                    a
                })
                .collect()
        });
        output.copy_from_slice(&rom[index as usize]);
        1
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn renders_epiano() {
        unsafe {
            let e = icx7_create(16);
            let mut v = [0u8; 155];
            assert_eq!(icx7_rom1a_voice(10, v.as_mut_ptr()), 1);
            assert_eq!(&v[145..155], b"E.PIANO 1 ");
            icx7_load_voice(e, v.as_ptr());
            icx7_note_on(e, 60, 100);
            let mut buf = vec![0f32; 4800];
            // odd sizes on purpose
            icx7_render(e, buf.as_mut_ptr(), 1000);
            icx7_render(e, buf.as_mut_ptr().add(1000), 3800);
            let peak = buf.iter().fold(0f32, |a, &x| a.max(x.abs()));
            assert!(peak > 0.01, "peak {peak}");
            icx7_all_notes_off(e);
            icx7_destroy(e);
        }
    }

    #[test]
    fn bulk_roundtrip() {
        unsafe {
            let mut voices = vec![0u8; 32 * 155];
            for i in 0..32 {
                icx7_rom1a_voice(i, voices.as_mut_ptr().add(i as usize * 155));
            }
            let mut dump = vec![0u8; 4104];
            assert_eq!(icx7_build_bulk_dump(voices.as_ptr(), dump.as_mut_ptr()), 4104);
            assert!(DxVoice::bulk_dump_checksum_ok(&dump));
            assert_eq!(&dump[6..4102], &dx7_core::ROM1A_VOICE_DATA[..]);
            let mut back = vec![0u8; 32 * 155];
            assert_eq!(icx7_parse_sysex(dump.as_ptr(), dump.len(), back.as_mut_ptr()), 32);
            assert_eq!(back, voices);
        }
    }

    #[test]
    fn garbage_is_safe() {
        unsafe {
            let junk = [0xF0u8, 0x43, 0, 9, 1, 2, 3];
            let mut out = vec![0u8; 32 * 155];
            assert_eq!(icx7_parse_sysex(junk.as_ptr(), junk.len(), out.as_mut_ptr()), 0);
            let e = icx7_create(8);
            let wild = [0xFFu8; 155];
            icx7_load_voice(e, wild.as_ptr());
            for n in 0..128 {
                icx7_note_on(e, n, 127);
            }
            let mut buf = vec![0f32; 48000];
            icx7_render(e, buf.as_mut_ptr(), buf.len() as u32);
            assert!(buf.iter().all(|x| x.is_finite() && x.abs() <= 1.0));
            icx7_destroy(e);
        }
    }
}

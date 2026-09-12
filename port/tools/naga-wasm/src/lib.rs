//! WGSL -> GLSL ES 3.00 translation for the WebGL2 fallback.
//!
//! Exported with a plain C ABI (no wasm-bindgen) so the JS side is a small
//! hand-written loader: it copies the WGSL and entry-point strings into wasm
//! memory, calls `translate`, and reads back a JSON document containing the
//! GLSL source and naga's reflection info (which uniform block / sampler
//! names correspond to which WebGPU bind group and binding).

use naga::back::glsl;
use naga::valid::{Capabilities, ValidationFlags, Validator};
use naga::ShaderStage;
use std::alloc::{alloc, dealloc, Layout};

#[no_mangle]
pub extern "C" fn nw_alloc(size: usize) -> *mut u8 {
    if size == 0 {
        return std::ptr::null_mut();
    }
    unsafe { alloc(Layout::from_size_align_unchecked(size, 1)) }
}

#[no_mangle]
pub extern "C" fn nw_free(ptr: *mut u8, size: usize) {
    if !ptr.is_null() && size != 0 {
        unsafe { dealloc(ptr, Layout::from_size_align_unchecked(size, 1)) }
    }
}

fn json_escape(s: &str) -> String {
    let mut out = String::with_capacity(s.len() + 8);
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if (c as u32) < 0x20 => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out
}

fn translate_impl(wgsl: &str, entry: &str, stage: u32) -> Result<String, String> {
    let stage = match stage {
        0 => ShaderStage::Vertex,
        1 => ShaderStage::Fragment,
        _ => return Err("unsupported stage".into()),
    };
    let module = naga::front::wgsl::parse_str(wgsl).map_err(|e| e.emit_to_string(wgsl))?;
    let info = Validator::new(ValidationFlags::all(), Capabilities::empty())
        .validate(&module)
        .map_err(|e| e.emit_to_string(wgsl))?;

    let options = glsl::Options {
        version: glsl::Version::Embedded { version: 300, is_webgl: true },
        // Flip Y and remap Z from WebGPU clip space to GL clip space, as
        // wgpu's GL backend does. The polyfill flips viewports/scissors to match.
        writer_flags: glsl::WriterFlags::ADJUST_COORDINATE_SPACE,
        binding_map: Default::default(),
        zero_initialize_workgroup_memory: false,
    };
    let pipeline_options = glsl::PipelineOptions {
        shader_stage: stage,
        entry_point: entry.to_string(),
        multiview: None,
    };
    let mut glsl_out = String::new();
    let mut writer = glsl::Writer::new(&mut glsl_out, &module, &info, &options, &pipeline_options, Default::default())
        .map_err(|e| format!("{e}"))?;
    let reflection = writer.write().map_err(|e| format!("{e}"))?;

    // Reflection: uniform block names and combined texture/sampler names by (group, binding).
    let mut uniforms = String::new();
    for (handle, name) in reflection.uniforms.iter() {
        let var = &module.global_variables[*handle];
        if let Some(b) = &var.binding {
            if !uniforms.is_empty() {
                uniforms.push(',');
            }
            uniforms.push_str(&format!(
                "{{\"group\":{},\"binding\":{},\"name\":\"{}\"}}",
                b.group,
                b.binding,
                json_escape(name)
            ));
        }
    }
    let mut textures = String::new();
    for (name, mapping) in reflection.texture_mapping.iter() {
        let tex = &module.global_variables[mapping.texture];
        let (tg, tb) = tex.binding.as_ref().map(|b| (b.group, b.binding)).unwrap_or((0, 0));
        let (sg, sb) = mapping
            .sampler
            .and_then(|s| module.global_variables[s].binding.as_ref().map(|b| (b.group, b.binding)))
            .unwrap_or((u32::MAX, u32::MAX));
        if !textures.is_empty() {
            textures.push(',');
        }
        textures.push_str(&format!(
            "{{\"name\":\"{}\",\"texture\":[{},{}],\"sampler\":[{},{}]}}",
            json_escape(name),
            tg,
            tb,
            sg,
            sb
        ));
    }
    Ok(format!(
        "{{\"glsl\":\"{}\",\"uniforms\":[{}],\"textures\":[{}]}}",
        json_escape(&glsl_out),
        uniforms,
        textures
    ))
}

/// Translate `wgsl` (UTF-8, `wgsl_len` bytes) for entry point `entry` at
/// `stage` (0 = vertex, 1 = fragment). Returns a pointer to a UTF-8 result
/// whose length is written to `*out_len`; the first byte is '{' for a JSON
/// success document or 'E' followed by an error message. Free with nw_free.
#[no_mangle]
pub extern "C" fn nw_translate(
    wgsl: *const u8,
    wgsl_len: usize,
    entry: *const u8,
    entry_len: usize,
    stage: u32,
    out_len: *mut usize,
) -> *mut u8 {
    let wgsl = unsafe { std::str::from_utf8_unchecked(std::slice::from_raw_parts(wgsl, wgsl_len)) };
    let entry = unsafe { std::str::from_utf8_unchecked(std::slice::from_raw_parts(entry, entry_len)) };
    let text = match translate_impl(wgsl, entry, stage) {
        Ok(json) => json,
        Err(e) => format!("E{e}"),
    };
    let bytes = text.into_bytes().into_boxed_slice();
    let len = bytes.len();
    unsafe {
        *out_len = len;
    }
    Box::into_raw(bytes) as *mut u8
}

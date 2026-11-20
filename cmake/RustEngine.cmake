# Compila il motore Rust (engine/: dx7-core + icx7-ffi) come libreria statica e la espone come target "icx7_engine".
# Su macOS crea un binario universale (una compilazione per architettura di CMAKE_OSX_ARCHITECTURES, poi lipo).
find_program(CARGO_EXECUTABLE cargo HINTS "$ENV{HOME}/.cargo/bin" "$ENV{USERPROFILE}/.cargo/bin" REQUIRED)

set(ICX7_ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/engine")
set(ICX7_RUST_TARGET_DIR "${CMAKE_BINARY_DIR}/rust")
file(GLOB_RECURSE ICX7_RUST_SOURCES CONFIGURE_DEPENDS
     "${ICX7_ENGINE_DIR}/*.rs" "${ICX7_ENGINE_DIR}/*/Cargo.toml" "${ICX7_ENGINE_DIR}/Cargo.toml")

set(ICX7_CARGO_ENV "CARGO_TARGET_DIR=${ICX7_RUST_TARGET_DIR}")

if(APPLE)
    set(ICX7_ENGINE_LIB "${ICX7_RUST_TARGET_DIR}/universal/libicx7_engine.a")
    set(_libs "")
    set(_cmds "")
    foreach(arch IN LISTS CMAKE_OSX_ARCHITECTURES)
        if(arch STREQUAL "arm64")
            set(triple aarch64-apple-darwin)
        elseif(arch STREQUAL "x86_64")
            set(triple x86_64-apple-darwin)
        else()
            message(FATAL_ERROR "Architettura non gestita: ${arch}")
        endif()
        list(APPEND _cmds COMMAND ${CMAKE_COMMAND} -E env ${ICX7_CARGO_ENV} "MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}"
                          "${CARGO_EXECUTABLE}" build --release --locked -p icx7-ffi --target ${triple})
        list(APPEND _libs "${ICX7_RUST_TARGET_DIR}/${triple}/release/libicx7_engine.a")
    endforeach()
    add_custom_command(OUTPUT "${ICX7_ENGINE_LIB}"
        ${_cmds}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${ICX7_RUST_TARGET_DIR}/universal"
        COMMAND lipo -create ${_libs} -output "${ICX7_ENGINE_LIB}"
        WORKING_DIRECTORY "${ICX7_ENGINE_DIR}"
        DEPENDS ${ICX7_RUST_SOURCES}
        COMMENT "Motore Rust ICX7 (universale)"
        VERBATIM)
elseif(MSVC)
    set(ICX7_ENGINE_LIB "${ICX7_RUST_TARGET_DIR}/x86_64-pc-windows-msvc/release/icx7_engine.lib")
    add_custom_command(OUTPUT "${ICX7_ENGINE_LIB}"
        COMMAND ${CMAKE_COMMAND} -E env ${ICX7_CARGO_ENV}
                "${CARGO_EXECUTABLE}" build --release --locked -p icx7-ffi --target x86_64-pc-windows-msvc
        WORKING_DIRECTORY "${ICX7_ENGINE_DIR}"
        DEPENDS ${ICX7_RUST_SOURCES}
        COMMENT "Motore Rust ICX7"
        VERBATIM)
else()
    set(ICX7_ENGINE_LIB "${ICX7_RUST_TARGET_DIR}/release/libicx7_engine.a")
    add_custom_command(OUTPUT "${ICX7_ENGINE_LIB}"
        COMMAND ${CMAKE_COMMAND} -E env ${ICX7_CARGO_ENV}
                "${CARGO_EXECUTABLE}" build --release --locked -p icx7-ffi
        WORKING_DIRECTORY "${ICX7_ENGINE_DIR}"
        DEPENDS ${ICX7_RUST_SOURCES}
        COMMENT "Motore Rust ICX7"
        VERBATIM)
endif()

add_custom_target(icx7_engine_build DEPENDS "${ICX7_ENGINE_LIB}")
add_library(icx7_engine STATIC IMPORTED GLOBAL)
set_target_properties(icx7_engine PROPERTIES
    IMPORTED_LOCATION "${ICX7_ENGINE_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${ICX7_ENGINE_DIR}/include")
add_dependencies(icx7_engine icx7_engine_build)

# librerie di sistema richieste dalla libreria standard di Rust
if(WIN32)
    set_property(TARGET icx7_engine PROPERTY INTERFACE_LINK_LIBRARIES kernel32 advapi32 ntdll userenv ws2_32 dbghelp bcrypt synchronization)
elseif(APPLE)
    set_property(TARGET icx7_engine PROPERTY INTERFACE_LINK_LIBRARIES "-framework CoreFoundation" iconv)
else()
    set_property(TARGET icx7_engine PROPERTY INTERFACE_LINK_LIBRARIES pthread dl m)
endif()

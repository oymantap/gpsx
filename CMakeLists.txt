cmake_minimum_required(VERSION 3.21)

project(neon_runner C)

set(PSN00BSDK_LIBS "${CMAKE_SOURCE_DIR}/vendor/PSn00bSDK/lib/libpsn00b"
    CACHE PATH "PSn00bSDK lib directory")

include("${PSN00BSDK_LIBS}/cmake/sdk.cmake")

include_directories("${CMAKE_SOURCE_DIR}/src")

# --- KONVERSI AUTOMATIS PNG KE TIM (RUNNER GITHUB ACTIONS) ---
set(ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets")
set(GEN_ASSETS_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen_assets")
file(MAKE_DIRECTORY "${GEN_ASSETS_DIR}")

# Path ke tool img2tim bawaan PSn00bSDK
set(IMG2TIM "${CMAKE_SOURCE_DIR}/vendor/PSn00bSDK/bin/img2tim")

# Command konversi enemy.png -> enemy.tim
add_custom_command(
    OUTPUT "${GEN_ASSETS_DIR}/enemy.tim"
    COMMAND ${IMG2TIM} -o "${GEN_ASSETS_DIR}/enemy.tim" -bpp 8 -bkg 000000 "${ASSETS_DIR}/enemy.png"
    DEPENDS "${ASSETS_DIR}/enemy.png"
    COMMENT "Converting enemy.png to enemy.tim"
)

# Command konversi player.png -> player.tim
add_custom_command(
    OUTPUT "${GEN_ASSETS_DIR}/player.tim"
    COMMAND ${IMG2TIM} -o "${GEN_ASSETS_DIR}/player.tim" -bpp 8 -bkg 000000 "${ASSETS_DIR}/player.png"
    DEPENDS "${ASSETS_DIR}/player.png"
    COMMENT "Converting player.png to player.tim"
)

# Command konversi potion.png -> potion.tim
add_custom_command(
    OUTPUT "${GEN_ASSETS_DIR}/potion.tim"
    COMMAND ${IMG2TIM} -o "${GEN_ASSETS_DIR}/potion.tim" -bpp 8 -bkg 000000 "${ASSETS_DIR}/potion.png"
    DEPENDS "${ASSETS_DIR}/potion.png"
    COMMENT "Converting potion.png to potion.tim"
)

# Target khusus untuk trigger konversi asset
add_custom_target(convert_assets DEPENDS
    "${GEN_ASSETS_DIR}/enemy.tim"
    "${GEN_ASSETS_DIR}/player.tim"
    "${GEN_ASSETS_DIR}/potion.tim"
)

psn00bsdk_add_executable(neon_runner GPREL
    src/main.c
)
add_dependencies(neon_runner convert_assets)

# --- MEMBUAT SYSTEM.CNF & ISO.XML ---
set(ISO_DIR "${CMAKE_CURRENT_BINARY_DIR}/iso")
file(MAKE_DIRECTORY "${ISO_DIR}")

file(WRITE "${ISO_DIR}/system.cnf"
"BOOT=cdrom:\\NEONRUN.EXE;1\nTCB=4\nEVENT=10\nSTACK=801FFFF0\n")

# Masukkan seluruh file TIM ke dalam ISO Tree
file(WRITE "${ISO_DIR}/iso.xml"
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<iso_project image_name=\"neon_runner.bin\" cue_sheet=\"neon_runner.cue\">
    <track type=\"data\">
        <identifiers
            system=\"PLAYSTATION\"
            application=\"PLAYSTATION\"
            volume=\"NEONRUNNER\"
            volume_set=\"NEONRUNNER\"
            publisher=\"RYCL\"
        />
        <directory_tree>
            <file name=\"system.cnf\" type=\"data\" source=\"${ISO_DIR}/system.cnf\"/>
            <file name=\"NEONRUN.EXE\" type=\"data\" source=\"${CMAKE_CURRENT_BINARY_DIR}/neon_runner.exe\"/>
            <file name=\"ENEMY.TIM\" type=\"data\" source=\"${GEN_ASSETS_DIR}/enemy.tim\"/>
            <file name=\"PLAYER.TIM\" type=\"data\" source=\"${GEN_ASSETS_DIR}/player.tim\"/>
            <file name=\"POTION.TIM\" type=\"data\" source=\"${GEN_ASSETS_DIR}/potion.tim\"/>
            <dummy sectors=\"256\"/>
        </directory_tree>
    </track>
</iso_project>
")

psn00bsdk_add_cd_image(neon_runner_disc neon_runner
    "${ISO_DIR}/iso.xml"
    DEPENDS neon_runner
)

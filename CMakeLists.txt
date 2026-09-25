cmake_minimum_required(VERSION 3.21)

project(neon_runner C)

set(PSN00BSDK_LIBS "${CMAKE_SOURCE_DIR}/vendor/PSn00bSDK/lib/libpsn00b"
    CACHE PATH "PSn00bSDK lib directory")

include("${PSN00BSDK_LIBS}/cmake/sdk.cmake")

include_directories("${CMAKE_SOURCE_DIR}/src")

psn00bsdk_add_executable(neon_runner GPREL
    src/main.c
)

set(ISO_DIR "${CMAKE_CURRENT_BINARY_DIR}/iso")
set(ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets")
file(MAKE_DIRECTORY "${ISO_DIR}")

file(WRITE "${ISO_DIR}/system.cnf"
"BOOT=cdrom:\\NEONRUN.EXE;1\nTCB=4\nEVENT=10\nSTACK=801FFFF0\n")

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
            <file name=\"ENEMY.TIM\" type=\"data\" source=\"${ASSETS_DIR}/enemy.tim\"/>
            <file name=\"PLAYER.TIM\" type=\"data\" source=\"${ASSETS_DIR}/player.tim\"/>
            <file name=\"POTION.TIM\" type=\"data\" source=\"${ASSETS_DIR}/potion.tim\"/>
            <dummy sectors=\"256\"/>
        </directory_tree>
    </track>
</iso_project>
")

psn00bsdk_add_cd_image(neon_runner_disc neon_runner
    "${ISO_DIR}/iso.xml"
    DEPENDS neon_runner
)

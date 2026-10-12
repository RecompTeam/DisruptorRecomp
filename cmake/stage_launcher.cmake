# Stage into the executable's directory, including build/Release with Visual
# Studio generators. Never overwrite a player's bindings or saved preferences.
foreach(pair IN ITEMS "game.toml|game.toml" "mouse-aim.ini|mouse-aim.ini"
                      "docs/PLAYING.md|GETTING_STARTED.md"
                      "release/windows/Play Disruptor.cmd|Play Disruptor.cmd"
                      "config/keybinds-modern.ini|keybinds.ini"
                      "release/windows/settings.toml|settings.toml")
    string(REPLACE "|" ";" paths "${pair}")
    list(GET paths 0 source)
    list(GET paths 1 destination)
    if(destination STREQUAL "keybinds.ini" OR destination STREQUAL "settings.toml")
        if(EXISTS "${DESTINATION}/${destination}")
            continue()
        endif()
    endif()
    configure_file("${SOURCE}/${source}" "${DESTINATION}/${destination}" COPYONLY)
endforeach()

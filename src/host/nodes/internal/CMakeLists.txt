target_sources(synth_canvas_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/envelope_node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/event_tunnel_node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lfo_node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/midi_input_node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sequencer_node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stepper_node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/transport_node.cpp
)

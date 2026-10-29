# Build the bundled 1.3.0 module: its installed reader loses message type/state across continuations.
set(_ws_root "${CMAKE_CURRENT_BINARY_DIR}/dependencies/oatpp-websocket-1.3.0-latest")
file(ARCHIVE_EXTRACT INPUT "${CMAKE_CURRENT_LIST_DIR}/../dependencies/oatpp-websocket-1.3.0-latest.zip" DESTINATION
     "${CMAKE_CURRENT_BINARY_DIR}/dependencies")
set(_ws_source "${_ws_root}/src/oatpp-websocket/WebSocket.cpp")
file(READ "${_ws_source}" _ws_original)
set(_ws_before [[      readPayload(frameHeader, nullptr);
      break;]])
set(_ws_after
    [[      {
        auto messageHeader = frameHeader;
        messageHeader.opcode = static_cast<v_uint8>(m_lastOpcode);
        readPayload(messageHeader, nullptr);
        if(frameHeader.fin) {
          m_lastOpcode = -1;
        }
      }
      break;]])
string(REPLACE "${_ws_before}" "${_ws_after}" _ws_patched "${_ws_original}")
if(_ws_patched STREQUAL _ws_original)
    message(FATAL_ERROR "Bundled oatpp-websocket continuation fix no longer matches its source")
endif()
file(WRITE "${_ws_source}" "${_ws_patched}")

add_library(
    app_websocket STATIC
    "${_ws_root}/src/oatpp-websocket/AsyncConnectionHandler.cpp"
    "${_ws_root}/src/oatpp-websocket/AsyncWebSocket.cpp"
    "${_ws_root}/src/oatpp-websocket/ConnectionHandler.cpp"
    "${_ws_root}/src/oatpp-websocket/Connector.cpp"
    "${_ws_root}/src/oatpp-websocket/Frame.cpp"
    "${_ws_root}/src/oatpp-websocket/Handshaker.cpp"
    "${_ws_root}/src/oatpp-websocket/SHA1.cpp"
    "${_ws_root}/src/oatpp-websocket/Utils.cpp"
    "${_ws_source}")
add_library(oatpp::oatpp-websocket ALIAS app_websocket)
target_include_directories(app_websocket SYSTEM PUBLIC "${_ws_root}/src")
target_link_libraries(app_websocket PUBLIC oatpp::oatpp)
app_apply_sanitizers(app_websocket)
apply_app_hardening(app_websocket)

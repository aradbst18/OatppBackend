# Install rules for OatppBackend.
#
# Layout: `bin/oatpp-backend` plus, only when APP_ENABLE_SWAGGER=ON, `share/swagger-res/` (Swagger UI assets). No config
# or version files are installed: the app creates `configs/app.conf` from defaults on first startup when missing (see
# Application::loadSettingsOrDefaults) and rewrites `configs/version.conf` on every startup (see
# Application::initInfrastructure). The checked-in `configs/app.{json,toml,xml,yaml}` are samples; installing all four
# would ship dead weight, mostly in the wrong format for the active build.
#
# Swagger res lookup: -DAPP_SWAGGER_RES_DIR=... wins; otherwise the oatpp-swagger install next to OATPP_BASE_DIR, then
# well-known prefixes, then the vendored copy under external/libs. The found source dir is baked in as
# OATPP_SWAGGER_RES_PATH for dev runs (binary in build/<preset>/); installed runs (out/bin/oatpp-backend) prefer the
# exe-relative ../share/swagger-res copy at runtime (see Swagger.cpp).

function(setup_install_rules)
    install(TARGETS app_server RUNTIME DESTINATION bin)

    if(NOT APP_ENABLE_SWAGGER)
        return()
    endif()

    if(NOT DEFINED APP_SWAGGER_RES_DIR OR NOT EXISTS "${APP_SWAGGER_RES_DIR}/index.html")
        set(_candidates "")
        if(DEFINED OATPP_BASE_DIR)
            list(APPEND _candidates "${OATPP_BASE_DIR}/bin/oatpp-swagger/res")
        endif()

        list(APPEND _candidates "/usr/local/include/oatpp-1.3.0/bin/oatpp-swagger/res"
             "${CMAKE_CURRENT_SOURCE_DIR}/external/libs/oatpp-swagger-1.3.1/res")

        set(_found "")
        foreach(_c IN LISTS _candidates)
            if(EXISTS "${_c}/index.html")
                set(_found "${_c}")
                break()
            endif()
        endforeach()

        set(APP_SWAGGER_RES_DIR
            "${_found}"
            CACHE PATH "Oatpp-swagger UI res dir (must contain index.html)")
    endif()

    if(EXISTS "${APP_SWAGGER_RES_DIR}/index.html")
        install(DIRECTORY "${APP_SWAGGER_RES_DIR}/" DESTINATION share/swagger-res)
        target_compile_definitions(app_core PUBLIC OATPP_SWAGGER_RES_PATH="${APP_SWAGGER_RES_DIR}")
    else()
        message(
            WARNING
                "APP_ENABLE_SWAGGER=ON but Swagger UI res dir not found (tried APP_SWAGGER_RES_DIR='${APP_SWAGGER_RES_DIR}'); "
                "/swagger/ui will fail at runtime")
    endif()
endfunction()

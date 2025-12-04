add_rules("mode.debug", "mode.release", "mode.releasedbg")

set_languages("c++latest")
set_warnings("allextra", "error")

set_symbols("hidden")
if is_mode("release") then
    set_optimize("fastest")
elseif is_mode("debug") then
    set_symbols("debug", "hidden")
    add_cxflags("-fno-omit-frame-pointer", { tools = { "clang", "gcc" } })
    add_cxflags("-fstandalone-debug", { tools = { "clang", "gcc" } })
    add_cxflags("-glldb", { tools = { "clang", "gcc" } })
    -- add_mxflags("-glldb", { tools = { "clang", "gcc" } })
elseif is_mode("releasedbg") then
    set_optimize("fast")
    set_symbols("debug", "hidden")
    add_cxflags("-fno-omit-frame-pointer", { tools = { "clang", "gcc" } })
    -- add_mxflags("-glldb", { tools = { "clang", "gcc" } })
end

add_repositories("tapzcrew-repo https://github.com/tapzcrew/xmake-repo main")

add_requires("glm  1.0.1", "frozen")

-- stormkit deps, remove when handled by xmake
add_requires("unordered_dense", "tl_function_ref", "cpptrace")

add_requires("stormkit", {
    configs = {
        entities = false,
        gpu = true,
        -- debug = is_mode("debug"),
    },
})

add_defines("PUGIXML_USE_STD_MODULE")
-- TODO disable unused exceptions and edit code accordingly
-- add_defines("PUGIXML_NO_EXCEPTIONS")
add_requires(
    "pugixml",
    "ftxui main"
)

add_requires("imgui", {
    configs = {
      vulkan = true,
      debug = is_mode("debug"),
      cxxflags = { "-DIMGUI_IMPL_VULKAN_NO_PROTOTYPES" },
    },
})

add_requireconfs(
    "glm",

    "pugixml",
    "cpptrace",
    { system=false }
)

-- if is_mode("debug") then
--     add_defines("_LIBCPP_DEBUG")
--     add_requireconfs(
--         "ftxui",
--         { debug = true }
--     )
-- end

add_requireconfs("**", { configs = { modules = true, std_import = true, cpp = "latest" }})
add_cxxflags("-fexperimental-library")
add_ldflags("-fexperimental-library")

option("compile_commands", { default = true, category = "root menu/support" })

if get_config("compile_commands") then
    add_rules("plugin.compile_commands.autoupdate", { outputdir = ".vscode", lsp = "clangd" })
end

target("markovjunior")
    set_kind("binary")

    add_packages("stormkit", { components = { "core", "log", "wsi", "gpu", "image" } })

    -- stormkit deps, remove when handled by xmake
    add_packages(
        "glm", "frozen",

        -- stormkit deps, remove when handled by xmake
        "unordered_dense", "tl_function_ref", "cpptrace",

        "pugixml",
        "ftxui",
        "imgui"
    )

    add_files(
        "lib/**.mpp",
        "src/**.mpp",
        "src/**.cpp"
    )
    set_rundir("$(projectdir)")

    if is_plat("macosx") then
        add_frameworks("Foundation", "AppKit", "Metal", "IOKit", "QuartzCore")
    end

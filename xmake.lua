add_rules("mode.debug", "mode.release")

option("target_type")
    set_default("server")
    set_showmenu(true)
    set_values("server", "client")
option_end()

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")
-- Keep the native ABI aligned with the LeviLamina version installed by the
-- test server. Unpinned resolution may select a newer incompatible package.
add_requires("levilamina 26.51.5", {configs = {target_type = get_config("target_type")}})
add_requires("ilistenattentively 0.17.0", {configs = {target_type = get_config("target_type")}})
add_requires("levibuildscript")

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

target("LuckyClover-Moderation")
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker")
    set_kind("shared")
    set_languages("c++20")
    set_symbols("debug")
    add_includedirs("src")
    add_headerfiles("src/**.h")
    add_files("src/**.cpp")
    add_packages("levilamina", "ilistenattentively")
    add_defines("NOMINMAX", "UNICODE", "_HAS_CXX23=1")
    set_exceptions("cxx")
    add_cxflags(
        "/utf-8", "/W4", "/w44265", "/w44289", "/w44296", "/w45263", "/w44738", "/w45204"
    )
    add_cxflags(
        "/EHs",
        "-Wno-microsoft-cast",
        "-Wno-invalid-offsetof",
        "-Wno-c++2b-extensions",
        "-Wno-microsoft-include",
        "-Wno-overloaded-virtual",
        "-Wno-ignored-qualifiers",
        "-Wno-missing-field-initializers",
        "-Wno-potentially-evaluated-expression",
        "-Wno-pragma-system-header-outside-header",
        {tools = {"clang_cl"}}
    )
    set_toolchains("clang-cl")


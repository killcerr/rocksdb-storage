add_rules("mode.debug", "mode.release")

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

option("target_type")
    set_default("server")
    set_showmenu(true)
    set_values("server", "client")
option_end()

-- add_requires("levilamina x.x.x") for a specific version
-- add_requires("levilamina develop") to use develop version
-- please note that you should add bdslibrary yourself if using dev version
set_toolchains("clang-cl")
add_requires("levilamina", {configs = {target_type = get_config("target_type")}})

add_requires("levibuildscript")

-- rocksdb with lz4 and zstd linked in, with the upstream recipe used as it is.
--
-- For the compilers of the MSVC family (clang-cl counts as one) rocksdb skips
-- find_package() for the compressors and takes their include and library paths
-- from its own thirdparty.inc, which defaults to a NuGet-style THIRDPARTY_HOME
-- tree and otherwise reads them from the environment -- the LZ4_/ZSTD_
-- variables documented there. With none of them set, the paths come out
-- drive-relative and point nowhere, and the build stops on a missing zstd.h.
-- Pointing those variables at the lz4 and zstd packages xmake installs is all
-- this recipe does differently.
package("rocksdb-compress")
    set_base("rocksdb")

    -- The upstream recipe installs a package for every config that is on, lz4
    -- and zstd among them; run its load script so the dependencies follow the
    -- configs here too.
    on_load(function (package)
        package:base():script("load")(package)
    end)

    on_install("windows|x64", function (package)
        if package:config("lz4") then
            local dir = package:dep("lz4"):installdir():gsub("\\", "/")
            os.setenv("LZ4_INCLUDE", dir .. "/include")
            os.setenv("LZ4_LIB_DEBUG", dir .. "/lib/lz4.lib")
            os.setenv("LZ4_LIB_RELEASE", dir .. "/lib/lz4.lib")
        end
        if package:config("zstd") then
            local dir = package:dep("zstd"):installdir():gsub("\\", "/")
            os.setenv("ZSTD_INCLUDE", dir .. "/include")
            os.setenv("ZSTD_LIB_DEBUG", dir .. "/lib/zstd.lib")
            os.setenv("ZSTD_LIB_RELEASE", dir .. "/lib/zstd.lib")
        end

        package:base():script("install")(package)
    end)
package_end()

-- lz4 and zstd are the two compressors the mod offers (see rocksdb_shim::DbConfig);
-- without a config here the build links none of them.
add_requires("rocksdb-compress v11.1.2", {configs = {shared = false, lz4 = true, zstd = true}})

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

target("rocksdb-storage")
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker")
    if is_plat("windows") then
        add_defines("NOMINMAX", "UNICODE", "_HAS_CXX23=1")
        set_exceptions("none") -- To avoid conflicts with /EHa.
        add_cxflags( "/EHa", "/utf-8", "/W4", "/w44265", "/w44289", "/w44296", "/w45263", "/w44738", "/w45204")
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
    end
    add_packages("levilamina", "rocksdb-compress")
    set_kind("shared")
    set_languages("c++20")
    set_symbols("debug")
    add_headerfiles("src/**.h")
    add_files("src/**.cpp")
    add_includedirs("src")


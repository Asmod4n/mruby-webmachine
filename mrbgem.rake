MRuby::Gem::Specification.new('mruby-webmachine') do |spec|
  spec.license = 'Apache-2'
  spec.author  = 'Hendrik Beskow'
  spec.summary = 'Webmachine: the HTTP state model, executed'

  spec.add_dependency 'mruby-c-ext-helpers'
  spec.add_dependency 'mruby-uri-parser'
  spec.add_dependency 'mruby-lmdb'
  spec.add_dependency 'mruby-mustache', github: 'Asmod4n/mruby-mustache', branch: 'main'
  spec.add_dependency 'mruby-fast-json'
  spec.add_dependency 'mruby-toml'
  spec.add_dependency 'mruby-slipstreamio', github: 'Asmod4n/slipstreamIO', branch: 'main'
  spec.add_dependency 'mruby-method'
  spec.add_dependency 'mruby-kernel-ext'
  spec.add_dependency 'mruby-proc-ext'
  # mruby-config: whichever build config bench and install run against,
  # its cxxflags, ldflags and libs come from here - not retyped in the
  # Rakefile, and not tied to a build's name.
  spec.add_dependency 'mruby-bin-config'

  spec.bins = %w[webmachine-cache webmachine-cache-check webmachine-serve]

  # zlib serves gzip and deflate. It is on every server distribution, and
  # its headers are a package of their own.
  unless spec.search_package('zlib')
    abort <<~MSG
      mruby-webmachine: zlib not found by pkg-config.

        Debian/Ubuntu   apt install zlib1g-dev
        RHEL/Fedora     dnf install zlib-devel
        Alpine          apk add zlib-dev
        macOS           xcode-select --install
    MSG
  end

  # libcrypto takes the fingerprint of an error: BLAKE2b with a key.
  unless spec.search_package('libcrypto')
    abort <<~MSG
      mruby-webmachine: libcrypto not found by pkg-config.

        Debian/Ubuntu   apt install libssl-dev
        RHEL/Fedora     dnf install openssl-devel
        Alpine          apk add openssl-dev
        macOS           brew install openssl@3
    MSG
  end

  # miniz reads the zip packs of assets. MINIZ_NO_ZLIB_COMPATIBLE_NAMES
  # keeps it off zlib's names, because both meet in one translation unit.
  mnz = "#{dir}/deps/miniz"
  abort 'mruby-webmachine: deps/miniz is empty - run: git submodule update --init' unless File.exist?("#{mnz}/miniz_zip.h")
  mnz_gen = "#{build_dir}/miniz"
  FileUtils.mkdir_p(mnz_gen)
  mnz_export = "#{mnz_gen}/miniz_export.h"
  mnz_export_content = "#pragma once\n#define MINIZ_EXPORT\n"
  unless File.exist?(mnz_export) && File.read(mnz_export) == mnz_export_content
    File.write(mnz_export, mnz_export_content)
  end
  spec.cc.include_paths << mnz << mnz_gen
  spec.cxx.include_paths << mnz << mnz_gen
  %w[MINIZ_NO_STDIO MINIZ_NO_DEFLATE_APIS MINIZ_NO_ZLIB_COMPATIBLE_NAMES].each do |d|
    spec.cc.defines << d
    spec.cxx.defines << d
  end
  spec.objs += %W[#{mnz}/miniz.c #{mnz}/miniz_tinfl.c #{mnz}/miniz_zip.c].map { |f|
    f.relative_path_from(dir).pathmap("#{build_dir}/%X#{spec.exts.object}")
  }

  # liburing carries slipstream's seam, so its archive reaches back into
  # symbols that live in libmruby.a - which the linker has already walked
  # by the time it reaches liburing. Named once more, it can answer.
  spec.linker.flags_after_libraries << build.libfile("#{build.build_dir}/lib/libmruby")

  %w[
    mruby-array-ext
    mruby-class-ext
    mruby-compar-ext
    mruby-enum-ext
    mruby-hash-ext
    mruby-numeric-ext
    mruby-object-ext
    mruby-range-ext
    mruby-sprintf
    mruby-string-ext
    mruby-symbol-ext
    mruby-toplevel-ext
  ].each { |gem| spec.add_test_dependency gem }
end

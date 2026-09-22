MRuby::Gem::Specification.new('mruby-webmachine') do |spec|
  spec.license = 'Apache-2'
  spec.author  = 'Hendrik Beskow'
  spec.summary = 'Webmachine: the HTTP state model, executed'

  spec.add_dependency 'mruby-c-ext-helpers'
  spec.add_dependency 'mruby-uri-parser'
  spec.add_dependency 'mruby-lmdb'
  spec.add_dependency 'mruby-mustache', github: 'Asmod4n/mruby-mustache', branch: 'main'
  spec.add_dependency 'mruby-slipstreamio', github: 'Asmod4n/slipstreamIO', branch: 'main'

  spec.bins = %w[webmachine-cache webmachine-cache-check webmachine-serve]

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
    mruby-kernel-ext
    mruby-numeric-ext
    mruby-object-ext
    mruby-proc-ext
    mruby-range-ext
    mruby-sprintf
    mruby-string-ext
    mruby-symbol-ext
    mruby-toplevel-ext
  ].each { |gem| spec.add_test_dependency gem }
end

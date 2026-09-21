MRuby::Gem::Specification.new('mruby-webmachine') do |spec|
  spec.license = 'Apache-2'
  spec.author  = 'Hendrik Beskow'
  spec.summary = 'Webmachine: the HTTP state model, executed'

  spec.add_dependency 'mruby-c-ext-helpers'
  spec.add_dependency 'mruby-uri-parser'
  spec.add_dependency 'mruby-lmdb'

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

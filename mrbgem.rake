MRuby::Gem::Specification.new('mruby-webmachine') do |spec|
  spec.license = 'Apache-2'
  spec.author  = 'Hendrik Beskow'
  spec.summary = 'Webmachine: the HTTP state model, executed'

  spec.add_dependency 'mruby-c-ext-helpers'

  # ada-url, vendored by this gem as an amalgamation. What is taken from
  # it is the percent decoder and the query parser. The URL parser is
  # WHATWG and this tree reads RFC 3986, so the request target is parsed
  # here and not there.
  spec.add_dependency 'mruby-uri-parser'

  # mruby has every Array, Hash, String, Enumerable and Numeric method
  # CRuby has. They live in the *-ext gems, and a gem that is not named
  # here is not in the build - which reads exactly like a method mruby
  # does not have. It has them. Name them.
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

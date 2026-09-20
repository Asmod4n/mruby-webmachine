MRuby::Gem::Specification.new('mruby-webmachine') do |spec|
  spec.license = 'Apache-2'
  spec.author  = 'Hendrik Beskow'
  spec.summary = 'Webmachine: the HTTP state model, executed'

  spec.add_dependency 'mruby-c-ext-helpers'

  # ada-url, vendored by this gem as an amalgamation. Three things are
  # taken from it.
  #
  #   ada::parse             reads conf.url, the one URL an operator
  #                          writes to say what the server listens on:
  #                          http://host:port, https://host:port or
  #                          unix:///path, with the settings of that
  #                          listener in its query.
  #   url_search_params      reads that query.
  #   unicode::percent_decode  decodes one component of a request target,
  #                          and the socket path of a unix:// URL, which
  #                          ada hands back in the URL's own spelling.
  #
  # What is not taken is the parsing of a request target. ada is WHATWG
  # and a request target is RFC 3986 and RFC 9112, and the two differ on
  # what they refuse. That parser is src/http.hpp's.
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

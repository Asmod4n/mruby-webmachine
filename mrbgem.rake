MRuby::Gem::Specification.new('mruby-webmachine') do |spec|
  spec.license = 'Apache-2'
  spec.author  = 'Hendrik Beskow'
  spec.summary = 'Webmachine: the HTTP state model, executed'

  spec.add_dependency 'mruby-c-ext-helpers'

  spec.add_test_dependency 'mruby-string-ext'
  spec.add_test_dependency 'mruby-range-ext'
end

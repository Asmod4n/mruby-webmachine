MRuby::Lockfile.disable

MRuby::Build.new('debug') do |conf|
  conf.toolchain
  conf.gem core: 'mruby-bin-mrbc'
  conf.enable_debug
  conf.enable_test
  conf.cc.flags << '-Og' << '-g3' << '-ggdb'
  conf.cxx.flags << '-Og' << '-g3' << '-ggdb' << '-std=c++23'
  conf.cc.defines << 'MRB_UTF8_STRING'
  conf.cxx.defines << 'MRB_UTF8_STRING'
  conf.gem File.expand_path(__dir__)
end

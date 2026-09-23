MRuby::Lockfile.disable

MRuby::Build.new('debug') do |conf|
  conf.toolchain :gcc
  conf.cc.command = 'gcc-16'
  conf.cxx.command = 'g++-16'
  conf.linker.command = 'g++-16'
  [conf.cc, conf.cxx].each { |compiler| compiler.cxx_compile_flag = '-x c++ -std=c++26' }
  conf.gem core: 'mruby-bin-mrbc'
  conf.enable_debug
  conf.enable_test
  conf.cc.flags << '-Og' << '-g3' << '-ggdb'
  conf.cxx.flags << '-Og' << '-g3' << '-ggdb' << '-std=c++26' << '-freflection'
  conf.cc.defines << 'MRB_UTF8_STRING'
  conf.cxx.defines << 'MRB_UTF8_STRING'
  conf.gem File.expand_path(__dir__)
end

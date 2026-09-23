MRuby::Lockfile.disable

MRuby::Build.new('release') do |conf|
  conf.toolchain :gcc
  conf.cc.command = 'gcc-16'
  conf.cxx.command = 'g++-16'
  conf.linker.command = 'g++-16'
  [conf.cc, conf.cxx].each { |compiler| compiler.cxx_compile_flag = '-x c++ -std=c++26' }
  conf.gem core: 'mruby-bin-mrbc'
  conf.cc.flags << '-O3' << '-march=x86-64-v3'
  conf.cxx.flags << '-O3' << '-march=x86-64-v3' << '-std=c++26' << '-freflection'
  conf.cc.defines << 'MRB_UTF8_STRING' << 'NDEBUG'
  conf.cxx.defines << 'MRB_UTF8_STRING' << 'NDEBUG'
  conf.gem File.expand_path(__dir__)
end

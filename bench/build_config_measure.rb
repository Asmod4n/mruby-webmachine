# One build of the server for the clock: the compiler and the
# instruction set come from the environment, so four runs give four
# clocks: g++ and clang, x86-64-v3 and x86-64-v4, all at -O2.
MRuby::Lockfile.disable

cxx = ENV.fetch('MEASURE_CXX')
cc = cxx.start_with?('clang') ? cxx.sub('++', '') : cxx.sub('g++', 'gcc')
march = ENV.fetch('MEASURE_MARCH')

MRuby::Build.new("measure-#{File.basename(cxx)}-#{march}") do |conf|
  conf.toolchain cxx.start_with?('clang') ? :clang : :gcc
  conf.cc.command = cc
  conf.cxx.command = cxx
  conf.linker.command = cxx
  [conf.cc, conf.cxx].each { |compiler| compiler.cxx_compile_flag = '-x c++ -std=c++26' }
  conf.gem core: 'mruby-bin-mrbc'
  conf.cc.flags << '-O2' << "-march=#{march}"
  conf.cxx.flags << '-O2' << "-march=#{march}" << '-std=c++26'
  conf.cxx.flags << '-freflection' unless cxx.start_with?('clang')
  conf.cc.defines << 'MRB_UTF8_STRING' << 'NDEBUG'
  conf.cxx.defines << 'MRB_UTF8_STRING' << 'NDEBUG'
  conf.gem File.expand_path('..', __dir__)
end

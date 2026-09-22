MRuby::Lockfile.disable

MRuby::Build.new('release') do |conf|
  conf.toolchain
  conf.gem core: 'mruby-bin-mrbc'
  conf.cc.flags << '-O3' << '-march=native' << '-fno-plt'
  conf.cxx.flags << '-O3' << '-march=native' << '-fno-plt' << '-std=c++23'
  conf.cc.defines << 'MRB_UTF8_STRING' << 'NDEBUG'
  conf.cxx.defines << 'MRB_UTF8_STRING' << 'NDEBUG'
  conf.gem File.expand_path(__dir__)
end

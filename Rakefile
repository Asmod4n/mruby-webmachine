MRUBY_DIR = File.expand_path('mruby', __dir__)
TEST_CONFIG = File.expand_path('build_config_debug.rb', __dir__)

file MRUBY_DIR do
  sh "git clone --depth 1 --recursive https://github.com/mruby/mruby.git #{MRUBY_DIR}"
end

desc 'build and run every test'
task test: MRUBY_DIR do
  sh "cd #{MRUBY_DIR} && MRUBY_CONFIG=#{TEST_CONFIG} rake all test"
end

task default: :test

MRUBY_DIR = File.expand_path('mruby', __dir__)
TEST_CONFIG = File.expand_path('build_config_debug.rb', __dir__)

file MRUBY_DIR do
  sh "git clone --depth 1 --recursive https://github.com/mruby/mruby.git #{MRUBY_DIR}"
end

desc 'build and run every test'
task test: MRUBY_DIR do
  sh "cd #{MRUBY_DIR} && MRUBY_CONFIG=#{TEST_CONFIG} rake all test"
end

require 'digest'
require 'etc'
require 'tmpdir'
require 'shellwords'

BENCH_MARCH = ENV.fetch('WM_MARCH', 'native').freeze

BENCH_THREADS_MAX = [Etc.nprocessors - 1, 1].max
BENCH_ALIGN = '-falign-functions=64 -falign-loops=64 -falign-jumps=64'.freeze
BENCH_FLAGS = "-std=c++23 -O3 -march=#{BENCH_MARCH} #{BENCH_ALIGN}".freeze

def bench_compiler(binary)
  comment = `readelf -p .comment #{binary} 2>/dev/null`
  clang = comment[/clang version.*/]
  return clang.squeeze(' ').strip if clang
  gcc = comment[/GCC:.*/]
  gcc ? gcc.sub('GCC: ', 'gcc ').squeeze(' ').strip : 'unreadable'
end

def bench_shared_library(binary, pattern)
  path = `ldd #{binary} 2>/dev/null`[pattern]
  path ? File.basename(File.realpath(path)) : nil
end

def bench_libc(binary)
  path = `ldd #{binary} 2>/dev/null`[%r{/[^ ]*/libc\.so[^ ]*}]
  return 'static' unless path

  said = `#{path} --version 2>/dev/null`.lines.first.to_s
  number = said.scan(/\d+\.\d+(?:\.\d+)?/)
  return "musl #{number.first || '?'}" if said.include?('musl')

  "glibc #{number.last || '?'}"
end

def bench_on
  return 'unreadable' unless system('command -v systemd-detect-virt >/dev/null 2>&1')

  vm = `systemd-detect-virt --vm 2>/dev/null`.strip
  container = `systemd-detect-virt --container 2>/dev/null`.strip
  parts = [vm, container].reject { |p| p.empty? || p == 'none' }
  parts.empty? ? 'metal' : parts.join('/')
end

def bench_scheduler
  state = %w[/sys/kernel/sched_ext/state /sys/kernel/sched_ext/root/state]
          .find { |f| File.readable?(f) && File.read(f).strip == 'enabled' }
  return 'none' unless state

  ops = %w[/sys/kernel/sched_ext/root/ops /sys/kernel/sched_ext/ops]
        .find { |f| File.readable?(f) }
  name = ops ? File.read(ops).strip : 'enabled-unnamed'
  all = %w[/sys/kernel/sched_ext/switch_all /sys/kernel/sched_ext/root/switch_all]
        .find { |f| File.readable?(f) }
  switched = all ? File.read(all).strip : '1'
  switched == '1' ? name : "#{name}(switch_all=#{switched})"
end

def bench_priority
  return [] unless system("renice -n -15 -p #{Process.pid} >/dev/null 2>&1")

  skip = []
  walk = Process.pid
  while walk.to_i.positive?
    skip << walk
    stat = begin
      File.read("/proc/#{walk}/stat")
    rescue SystemCallError
      break
    end
    walk = stat[/\) \S+ (\d+)/, 1].to_i
  end
  `ps -u #{Process.uid} -o pid=,ppid= --no-headers`.lines
    .map { |line| line.split.map(&:to_i) }
    .reject { |pid, ppid| ppid == 2 || pid == 2 || skip.include?(pid) }
    .map(&:first)
    .select { |pid| system("renice -n 15 -p #{pid} >/dev/null 2>&1") }
end

def bench_restore(pids)
  pids.each { |pid| system("renice -n 0 -p #{pid} >/dev/null 2>&1") }
  system("renice -n 0 -p #{Process.pid} >/dev/null 2>&1")
end

BENCH_USER = 'bench'

def bench_runner
  return [] unless system("id -u #{BENCH_USER} >/dev/null 2>&1")

  ['setpriv', "--reuid=#{BENCH_USER}", "--regid=#{BENCH_USER}", '--init-groups', '--']
end

def bench_provenance(binary, nice)
  cpuinfo = File.read('/proc/cpuinfo')
  flags = cpuinfo[/^flags\s*:(.*)$/, 1].to_s.split.sort
  {
    'commit' => `git -C #{__dir__} describe --always --dirty --tags`.strip,
    'build_flags' => BENCH_FLAGS,
    'march' => `g++ #{BENCH_FLAGS} -Q --help=target | awk '/^  -march=/{print $2; exit}'`.strip,
    'compiler' => bench_compiler(binary),
    'libstdcxx' => bench_shared_library(binary, %r{/[^ ]*libstdc\+\+\.so[^ ]*}) || 'static',
    'libc' => bench_libc(binary),
    'benchmark_lib' => `pkg-config --modversion benchmark 2>/dev/null`.strip,
    'kernel' => `uname -sr`.strip,
    'scheduler' => bench_scheduler,
    'bench_nice' => nice,
    'ran_as' => bench_runner.empty? ? Etc.getpwuid(Process.uid).name : BENCH_USER,
    'bench_threads_max' => BENCH_THREADS_MAX.to_s,
    'on' => bench_on,
    'cpu_model' => cpuinfo[/^model name\s*:\s*(.*)$/, 1].to_s.strip,
    'cpu_family_model_stepping' => %w[family model stepping]
      .map { |k| cpuinfo[/^cpu #{k}\s*:\s*(\d+)$/, 1] || cpuinfo[/^#{k}\s*:\s*(\d+)$/, 1] }
      .join(':'),
    'cpu_flag_count' => flags.size.to_s,
    'cpu_flag_digest' => Digest::SHA256.hexdigest(flags.join(' '))[0, 16]
  }
end

desc 'build and run the benchmarks; WM_MARCH sets -march (default: native)'
task :bench do
  sources = Dir[File.join(__dir__, 'bench', '*.cpp')].sort
  ada = Dir[File.join(__dir__, 'mruby', 'build', 'repos', '*', 'mruby-uri-parser')].first
  raise 'mruby-uri-parser is not checked out; run rake test once' if ada.nil?

  sources << File.join(ada, 'src', 'ada.cpp')
  includes = [File.join(__dir__, 'src'), File.join(ada, 'include')]
  binary = File.join(__dir__, 'bench', 'run')
  results = File.join(__dir__, 'bench', 'results')
  mkdir_p results
  sh "g++ #{BENCH_FLAGS} #{includes.map { |dir| "-I#{dir}" }.join(' ')} " \
     "#{sources.join(' ')} -lbenchmark -lpthread -o #{binary}"
  lowered = bench_priority
  context = bench_provenance(binary, lowered.empty? ? '0' : '1').map do |k, v|
    "--benchmark_context=#{k}=#{v.gsub(/[^A-Za-z0-9.:+~@\/-]+/, '_')}"
  end
  out = File.join(results, "#{Time.now.utc.strftime('%Y-%m-%d-%H%M%SZ')}.json")
  staged = File.join(Dir.tmpdir, "webmachine-bench-#{Process.pid}.json")
  sh(*bench_runner, binary, '--benchmark_repetitions=5',
     '--benchmark_report_aggregates_only=true',
     "--benchmark_out=#{staged}", '--benchmark_out_format=json', *context)
  mv staged, out
  bench_restore(lowered)
  puts "wrote #{out}"
end

FUZZ_CC = ENV.fetch('FUZZ_CC', 'clang++').freeze
FUZZ_FLAGS = "-std=c++23 -O1 -g -march=#{BENCH_MARCH} -fno-omit-frame-pointer " \
             '-fsanitize=address,undefined,fuzzer ' \
             '-D__cpp_concepts=202002L -Wno-builtin-macro-redefined'.freeze

desc 'build and run the fuzzer; rake fuzz[300] stops after 300 seconds; FUZZ_CC sets the compiler (default: clang++)'
task :fuzz, [:seconds] do |_task, args|
  ada = Dir[File.join(__dir__, 'mruby', 'build', 'repos', '*', 'mruby-uri-parser')].first
  raise 'mruby-uri-parser is not checked out; run rake test once' if ada.nil?

  fuzz = File.join(__dir__, 'fuzz')
  binary = File.join(fuzz, 'run')
  includes = [File.join(__dir__, 'src'), File.join(ada, 'include')]
  sh "#{FUZZ_CC} #{FUZZ_FLAGS} #{includes.map { |dir| "-I#{dir}" }.join(' ')} " \
     "#{File.join(fuzz, 'fuzz_http.cpp')} #{File.join(ada, 'src', 'ada.cpp')} -o #{binary}"
  seconds = args[:seconds].to_i
  next if args[:seconds] && seconds.zero?

  limit = seconds.positive? ? ["-max_total_time=#{seconds}"] : []
  sh(binary, File.join(fuzz, 'corpus'), "-dict=#{File.join(fuzz, 'http.dict')}",
     "-artifact_prefix=#{fuzz}/", '-max_len=512', '-print_final_stats=1', *limit)
end

CROSS_CXX = ENV.fetch('CROSS_CXX', 'aarch64-linux-gnu-g++').freeze
CROSS_RUN = ENV.fetch('CROSS_RUN', 'qemu-aarch64').freeze

desc 'run the wide scanners of another architecture under qemu; CROSS_CXX and CROSS_RUN set the tools'
task :crosscheck do
  %W[#{CROSS_CXX} #{CROSS_RUN}].each do |tool|
    next if system("command -v #{tool} >/dev/null 2>&1")

    raise "#{tool} is not installed; apt install g++-aarch64-linux-gnu qemu-user-static"
  end
  ada = Dir[File.join(__dir__, 'mruby', 'build', 'repos', '*', 'mruby-uri-parser')].first
  raise 'mruby-uri-parser is not checked out; run rake test once' if ada.nil?

  fuzz = File.join(__dir__, 'fuzz')
  binary = File.join(fuzz, 'crosscheck')
  includes = [File.join(__dir__, 'src'), File.join(ada, 'include')]
  sh "#{CROSS_CXX} -std=c++23 -O2 -g -static " \
     "#{includes.map { |dir| "-I#{dir}" }.join(' ')} " \
     "#{File.join(fuzz, 'crosscheck.cpp')} #{File.join(fuzz, 'fuzz_http.cpp')} " \
     "#{File.join(ada, 'src', 'ada.cpp')} -o #{binary}"
  sh CROSS_RUN, binary, File.join(fuzz, 'corpus')
end

task default: :test

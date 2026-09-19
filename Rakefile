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
require 'shellwords'

BENCH_FLAGS = '-std=c++23 -O3 -march=native'.freeze

# Read off the binary, never off PATH: a run can point at another build,
# and `g++ --version` would then name a compiler that never touched it.
def bench_compiler(binary)
  comment = `readelf -p .comment #{binary} 2>/dev/null`
  # crt1.o comes from the distribution's GCC and stands first on every
  # link line, so a clang binary carries a GCC string as well. A clang
  # string anywhere names the compiler; its absence names GCC.
  clang = comment[/clang version.*/]
  return clang.squeeze(' ').strip if clang
  gcc = comment[/GCC:.*/]
  gcc ? gcc.sub('GCC: ', 'gcc ').squeeze(' ').strip : 'unreadable'
end

def bench_shared_library(binary, pattern)
  path = `ldd #{binary} 2>/dev/null`[pattern]
  path ? File.basename(File.realpath(path)) : nil
end

# The library that will load, asked for its own version. Ubuntu writes
# "(Ubuntu GLIBC 2.39)" and openSUSE writes "(GNU libc)", so a search for
# the word GLIBC finds nothing on one of them. A line that guesses is
# worse than one that says it does not know.
def bench_libc(binary)
  path = `ldd #{binary} 2>/dev/null`[%r{/[^ ]*/libc\.so[^ ]*}]
  return 'static' unless path

  said = `#{path} --version 2>/dev/null`.lines.first.to_s
  number = said.scan(/\d+\.\d+(?:\.\d+)?/)
  return "musl #{number.first || '?'}" if said.include?('musl')

  "glibc #{number.last || '?'}"
end

# Same hardware, a container in a virtual machine and one on metal read
# different rates, so both questions are asked and neither is guessed.
def bench_on
  return 'unreadable' unless system('command -v systemd-detect-virt >/dev/null 2>&1')

  vm = `systemd-detect-virt --vm 2>/dev/null`.strip
  container = `systemd-detect-virt --container 2>/dev/null`.strip
  parts = [vm, container].reject { |p| p.empty? || p == 'none' }
  parts.empty? ? 'metal' : parts.join('/')
end

# A sched_ext scheduler is BPF, loaded at run time and swapped without a
# reboot, so the kernel name does not say who placed the threads. The
# same binary on the same machine then reads differently through the day.
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

def bench_provenance(binary)
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
    'on' => bench_on,
    'cpu_model' => cpuinfo[/^model name\s*:\s*(.*)$/, 1].to_s.strip,
    'cpu_family_model_stepping' => %w[family model stepping]
      .map { |k| cpuinfo[/^cpu #{k}\s*:\s*(\d+)$/, 1] || cpuinfo[/^#{k}\s*:\s*(\d+)$/, 1] }
      .join(':'),
    'cpu_flag_count' => flags.size.to_s,
    'cpu_flag_digest' => Digest::SHA256.hexdigest(flags.join(' '))[0, 16]
  }
end

desc 'build and run the benchmarks'
task :bench do
  sources = Dir[File.join(__dir__, 'bench', '*.cpp')].sort.join(' ')
  binary = File.join(__dir__, 'bench', 'run')
  results = File.join(__dir__, 'bench', 'results')
  mkdir_p results
  sh "g++ #{BENCH_FLAGS} -I#{File.join(__dir__, 'src')} #{sources} " \
     "-lbenchmark -lpthread -o #{binary}"
  context = bench_provenance(binary).map do |k, v|
    "--benchmark_context=#{k}=#{v.gsub(/[^A-Za-z0-9.:+~@\/-]+/, '_')}"
  end
  out = File.join(results, "#{Time.now.strftime('%Y-%m-%d-%H%M%S')}.json")
  sh binary, '--benchmark_repetitions=5', '--benchmark_report_aggregates_only=true',
     "--benchmark_out=#{out}", '--benchmark_out_format=json', *context
  puts "wrote #{out}"
end

task default: :test

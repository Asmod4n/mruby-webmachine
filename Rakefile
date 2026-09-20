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

# -march=native is not one ISA: this tree is built in containers that land
# on hosts that differ. WM_MARCH= pins it, so a row measured here can be
# read beside a row measured elsewhere.
BENCH_MARCH = ENV.fetch('WM_MARCH', 'native').freeze

# One cpu fewer than the box has, for the whole measurement, server and
# client together. What perturbs a median is the work beside the run - an
# agent building and testing in the background moves one arm of a
# comparison and not the others - so one cpu stays out of it.
BENCH_THREADS_MAX = [Etc.nprocessors - 1, 1].max
# The alignment flags are not decoration. Without them a relink moves a
# number by a third: the same source read 66, 90 and 90 ns against three
# builds of the timing library, and with -falign-* they all read 89.0 plus
# or minus 0.5. What moved was where the linker put the hot loop.
BENCH_ALIGN = '-falign-functions=64 -falign-loops=64 -falign-jumps=64'.freeze
BENCH_FLAGS = "-std=c++23 -O3 -march=#{BENCH_MARCH} #{BENCH_ALIGN}".freeze

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

# The run goes first at every point where it meets the other work of this
# user. Thirty points: -15 here, +15 for the rest. Ten does not do it, and
# +19 alone cannot build the gap from above.
#
# Kernel threads are skipped, and that is not cosmetic. As root, ps -u lists
# them too - 67 of the 76 processes on this box - and ksoftirqd carries the
# softirq that delivers a loopback packet, which is what a server bench
# measures. Putting it behind the run would distort the number the run
# exists to take.
#
# The run's own ancestors are skipped as well: a nice value raised beyond
# the limit cannot be lowered again, and one of them is the shell that
# started this.
#
# Gives back what it lowered, so it can be put back. A bench is a moment; an
# editor left at +15 outlives it.
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

# Never as root. Root holds CAP_SYS_NICE and CAP_IPC_LOCK, so RLIMIT_NICE and
# RLIMIT_MEMLOCK are advisory for it. A provided buffer pool is ordinary
# memory and is not charged, but io_uring's SQ and CQ rings are: measured on
# 6.18 against an 8192 KiB limit, root opened 512 rings of 32768 entries
# without a refusal, and an unprivileged user was stopped at two. A run as
# root does not meet the machine the server meets.
#
# The nice value survives the uid change, so the priority is raised first
# and the privileges dropped after. On a machine that allows it the same
# -15 comes from an RLIMIT_NICE grant in /etc/security/limits.d, and the
# user needs no help; this container has no CAP_SYS_RESOURCE, so no process
# can raise that limit and the inherited value stands in for it.
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

desc 'build and run the benchmarks'
task :bench do
  sources = Dir[File.join(__dir__, 'bench', '*.cpp')].sort
  # src/http.hpp calls ada, so the bench binary carries ada as well. The
  # copy is the one mruby-uri-parser vendors, and it is compiled here with
  # the bench's own flags rather than linked from the debug build: an -Og
  # object in an -O3 binary measures the wrong thing.
  ada = Dir[File.join(__dir__, 'mruby', 'build', 'repos', '*', 'mruby-uri-parser')].first
  raise 'mruby-uri-parser is not checked out; run rake test once' if ada.nil?

  sources << File.join(ada, 'src', 'ada.cpp')
  includes = [File.join(__dir__, 'src'), File.join(ada, 'include')]
  binary = File.join(__dir__, 'bench', 'run')
  results = File.join(__dir__, 'bench', 'results')
  mkdir_p results
  sh "g++ #{BENCH_FLAGS} #{includes.map { |dir| "-I#{dir}" }.join(' ')} " \
     "#{sources.join(' ')} -lbenchmark -lpthread -o #{binary}"
  # After the build, never before: a compiler running beside the run is the
  # noise this exists to keep out, and the sweep should see the process list
  # the run will actually meet.
  lowered = bench_priority
  context = bench_provenance(binary, lowered.empty? ? '0' : '1').map do |k, v|
    "--benchmark_context=#{k}=#{v.gsub(/[^A-Za-z0-9.:+~@\/-]+/, '_')}"
  end
  out = File.join(results, "#{Time.now.utc.strftime('%Y-%m-%d-%H%M%SZ')}.json")
  # The run cannot write into a tree it does not own, so it reports into the
  # world writable directory and this task, which does own the tree, moves it.
  staged = File.join(Dir.tmpdir, "webmachine-bench-#{Process.pid}.json")
  sh(*bench_runner, binary, '--benchmark_repetitions=5',
     '--benchmark_report_aggregates_only=true',
     "--benchmark_out=#{staged}", '--benchmark_out_format=json', *context)
  mv staged, out
  bench_restore(lowered)
  puts "wrote #{out}"
end

task default: :test

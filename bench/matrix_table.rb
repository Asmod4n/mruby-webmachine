# Reads the rounds bench/matrix.sh wrote and prints one table per test:
# an arm in each row, a build in each column, the median of the rounds
# and their coefficient of variation. The last row of each table is the
# highest one minute load average any round of that column saw.
#
#   ruby bench/matrix_table.rb bench/results/<stamp>-<name>-matrix

require 'json'

out = ARGV.fetch(0)
cells = Dir[File.join(out, '*')].select { |path| File.directory?(path) }.map { |path| File.basename(path) }
levels = ENV.fetch('BENCH_LEVELS', 'Os O2').split
marches = ENV.fetch('BENCH_MARCH', 'x86-64-v4').split
builds = %w[g++-16 clang++-23].product(levels, marches).map { |parts| parts.join('-') }
tests = cells.map { |cell| cell[/-test(.+)\z/, 1] }.uniq.sort_by { |test| test.to_i }
# Where each binary holds one arm, every binary of a build is one column.
tests = ['*'] if ENV['ONE_TABLE']

def median(values)
  sorted = values.sort
  middle = sorted.size / 2
  sorted.size.odd? ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2.0
end

def spread(values)
  mean = values.sum / values.size
  return 0.0 if values.size < 2 || mean.zero?

  Math.sqrt(values.sum { |value| (value - mean)**2 } / (values.size - 1)) / mean * 100
end

short = ->(build) { build.sub('clang++-23', 'clang').sub('g++-16', 'gcc').sub('-x86-64-', ' ').sub('-native', '') }

tests.each do |test|
  times = Hash.new { |hash, arm| hash[arm] = Hash.new { |inner, build| inner[build] = [] } }
  load = Hash.new(0.0)
  builds.each do |build|
    Dir[File.join(out, "#{build}-test#{test}", 'round-*.json')].each do |round|
      run = JSON.parse(File.read(round))
      load[build] = [load[build], run.dig('context', 'load_avg', 0).to_f].max
      run['benchmarks'].each { |row| times[row['name']][build] << row['real_time'] }
    end
  end
  puts
  puts "test #{test}: median ns of the rounds (CV %)"
  puts format('%-22s', '') + builds.map { |build| format('%15s', short.call(build)) }.join
  times.each do |arm, per_build|
    cells_of_arm = builds.map do |build|
      values = per_build[build]
      values.empty? ? format('%15s', '-') : format('%15s', format('%.1f (%.1f)', median(values), spread(values)))
    end
    puts format('%-22s', arm) + cells_of_arm.join
  end
  puts format('%-22s', 'highest load') + builds.map { |build| format('%15.2f', load[build]) }.join
end

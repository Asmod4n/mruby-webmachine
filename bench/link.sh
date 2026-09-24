C=$1; M=$2; R=; [ "$C" = g++ ] && R=-freflection
B=mruby/build/measure-$C-$M/bin/mruby-config
$C -O2 -march=$M -std=c++26 $R -rdynamic $($B --cxxflags) bench/measure.cpp -o build/measure-$C-$M \
  $($B --ldflags) -Wl,--whole-archive $($B --libmruby-path) -Wl,--no-whole-archive \
  $($B --ldflags-before-libs) $($B --libs) -lbenchmark
